extern "C"
{
#include <odroid_system.h>
#include <string.h>
#include <stdio.h>

#include "main.h"
#include "gw_lcd.h"
#include "gw_linker.h"
#include "gw_buttons.h"
#include "rg_i18n.h"
#include "common.h"
#include "rom_manager.h"
#include "appid.h"
#include "cpp_init_array.h"
#ifndef GNW_DISABLE_COMPRESSION
#include "lzma.h"
#endif
#include "heap.hpp"
}

#include <handy.h>

// Lynx native screen is 160x102. We render the core into a single RGB565
// framebuffer in the overlay BSS, then 2x2 nearest-scale it (320x204) and
// center it vertically on the 320x240 LCD.
#define LYNX_FPS                60
#define AUDIO_LYNX_SAMPLE_RATE  HANDY_AUDIO_SAMPLE_FREQ /* 32000 */

/* Plain static layout — exactly like the original (build 1219) where APB plays
 * fine. The struct/guard experiments were chasing a phantom clobber that was
 * really veneer-corrupted reads + the new core never installing; reverted. */
static CSystem *lynx = NULL;
static uint16_t lynx_framebuffer[HANDY_SCREEN_WIDTH * HANDY_SCREEN_HEIGHT];
static SWORD    lynx_audio_buffer[HANDY_AUDIO_BUFFER_LENGTH];

static void blit();

extern "C" size_t heap_free_mem(void); /* remaining overlay heap (RAM_EMU), see heap.cpp */

/* DEVICE FACT (measured, build 14:51:02): when firmware calls SaveState/LoadState
 * through the registered function pointers, `lynx` reads back 0 in that context
 * ("[save] FAIL ptr-null" while APB plays fine and UpdateFrame dereferences the
 * SAME pointer one frame later). The resume-load path already worked around this
 * by DEFERRING the real load into the main loop, where `lynx` is provably valid.
 * We do the same for menu save/load: the handler only records the request +
 * path; the actual ContextSave/Load runs in the loop next to UpdateFrame. */
static volatile bool s_pending_save = false;
static volatile bool s_pending_load = false;
static char          s_pending_path[300];

static void queue_state_op(volatile bool *flag, const char *savePathName)
{
    strncpy(s_pending_path, savePathName, sizeof s_pending_path - 1);
    s_pending_path[sizeof s_pending_path - 1] = '\0';
    *flag = true;
}

static bool LoadState(const char *savePathName)
{
    queue_state_op(&s_pending_load, savePathName);
    return true; /* real load happens in the loop where `lynx` is valid */
}

static bool SaveState(const char *savePathName)
{
    queue_state_op(&s_pending_save, savePathName);
    return true; /* real save happens in the loop where `lynx` is valid */
}

/* Runs from the main loop (NOT a firmware fn-ptr handler), so `lynx` is the same
 * provably-valid pointer that UpdateFrame uses. This is the only place the Lynx
 * savestate stream is actually read/written. */
static void process_pending_state_ops()
{
    if (s_pending_save)
    {
        s_pending_save = false;
        FILE *fp = fopen(s_pending_path, "wb");
        if (fp == NULL) { printf("[lynx save] fopen failed\n"); }
        else
        {
            bool ok = lynx->ContextSave(fp);
            fclose(fp);
            printf("[lynx save] done ok=%d\n", (int)ok);
        }
    }
    if (s_pending_load)
    {
        s_pending_load = false;
        FILE *fp = fopen(s_pending_path, "rb");
        if (fp == NULL) { lynx->Reset(); printf("[lynx load] fopen failed\n"); }
        else
        {
            bool ok = lynx->ContextLoad(fp);
            fclose(fp);
            if (!ok) lynx->Reset();
            printf("[lynx load] done ok=%d\n", (int)ok);
        }
    }
}

static void *Screenshot()
{
    lcd_wait_for_vblank();
    lcd_clear_active_buffer();
    blit();
    return lcd_get_active_buffer();
}

#ifndef GNW_DISABLE_COMPRESSION
// Memory to handle compressed roms
#define ROM_BUFF_LENGTH 524288 // 512kB (max bank-switched Lynx cart)
static uint8_t rom_memory[ROM_BUFF_LENGTH];
#endif

/* RAM kept free for the allocations CCart/CSystem still make on top of the ROM
 * copy: bank1 (≤64K), the Lynx 64K system RAM, plus heap slack. If the full ROM
 * copy + this headroom won't fit, we XIP bank0 from flash instead. */
#define LYNX_RAM_COPY_HEADROOM (192 * 1024)

static size_t getromdata(unsigned char **data)
{
    /* Small carts (≤256K, e.g. APB): copy the ROM into RAM so the 65C02 fetches
     * bank0 from fast RAM — the proven config (256K ROM + 64K bank1 + 64K CRam
     * fits the ~610K overlay heap).
     *
     * Large carts (512K, e.g. Ninja Gaiden III): a full RAM copy + bank1 + CRam
     * overflows the heap (the BSOD). For these we hand CCart the flash pointer so
     * it XIPs bank0 straight from memory-mapped QSPI flash (the path CCart already
     * implements: full bank0 present -> mCartBank0 = gamedata, no new[]/memcpy).
     * Only bank1 (64K) then lands in RAM, so 512K fits. Decision is by ACTUAL free
     * heap, not a hardcoded cap, so APB stays on the RAM path with no regression. */
    uint32_t size = 0;
    unsigned char *flashptr = (unsigned char *)odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &size, false);
    if (!flashptr || size == 0) { *data = NULL; return 0; }

    if (heap_free_mem() >= (size_t)size + LYNX_RAM_COPY_HEADROOM)
    {
        unsigned char *ram = new unsigned char[size];
        memcpy(ram, flashptr, size);
        *data = ram;
        printf("[lynx] ROM->RAM %lu B (free %u)\n", (unsigned long)size, (unsigned)heap_free_mem());
    }
    else
    {
        *data = flashptr; /* XIP bank0 directly from flash */
        printf("[lynx] ROM XIP-flash %lu B (free %u)\n", (unsigned long)size, (unsigned)heap_free_mem());
    }
    return size;
}

// 2x2 nearest scale of the 160x102 core framebuffer onto the centered
// 320x204 region of the 320x240 LCD back buffer.
static void blit()
{
    const uint16_t *src = lynx_framebuffer;
    uint16_t *out = (uint16_t *)lcd_get_active_buffer();
    const int y_offset = (GW_LCD_HEIGHT - HANDY_SCREEN_HEIGHT * 2) / 2; // 18

    for (int sy = 0; sy < HANDY_SCREEN_HEIGHT; sy++)
    {
        uint16_t *row0 = out + (y_offset + sy * 2) * GW_LCD_WIDTH;
        uint16_t *row1 = row0 + GW_LCD_WIDTH;
        const uint16_t *in = src + sy * HANDY_SCREEN_WIDTH;
        for (int sx = 0; sx < HANDY_SCREEN_WIDTH; sx++)
        {
            uint16_t c = in[sx];
            row0[sx * 2]     = c;
            row0[sx * 2 + 1] = c;
            row1[sx * 2]     = c;
            row1[sx * 2 + 1] = c;
        }
    }
}

static void sound_store()
{
    if (common_emu_sound_loop_is_muted())
    {
        gAudioBufferPointer = 0;
        return;
    }

    int32_t factor = common_emu_sound_get_volume();
    int16_t *out = audio_get_active_buffer();
    uint16_t len = audio_get_buffer_length();
    int gen = (int)(gAudioBufferPointer / 2); // stereo frames generated this frame

    for (uint16_t i = 0; i < len; i++)
    {
        int idx = (gen > 0) ? ((i < (uint16_t)gen) ? i : gen - 1) : 0;
        int32_t s = (int32_t)lynx_audio_buffer[idx * 2] + (int32_t)lynx_audio_buffer[idx * 2 + 1];
        out[i] = (int16_t)((s * factor) >> 9); // mix L+R, then apply volume/256
    }

    gAudioBufferPointer = 0;
}

static void map_buttons(odroid_gamepad_state_t *joystick)
{
    ULONG buttons = 0;
    if (joystick->values[ODROID_INPUT_UP])     buttons |= BUTTON_UP;
    if (joystick->values[ODROID_INPUT_DOWN])   buttons |= BUTTON_DOWN;
    if (joystick->values[ODROID_INPUT_LEFT])   buttons |= BUTTON_LEFT;
    if (joystick->values[ODROID_INPUT_RIGHT])  buttons |= BUTTON_RIGHT;
    if (joystick->values[ODROID_INPUT_A])      buttons |= BUTTON_A;
    if (joystick->values[ODROID_INPUT_B])      buttons |= BUTTON_B;
    if (joystick->values[ODROID_INPUT_START])  buttons |= BUTTON_PAUSE;
    if (joystick->values[ODROID_INPUT_SELECT]) buttons |= BUTTON_OPT1;
    if (joystick->values[ODROID_INPUT_X])      buttons |= BUTTON_OPT2;
    lynx->SetButtonData(buttons);
}

static void app_main_lynx_cpp(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    odroid_gamepad_state_t joystick;
    odroid_dialog_choice_t options[] = {ODROID_DIALOG_CHOICE_LAST};
    uint32_t rom_length = 0;
    uint8_t *rom_ptr = NULL;

    /* NOTE: no sd_trace here — keeping /lynx_trace.txt open on the SD card
     * collided with the save-state writes (fopen of /data/lynx/*.sav failed,
     * so saves never appeared). Plain printf -> UART only. */
    heap_itc_alloc(true);

    common_emu_state.pause_after_frames = start_paused ? 2 : 0;

    rom_length = getromdata(&rom_ptr);
    printf("[lynx] getromdata: rom_ptr=%p rom_length=%lu\n",
           (void *)rom_ptr, (unsigned long)rom_length);
    if (rom_ptr == NULL)
    {
        printf("Lynx: Failed to load ROM in flash/ram.\n");
        return;
    }

    // Init emulator (Handy renders directly to gPrimaryFrameBuffer in 565 LE)
    printf("[lynx] new CSystem...\n");
    lynx = new CSystem((const UBYTE *)rom_ptr, (ULONG)rom_length,
                       MIKIE_PIXEL_FORMAT_16BPP_565, AUDIO_LYNX_SAMPLE_RATE);

    /* DEVICE-CRITICAL: do the accept/reject check IMMEDIATELY after construction,
     * BEFORE any printf in this RAM-overlay frame. On device a RAM->flash
     * veneer'd printf here corrupts the very NEXT comparison: the DIAG proved the
     * same (lynx->mFileType == ILLEGAL) read false (eq_illegal=0) as a printf
     * ARG, yet evaluated true an instant later once the printf had run. So we
     * must test before the first post-construction printf in this frame. */
    if (lynx == NULL || lynx->mFileType == HANDY_FILETYPE_ILLEGAL)
    {
        printf("Lynx: ROM loading failed.\n");
        return;
    }
    /* Set the render/audio globals IMMEDIATELY — before any printf in this
     * RAM-overlay frame. A RAM->flash veneer'd printf here corrupts the very
     * next operation on device (proven: it flipped the fileType compare), so a
     * printf between the accept and these stores can leave gPrimaryFrameBuffer
     * pointing at garbage -> handy renders off-screen (black) and trashes
     * memory -> fault back to the menu. Stores first, log after. */
    gPrimaryFrameBuffer = (UBYTE *)lynx_framebuffer;
    gAudioBuffer = lynx_audio_buffer;
    gAudioEnabled = 1;
    printf("[lynx] CSystem ok, fb=%p (build %s %s)\n",
           (void *)gPrimaryFrameBuffer, __DATE__, __TIME__);

    uint32_t samplesPerFrame = AUDIO_LYNX_SAMPLE_RATE / LYNX_FPS;

    common_emu_state.frame_time_10us = (uint16_t)(100000 / LYNX_FPS + 0.5f);

    odroid_system_init(APPID_LYNX, AUDIO_LYNX_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, NULL, NULL, NULL, NULL);

    /* Resume-load is DEFERRED to the first loop iteration: LoadState needs
     * g_lynx_csystem, which is only captured (cleanly) once inside the loop.
     * Doing it here (before the loop) would read a 0 pointer. */
    bool pending_resume = load_state;
    if (!load_state)
        lcd_clear_buffers();

    audio_start_playing(samplesPerFrame);

    /* Main loop — printf-free like the other cores. */
    while (1)
    {
        wdog_refresh();
        common_emu_frame_loop();
        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &blit);

        uint8_t turbo_buttons = odroid_settings_turbo_buttons_get();
        bool turbo_a = (joystick.values[ODROID_INPUT_A] && (turbo_buttons & 1));
        bool turbo_b = (joystick.values[ODROID_INPUT_B] && (turbo_buttons & 2));
        bool turbo_button = odroid_button_turbos();
        if (turbo_a)
            joystick.values[ODROID_INPUT_A] = turbo_button;
        if (turbo_b)
            joystick.values[ODROID_INPUT_B] = !turbo_button;

        map_buttons(&joystick);

        /* Resume-load: queue it (LoadState just sets the pending flag now). */
        if (pending_resume)
        {
            pending_resume = false;
            odroid_system_emu_load_state(save_slot);
        }

        /* Do any queued save/load HERE, where `lynx` is the valid pointer that
         * UpdateFrame (below) uses — not in the firmware fn-ptr handler context
         * where it reads back 0. */
        process_pending_state_ops();

        lynx->UpdateFrame(true);

        blit();
        common_ingame_overlay();
        lcd_swap();
        sound_store();

        common_emu_sound_sync(false);
    }
}

extern "C" int app_main_lynx(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    // Call static C++ constructors now, *after* the overlay is copied into RAM.
    // Do not use __libc_init_array() as it will not work with the overlay.
    cpp_init_array(__init_array_lynx_start__, __init_array_lynx_end__);

    app_main_lynx_cpp(load_state, start_paused, save_slot);

    return 0;
}
