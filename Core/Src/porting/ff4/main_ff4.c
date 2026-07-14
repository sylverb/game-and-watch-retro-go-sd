// FF4 retro-go porting glue — Phase 5.4 proof-of-life.
//
// Wires the LakeSnes core to the real device:
//   1. Cache /roms/homebrew/ff4.sfc into the round-robin flash region
//      (odroid_overlay_cache_file_in_flash).
//   2. Initialise LakeSnes with the cached ROM bytes (ff4_init).
//   3. Spin a frame loop that advances LakeSnes one frame at a time
//      (ff4_step) while reading the gamepad. No LCD swap and no audio
//      yet — the goal of this phase is to confirm LakeSnes runs on
//      the STM32H7B0 without crashing. Press SELECT+START to exit
//      back to the launcher.

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <odroid_system.h>
#include "appid.h"
#include "main_ff4.h"
#include "main.h"
#include "gw_buttons.h"

extern bool ff4_init(const uint8_t *rom_bytes, int rom_length);
extern void ff4_step(void);
extern void ff4_shutdown(void);
extern void ff4_get_state(uint32_t *frames_out, uint64_t *cycles_out);
extern void ff4_blit_to_lcd(uint16_t *lcd_fb);
extern void ff4_set_button(int player, int button, bool pressed);
extern int  ff4_ppu_render_enabled;   /* frameskip hook (ff4/snes/ppu.c) */

/* FF4_FRAMESKIP: SNES frames emulated WITHOUT rendering before each
 * rendered one (so the LCD shows 1 frame in FF4_FRAMESKIP+1). Rendering
 * dominates the frame cost on this device (title screen measured 6-8
 * fps with every frame rendered, i.e. game logic at ~1/8 speed), and
 * the skip changes nothing emulation-visible: sprite evaluation still
 * runs on skipped frames (game-visible $213E flags), only the pixel
 * loop is bypassed -- proven WRAM-identical on the desktop harness
 * (--render-every). Held buttons persist across the batch, so input
 * sampling granularity coarsens to one gamepad read per displayed
 * frame. Override on the make command line if needed; 0 disables.
 *
 * The render period (FF4_FRAMESKIP+1) MUST be odd. SNES games fake
 * transparency by flickering elements at 30 Hz (drawn every other
 * frame -- FF4's battle menu does this); an even period samples a
 * single phase of that flicker, freezing the element fully opaque or
 * fully invisible (artifact observed on device with period 4 on
 * 2026-07-08). An odd period alternates phases across rendered frames
 * and keeps the flicker visible at a reduced rate.
 *
 * Default 0 (disabled): tested at 2 (render 1 of 3) on 2026-07-09 --
 * game speed does improve, but the display reads as a slideshow, not a
 * game. Kept as a tunable; the real speed levers are the per-line
 * renderer restructuring and continued dispatch porting. */
#ifndef FF4_FRAMESKIP
#define FF4_FRAMESKIP 0
#endif

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "rg_rtc.h"
#include "snes/snes.h"
#include "snes/cpu.h"
#include "snes/ppu.h"
#include "snes/apu.h"
#include "snes/statehandler.h"
extern Snes *ff4_snes;

#ifdef FF4_AUTOBOOT
/* D3 + D4 + D5 shared state. D1/D2 are stateless. */
static uint32_t g_diag_host_frame = 0;
/* Adaptive-skip probe counters (playable builds; read via gdb) */
uint32_t g_adaskip_rendered, g_adaskip_skipped;

/* D6R deterministic block ring (see the D6 block in the frame loop) */
#define D6R_SLOTS 24
typedef struct { uint32_t win_ms, emu_ms, rend_ms, blit_ms; } D6RBlock;
D6RBlock  g_d6_ring[D6R_SLOTS];
uint32_t  g_d6_blocks = 0;
static uint32_t g_diag_pc_bank_hist[256];
static uint32_t g_diag_pc_sample_count = 0;
/* miss ring written by ff4_dispatch_try (see dispatch_all.c patch);
 * we only read it here. */
extern uint32_t g_diag_miss_ring[8];
extern uint32_t ff4_dispatch_hits;
extern uint32_t ff4_dispatch_misses;
#endif

/* SNES joypad bit order (matches LakeSnes input_read serial shift). */
#define SNES_BTN_B      0
#define SNES_BTN_Y      1
#define SNES_BTN_SELECT 2
#define SNES_BTN_START  3
#define SNES_BTN_UP     4
#define SNES_BTN_DOWN   5
#define SNES_BTN_LEFT   6
#define SNES_BTN_RIGHT  7
#define SNES_BTN_A      8
#define SNES_BTN_X      9
#define SNES_BTN_L      10
#define SNES_BTN_R      11

/* Runtime button layout (menu-tunable). PAUSE/SET now belongs to the
 * retro-go pause menu (common_emu_input_loop), so it no longer doubles
 * as SNES X. New default layout, user-requested 2026-07-14:
 *   GAME  -> SNES X      (opens FF4's own main menu)
 *   TIME  -> SNES Start  (title screen / pause)
 *   A / B -> SNES A / B  (swappable from the pause menu)
 *   SNES Select is unmapped on a Mario unit (no physical button left;
 *   FF4 barely uses it). The Zelda unit's extra START/SELECT buttons
 *   (ODROID_INPUT_X / ODROID_INPUT_Y) keep feeding SNES X / Y. */
static bool ff4_swap_ab = false;

static void ff4_pump_buttons(const odroid_gamepad_state_t *js) {
    /* While PAUSE/SET is held every input belongs to the menu-combo
     * layer -- nothing may leak into the SNES pad. */
    const bool menu_held = js->values[ODROID_INPUT_VOLUME];
    const bool a = !menu_held && js->values[ODROID_INPUT_A];
    const bool b = !menu_held && js->values[ODROID_INPUT_B];
    ff4_set_button(1, SNES_BTN_UP,     !menu_held && js->values[ODROID_INPUT_UP]);
    ff4_set_button(1, SNES_BTN_DOWN,   !menu_held && js->values[ODROID_INPUT_DOWN]);
    ff4_set_button(1, SNES_BTN_LEFT,   !menu_held && js->values[ODROID_INPUT_LEFT]);
    ff4_set_button(1, SNES_BTN_RIGHT,  !menu_held && js->values[ODROID_INPUT_RIGHT]);
    ff4_set_button(1, SNES_BTN_A,      ff4_swap_ab ? b : a);
    ff4_set_button(1, SNES_BTN_B,      ff4_swap_ab ? a : b);
    ff4_set_button(1, SNES_BTN_X,      !menu_held && (js->values[ODROID_INPUT_START]
                                                      || js->values[ODROID_INPUT_X]));
    ff4_set_button(1, SNES_BTN_Y,      !menu_held && js->values[ODROID_INPUT_Y]);
    ff4_set_button(1, SNES_BTN_SELECT, false);
    ff4_set_button(1, SNES_BTN_START,  !menu_held && js->values[ODROID_INPUT_SELECT]);
}


#include "gw_lcd.h"
#include "gw_audio.h"
#include "gw_littlefs.h"
#include "porting/common.h"

/* ── retro-go pause-menu integration (savestates / screenshot) ─────────
 * Savestates stream through the statehandler byte hooks, a 512-byte
 * window and the gw_littlefs TAMP layer (FS_COMPRESS): the ~270 KB raw
 * state compresses ~4:1 (the stubbed 64 KB ARAM is near-empty, WRAM is
 * highly structured -- 67.7 KB measured on a real save), so all four
 * pause-menu slots fit the ~408 KB LittleFS. Uncompressed, a single
 * slot ate 67 of the 100 blocks and every second save died in
 * LFS_ERR_NOSPC (first multi-slot test, 2026-07-14). The header length
 * field (bytes 8..11) is injected in flight from a sizing pass -- see
 * the write hook; a post-stream fseek would CTZ-rewrite the whole file. */
static fs_file_t *ff4_ss_file;
static uint8_t  ff4_ss_iobuf[512];
static int      ff4_ss_iolen;    /* write side: bytes buffered */
static int      ff4_ss_iopos, ff4_ss_ioend;  /* read side: window cursor */
static bool     ff4_ss_werr;     /* write side: any fs_write failure */
static uint32_t ff4_ss_expected_size;        /* from the sizing pass */
static uint32_t ff4_ss_wroff;

static void ff4_ss_write_hook(uint8_t byte) {
    const uint32_t off = ff4_ss_wroff++;
    if (off >= 8 && off < 12)
        byte = (uint8_t)(ff4_ss_expected_size >> (8 * (off - 8)));
    ff4_ss_iobuf[ff4_ss_iolen++] = byte;
    if (ff4_ss_iolen == (int)sizeof(ff4_ss_iobuf)) {
        if (ff4_ss_file && !ff4_ss_werr
            && fs_write(ff4_ss_file, ff4_ss_iobuf, sizeof(ff4_ss_iobuf)) < 0)
            ff4_ss_werr = true;
        ff4_ss_iolen = 0;
        wdog_refresh();
    }
}

static uint8_t ff4_ss_read_hook(void) {
    if (ff4_ss_iopos >= ff4_ss_ioend) {
        ff4_ss_ioend = ff4_ss_file
            ? fs_read(ff4_ss_file, ff4_ss_iobuf, sizeof(ff4_ss_iobuf)) : 0;
        ff4_ss_iopos = 0;
        wdog_refresh();
        if (ff4_ss_ioend <= 0) return 0;   /* short file: statehandler gets zeros */
    }
    return ff4_ss_iobuf[ff4_ss_iopos++];
}

static bool ff4_system_SaveState(char *savePathName) {
    if (ff4_snes == NULL) return false;
    odroid_audio_mute(true);
    /* Tiny scratch: the hooks capture every byte; the statehandler
     * silently drops the overflow past the external buffer. */
    static uint8_t scratch[64];
    /* Pass 1 -- sizing only (no hook, no file): serialization is
     * deterministic and the game is paused, so pass 2 streams the exact
     * same bytes. */
    int size = snes_saveStateInto(ff4_snes, scratch, (int)sizeof(scratch));
    ff4_ss_file = fs_open(savePathName, FS_WRITE, FS_COMPRESS);
    if (ff4_ss_file == NULL) { odroid_audio_mute(false); return false; }
    /* Pass 2 -- stream through TAMP with the length injected in flight. */
    ff4_ss_expected_size = (uint32_t)size;
    ff4_ss_wroff = 0;
    ff4_ss_iolen = 0;
    ff4_ss_werr = false;
    sh_set_writeByte_hook(ff4_ss_write_hook);
    int size2 = snes_saveStateInto(ff4_snes, scratch, (int)sizeof(scratch));
    sh_set_writeByte_hook(NULL);
    if (ff4_ss_iolen > 0 && !ff4_ss_werr
        && fs_write(ff4_ss_file, ff4_ss_iobuf, ff4_ss_iolen) < 0)
        ff4_ss_werr = true;
    fs_close(ff4_ss_file); ff4_ss_file = NULL;
    if (ff4_ss_werr || size2 != size) {
        /* Do not leave a partial file squatting blocks: it reads as a
         * plausible slot in the menu and starves later saves. */
        unlink(savePathName);
        printf("FF4: savestate write FAILED (%s) -- slot removed\n",
               ff4_ss_werr ? "fs_write" : "size drift");
        odroid_audio_mute(false);
        return false;
    }
    printf("FF4: savestate saved, %d bytes raw -> %s\n", size, savePathName);
    odroid_audio_mute(false);
    return true;
}

static bool ff4_system_LoadState(char *savePathName) {
    if (ff4_snes == NULL || savePathName == NULL) return false;
    odroid_audio_mute(true);
    /* Header pre-read: the LSSF length field (bytes 8..11) carries the
     * RAW size snes_loadState validates against -- the compressed file
     * size is meaningless for that check. The decompressor cannot seek,
     * so close and reopen to restart the stream. */
    ff4_ss_file = fs_open(savePathName, FS_READ, FS_COMPRESS);
    if (ff4_ss_file == NULL) {
        printf("FF4: no savestate at %s\n", savePathName);
        odroid_audio_mute(false);
        return false;
    }
    uint8_t hdr[12];
    int got = fs_read(ff4_ss_file, hdr, sizeof(hdr));
    uint32_t raw_size = (uint32_t)hdr[8] | ((uint32_t)hdr[9] << 8)
                      | ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
    fs_close(ff4_ss_file); ff4_ss_file = NULL;
    if (got != (int)sizeof(hdr) || memcmp(hdr, "LSSF", 4) != 0 || raw_size == 0) {
        printf("FF4: savestate header invalid at %s\n", savePathName);
        odroid_audio_mute(false);
        return false;
    }
    ff4_ss_file = fs_open(savePathName, FS_READ, FS_COMPRESS);
    if (ff4_ss_file == NULL) { odroid_audio_mute(false); return false; }
    ff4_ss_iopos = ff4_ss_ioend = 0;
    static uint8_t dummy[16];   /* never read: the hook feeds every byte */
    sh_set_readByte_hook(ff4_ss_read_hook);
    bool ok = snes_loadState(ff4_snes, dummy, (int)raw_size);
    sh_set_readByte_hook(NULL);
    fs_close(ff4_ss_file); ff4_ss_file = NULL;
    printf("FF4: savestate load %lu raw bytes -> %s\n",
           (unsigned long)raw_size, ok ? "OK" : "FAIL");
    odroid_audio_mute(false);
    return ok;
}

static void *ff4_system_Screenshot(void) {
    lcd_clear_active_buffer();
    ff4_blit_to_lcd((uint16_t *)lcd_get_active_buffer());
    return lcd_get_active_buffer();
}

/* Render-skip mode (pause-menu entry). "adaptive" is the shipped v1
 * controller (skip at most every other render when the pacer falls
 * behind); "off" renders every frame; the fixed choices force an odd
 * render period (see the FF4_FRAMESKIP comment above) and disable the
 * adaptive controller. */
enum { FF4_SKIP_ADAPTIVE = 0, FF4_SKIP_OFF, FF4_SKIP_1IN3, FF4_SKIP_1IN5, FF4_SKIP_1IN7 };
static int g_ff4_skip_sel = FF4_SKIP_ADAPTIVE;
static int g_ff4_frameskip = FF4_FRAMESKIP;
static char ff4_frameskip_value[16];
static char ff4_swap_ab_value[16];

static bool ff4_frameskip_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat) {
    static const char *names[] = {"adaptive", "off", "1 in 3", "1 in 5", "1 in 7"};
    static const int   skips[] = {0, 0, 2, 4, 6};
    if (event == ODROID_DIALOG_PREV) g_ff4_skip_sel = (g_ff4_skip_sel + 4) % 5;
    if (event == ODROID_DIALOG_NEXT) g_ff4_skip_sel = (g_ff4_skip_sel + 1) % 5;
    g_ff4_frameskip = skips[g_ff4_skip_sel];
    strcpy(option->value, names[g_ff4_skip_sel]);
    return event == ODROID_DIALOG_ENTER;
}

static bool ff4_swap_ab_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat) {
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT)
        ff4_swap_ab = !ff4_swap_ab;
    strcpy(option->value, ff4_swap_ab ? "swapped" : "normal");
    return event == ODROID_DIALOG_ENTER;
}

/* Called from inside LakeSnes's snes_runFrame loop every ~4096 opcodes
 * to keep the WWDG (≈237 ms window on this build) happy. Without this
 * the first frame of pure-interpreter execution easily times out. */
void ff4_port_wdog_refresh(void) {
    wdog_refresh();
}

/* 48 kHz / 60 fps = 800 mono samples per frame. LakeSnes' dsp_getSamples
 * resamples from the SPC700's native 534 (NTSC) samples/frame to any
 * requested count, so we ask directly for 800. Stereo is generated in
 * the scratch buffer below, then downmixed to mono into the SAI DMA
 * half-buffer before submission. */
#define FF4_AUDIO_SAMPLE_RATE   48000
#define FF4_AUDIO_FRAME_SAMPLES 800
/* Deep DMA halves + an intermediate ring decouple audio production (one
 * frame of samples per EMULATED frame, render-skipped ones included) from
 * the loop's wall-clock jitter: a 30-50 ms rendered frame used to starve
 * the previous one-frame-deep DMA buffer and crackle (title, mode-7
 * intro, 2026-07-14). Halves of 3 frames absorb the bursts; the ring
 * (10 frames) carries the backlog; a genuine sustained deficit (zone
 * slower than real time) degrades to clean silence instead of garbage. */
#define FF4_AUDIO_HALF_SAMPLES  (FF4_AUDIO_FRAME_SAMPLES * 3)
#define FF4_AUDIO_RING_SAMPLES  (FF4_AUDIO_FRAME_SAMPLES * 10)

static int16_t ff4_audio_stereo_scratch[FF4_AUDIO_FRAME_SAMPLES * 2];
static int16_t ff4_audio_ring[FF4_AUDIO_RING_SAMPLES] __attribute__((section(".audio")));
static uint32_t ff4_ring_head, ff4_ring_tail;   /* head=write, tail=read */

static inline uint32_t ff4_ring_count(void) { return ff4_ring_head - ff4_ring_tail; }

extern Snes *ff4_snes;
extern void snes_setSamples(Snes *snes, int16_t *sampleData, int samplesPerFrame);

/* Producer: pull one emulated frame of DSP output, downmix to mono into
 * the ring. Called after EVERY ff4_step -- render-skipped frames too
 * (their audio exists; the old per-iteration submit dropped it). */
static void ff4_sound_produce(void) {
    if (ff4_snes == NULL || ff4_snes->apu == NULL) return;
    snes_setSamples(ff4_snes, ff4_audio_stereo_scratch, FF4_AUDIO_FRAME_SAMPLES);
    if (FF4_AUDIO_RING_SAMPLES - ff4_ring_count() < FF4_AUDIO_FRAME_SAMPLES)
        return;   /* ring full (paused pacer backlog): drop, DMA is ahead anyway */
    for (int i = 0; i < FF4_AUDIO_FRAME_SAMPLES; i++) {
        int32_t mono = (int32_t)ff4_audio_stereo_scratch[i * 2]
                     + (int32_t)ff4_audio_stereo_scratch[i * 2 + 1];
        ff4_audio_ring[(ff4_ring_head++) % FF4_AUDIO_RING_SAMPLES] = (int16_t)(mono >> 1);
    }
}

/* Consumer: on each DMA half flip, refill the freed half from the ring
 * (volume applied here), zero-padding any shortfall -- silence, not the
 * stale-buffer garbage loop the SAI used to replay on underrun. */
static void ff4_sound_pump(void) {
    static uint32_t last_flip;
    uint32_t flips = *(volatile uint32_t *)&dma_counter;
    if (flips == last_flip) return;
    last_flip = flips;

    int16_t *dma_buf  = audio_get_active_buffer();
    uint16_t dma_len  = audio_get_buffer_length();
    int16_t  vol      = common_emu_sound_loop_is_muted() ? 0 : common_emu_sound_get_volume();

    /* Optional volume ceiling for noise-sensitive environments (e.g. a shared
     * office). Build with -DFF4_AUDIO_VOL_CAP_PCT=5 to hard-cap output at ~5%
     * of full scale. `vol` is a 0..255 gain applied via >>8 (255 = unity), so
     * the cap is 255 * pct / 100. It is a ceiling, not an override: a lower
     * device volume setting stays lower. Default 0 = disabled (normal volume). */
#ifndef FF4_AUDIO_VOL_CAP_PCT
#define FF4_AUDIO_VOL_CAP_PCT 0
#endif
#if FF4_AUDIO_VOL_CAP_PCT > 0
    {
        const int16_t vol_cap = (int16_t)((255 * (FF4_AUDIO_VOL_CAP_PCT) + 50) / 100);
        if (vol > vol_cap) vol = vol_cap;
    }
#endif

    uint32_t avail = ff4_ring_count();
    uint16_t n = (avail < dma_len) ? (uint16_t)avail : dma_len;
    for (uint16_t i = 0; i < n; i++) {
        int32_t mono = ff4_audio_ring[(ff4_ring_tail++) % FF4_AUDIO_RING_SAMPLES];
        dma_buf[i] = (int16_t)((mono * vol) >> 8);
    }
    /* Shortfall (emulation slower than real time over the ring window):
     * clean silence instead of repeating stale samples. */
    for (uint16_t i = n; i < dma_len; i++) dma_buf[i] = 0;
}

#ifdef FF4_AUTO_SAVESTATE_DUMP
/* Streaming savestate dump: instead of holding the full ~280 KB in RAM
 * (which doesn't fit alongside the existing .overlay_ff4_bss), install a
 * per-byte hook on the LakeSnes StateHandler and forward each emitted byte
 * to a small line buffer that flushes to serial every 64 bytes. The
 * scratch buffer for snes_saveStateInto stays minimal — sh_writeByte
 * silently drops bytes past sh->allocSize since the hook has already done
 * its job. */
#include "snes/statehandler.h"

extern int snes_saveStateInto(Snes *snes, uint8_t *buf, int buf_size);

#define FF4_SAVESTATE_LINE_BYTES 64

static uint8_t  ff4_savestate_line[FF4_SAVESTATE_LINE_BYTES];
static int      ff4_savestate_line_off = 0;
static uint32_t ff4_savestate_total_off = 0;
static int      ff4_savestate_dump_done = 0;
/* Trigger frame measured in *emulated* SNES frames. We synthesize a brief
 * A press at frame 80..100 to dismiss the title screen, then capture a
 * snapshot at frame 700 — well into the post-title sequence so the loaded
 * state is visually distinct from a cold boot.
 *
 * At ~3 fps interpreter speed on G&W: ~30s to title, ~4 min wall-clock to
 * snapshot. The full dump finishes in another ~25 s. */
/* Hold A pressed for 200 emulated frames from frame 50 — enough that any
 * "press A to advance" prompt should fire. Then capture at frame 600 so
 * the resulting savestate is well into the post-A sequence. */
static const uint32_t FF4_SAVESTATE_DUMP_FRAME = 600;
static const uint32_t FF4_AUTO_A_PRESS_START   = 50;
static const uint32_t FF4_AUTO_A_PRESS_END     = 250;

static void ff4_savestate_flush_line(void) {
    if (ff4_savestate_line_off == 0) return;
    uint32_t line_start = ff4_savestate_total_off - ff4_savestate_line_off;
    printf("FF4_DUMP: %06x ", (unsigned)line_start);
    for (int i = 0; i < ff4_savestate_line_off; i++) {
        printf("%02x", ff4_savestate_line[i]);
    }
    printf("\n");
    ff4_savestate_line_off = 0;
    /* Watchdog every line and yield ~1 ms so the RTT/UART backbuffer can
     * drain before the next line gets emitted. Without the delay the
     * monitor receives only every Nth line and the savestate reconstructs
     * with holes (observed: 75/4300 lines captured in a 220 s window). */
    wdog_refresh();
    HAL_Delay(5);
}

static void ff4_savestate_streaming_hook(uint8_t b) {
    ff4_savestate_line[ff4_savestate_line_off++] = b;
    ff4_savestate_total_off++;
    if (ff4_savestate_line_off == FF4_SAVESTATE_LINE_BYTES) {
        ff4_savestate_flush_line();
    }
}

static void ff4_dump_savestate_serial(void) {
    if (ff4_snes == NULL) return;

    /* Tiny scratch — sh_writeByte will overflow it silently but the hook
     * has already captured every byte. */
    static uint8_t scratch[256];

    ff4_savestate_line_off = 0;
    ff4_savestate_total_off = 0;
    printf("=== FF4_SAVESTATE_DUMP_BEGIN ===\n");

    sh_set_writeByte_hook(ff4_savestate_streaming_hook);
    int size = snes_saveStateInto(ff4_snes, scratch, (int)sizeof(scratch));
    sh_set_writeByte_hook(NULL);

    ff4_savestate_flush_line();
    printf("=== FF4_SAVESTATE_DUMP_END size=%d ===\n", size);
}
#endif

int app_main_ff4(uint8_t load_state, uint8_t start_paused, int8_t save_slot) {
    (void)start_paused;   /* our own pacer runs the loop; pause-at-start unsupported */

    printf("FF4 start (Phase 5.4 proof-of-life)\n");
    printf("=== FF4_BOOT_MARKER_2026_06_13_AUTOTEST ===\n");

    odroid_system_init(APPID_FF4, FF4_AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(&ff4_system_LoadState, &ff4_system_SaveState,
                           &ff4_system_Screenshot, NULL, NULL, NULL);

    /* Flags defined in odroid_system.c / odroid_overlay.c (declared here
     * rather than in the retro-go-stm32 submodule headers, which stay
     * pristine). */
    extern bool odroid_system_disable_save_screenshot;
    extern bool odroid_overlay_hide_common_game_options;

    /* FF4 frontend tuning: no per-slot companion screenshot (the ~270 KB
     * state nearly fills the ~408 KB LittleFS -- the extra 150 KB write
     * blew a "no more free space" error screen on every save), and no
     * Turbo/Scaling/Filtering/Speed menu entries (inert here: this port
     * drives its own blit and its own pacer). */
    odroid_system_disable_save_screenshot = true;
    odroid_overlay_hide_common_game_options = true;

    /* Seed the RTC from the build timestamp when it was never set --
     * savestate timestamps read 01/01/1970 otherwise. The launcher's
     * time dialog stays the proper way to actually set it. */
    {
        struct tm tm;
        GW_GetUnixTM(&tm);
        if (tm.tm_year < (2024 - 1900)) {
            static const char mon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
            const char ms[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
            const char *hit = strstr(mon, ms);
            tm.tm_mon  = hit ? (int)((hit - mon) / 3) : 0;
            tm.tm_mday = atoi(__DATE__ + 4);
            tm.tm_year = atoi(__DATE__ + 7) - 1900;
            tm.tm_hour = atoi(__TIME__);
            tm.tm_min  = atoi(__TIME__ + 3);
            tm.tm_sec  = atoi(__TIME__ + 6);
            GW_SetUnixTM(&tm);
            printf("FF4: RTC seeded from build time %s %s\n", __DATE__, __TIME__);
        }
    }

    /* Boot volume. This loop never enters the common in-game overlay, so
     * the persisted volume level cannot be changed from inside FF4 -- if it
     * was last saved muted it stays muted forever. Force a comfortable
     * level at app start instead; 6 is 25% in common.c's volume_tbl.
     * Build with -DFF4_AUDIO_BOOT_VOLUME_LEVEL=-1 to keep the persisted
     * setting untouched. */
#ifndef FF4_AUDIO_BOOT_VOLUME_LEVEL
#define FF4_AUDIO_BOOT_VOLUME_LEVEL 6
#endif
#if FF4_AUDIO_BOOT_VOLUME_LEVEL >= 0
    odroid_audio_volume_set(FF4_AUDIO_BOOT_VOLUME_LEVEL);
#endif

    /* CPU clock. ENABLE_BOOT_OC only affects the cold-boot path in main();
     * the boot sequence then runs a sleep/wake cycle whose wake handler
     * (gw_sleep.c) re-applies the PERSISTED odroid_settings_cpu_oc_level --
     * default 0 -- silently clobbering the build-time overclock (found
     * 2026-07-12: DIVN1 read back 139 despite the OC call site being in
     * flash). Pin both: persist the level so every later wake keeps it,
     * and re-apply it now. Levels: 0 = 280 MHz, 1 = 312 MHz, 2 = 354 MHz.
     * Build with -DFF4_CPU_OC_LEVEL=-1 to leave the persisted setting
     * alone. */
#ifndef FF4_CPU_OC_LEVEL
#if defined(ENABLE_BOOT_OC) && ENABLE_BOOT_OC == 1
#define FF4_CPU_OC_LEVEL 2
#else
#define FF4_CPU_OC_LEVEL -1
#endif
#endif
#if FF4_CPU_OC_LEVEL >= 0
    odroid_settings_cpu_oc_level_set(FF4_CPU_OC_LEVEL);
    SystemClock_Config(FF4_CPU_OC_LEVEL);
#endif

    /* Cache the ROM into the round-robin flash region. The pointer
     * returned is XIP-addressable for the lifetime of this app.
     *
     * NOTE: "Final Fantasy IV.bin" is the FF4 overlay code (extracted
     * via objcopy --only-section=.overlay_ff4 in SD_CONTENT_STAMP).
     * The actual SNES ROM lives at a separate path so the menu entry
     * and the data are decoupled — mirrors the zelda3 / zelda3.ro
     * convention. User drops the ROM at /roms/homebrew/ff4.sfc. */
    uint32_t rom_length = 0;
    uint8_t *rom_bytes = odroid_overlay_cache_file_in_flash(
        "/roms/homebrew/ff4.sfc", &rom_length, false);
    if (rom_bytes == NULL || rom_length == 0) {
        printf("FF4: missing /roms/homebrew/ff4.sfc\n");
        return -1;
    }
    printf("FF4: rom cached at %p, %lu bytes\n",
           (void *)rom_bytes, (unsigned long)rom_length);

    if (!ff4_init(rom_bytes, (int)rom_length)) {
        printf("FF4: LakeSnes init failed\n");
        return -1;
    }

#ifdef FF4_LOAD_SAVESTATE
    {
        uint32_t st_size = 0;
        uint8_t *st_bytes = odroid_overlay_cache_file_in_flash(
            "/roms/homebrew/Final Fantasy IV.lss", &st_size, false);
        printf("=== FF4_SAVESTATE_TRY === size=%lu\n",
               (unsigned long)st_size);
        if (st_bytes == NULL || st_size == 0) {
            printf("=== FF4_SAVESTATE_MISSING ===\n");
        } else {
            bool ok = snes_loadState(ff4_snes, st_bytes, (int)st_size);
            printf("=== FF4_SAVESTATE_%s === pc=%02X:%04X frames=%lu\n",
                   ok ? "OK" : "FAIL",
                   ff4_snes->cpu->k, ff4_snes->cpu->pc,
                   (unsigned long)ff4_snes->frames);

            printf("=== FF4_WRAM_AT_LOAD === nmi$0200:");
            for (int i = 0; i < 16; i++) printf(" %02X", ff4_snes->ram[0x0200 + i]);
            printf("\n");

#ifdef FF4_PC_FIXUP
            /* Force PC to a known-good entry point. The savestate captures
             * pc=$00:3302 which falls in the PPU/APU register zone ($2000-
             * $3FFF) and is not valid executable code on G&W. Jump to
             * FieldMain @ $00:80A0 (which enables NMI, waits a frame, and
             * enters the main game loop). WRAM/VRAM from the savestate are
             * preserved, but field state ($79/$7A/$7B counters) gets reset
             * by FieldMain's prologue. */
            ff4_snes->cpu->k = 0x00;
            ff4_snes->cpu->pc = 0x80A0;
            ff4_snes->cpu->e = false;  /* native mode */
            ff4_snes->cpu->i = true;   /* IRQs masked at entry (cli later) */
            ff4_snes->cpu->mf = true;  /* A 8-bit */
            ff4_snes->cpu->xf = true;  /* X/Y 8-bit */
            ff4_snes->cpu->dp = 0x0000;
            ff4_snes->cpu->db = 0x00;
            ff4_snes->cpu->sp = 0x01FF;
            printf("=== FF4_PC_FIXUP === pc forced to 00:80A0 (FieldMain)\n");
#endif

#ifdef FF4_APU_ECHO
            /* Post-load APU mailbox unstuck: the saved state captures the
             * SPC handshake mid-conversation (FF4 audio engine polling).
             * On G&W the SPC700 is stubbed (InitSound_ext/ExecSound_ext
             * no-ops), so outPorts stay at zero forever and the CPU spins
             * waiting for an echo. Mirror inPorts → outPorts at load time
             * so any "wait until $2140 == 0xXX" loop sees its echo. */
            printf("=== FF4_APU_FIXUP === in=%02X %02X %02X %02X "
                   "out_before=%02X %02X %02X %02X\n",
                   ff4_snes->apu->inPorts[0], ff4_snes->apu->inPorts[1],
                   ff4_snes->apu->inPorts[2], ff4_snes->apu->inPorts[3],
                   ff4_snes->apu->outPorts[0], ff4_snes->apu->outPorts[1],
                   ff4_snes->apu->outPorts[2], ff4_snes->apu->outPorts[3]);
            for (int i = 0; i < 4; i++) {
                ff4_snes->apu->outPorts[i] = ff4_snes->apu->inPorts[i];
            }
#endif
        }
    }
#endif

    odroid_gamepad_state_t joystick = {0};
    int frame = 0;
    uint32_t t_start = HAL_GetTick();

#ifndef FF4_AUTO_WALK
    /* Adaptive render skip (user-approved 2026-07-13). The render wall is
     * ~26 ms/frame under continuous scroll while a render-skipped emulated
     * frame costs ~7-8 ms: skipping the RENDER of at most every other
     * frame when the 60 Hz pacer falls behind keeps the GAME clock at an
     * exact 60 Hz everywhere -- display refresh floats between 60 (light
     * scenes, no skips) and ~30 (heavy scroll, alternate skips). Guards:
     * never two consecutive skips (30 fps display floor), and a periodic
     * two-rendered-frames rephase so the skip pattern cannot lock onto
     * one parity (SNES 2-frame flicker effects alias at 30 Hz otherwise).
     * Build with -DFF4_ADAPTIVE_SKIP=0 to disable. */
#ifndef FF4_ADAPTIVE_SKIP
#define FF4_ADAPTIVE_SKIP 1
#endif
#define FF4_ADASKIP_BEHIND_MS 3
    uint8_t adaskip_this = 0, adaskip_prev = 0;
    uint32_t adaskip_run = 0;   /* consecutive skips since last rephase */
#endif

    /* Launcher-requested resume (autoboot passes load_state=1): restore
     * the pause-menu savestate slot. A missing file is a clean no-op
     * (ff4_system_LoadState returns false) and the game starts fresh. */
    if (load_state) odroid_system_emu_load_state(save_slot);

    /* Start the SAI DMA loop with a length matching one frame worth of
     * mono samples. The DMA half-buffer callbacks toggle dma_state and
     * audio_get_active_buffer() returns the half we may safely write. */
    memset(ff4_audio_stereo_scratch, 0, sizeof(ff4_audio_stereo_scratch));
    audio_start_playing(FF4_AUDIO_HALF_SAMPLES);

    while (true) {
        wdog_refresh();

        odroid_input_read_gamepad(&joystick);

        /* Retro-go pause menu on PAUSE/SET: brightness, volume, save /
         * load state, quit, plus the FF4 entries below. Replaces the old
         * SELECT+START exit combo (TIME and GAME now carry SNES buttons).
         * PAUSE/SET quick combos: A=save, B=load, arrows=volume/bright. */
        {
            odroid_dialog_choice_t options[] = {
                {310, "Frameskip", ff4_frameskip_value, 1, &ff4_frameskip_cb},
                {311, "A/B buttons", ff4_swap_ab_value, 1, &ff4_swap_ab_cb},
                ODROID_DIALOG_CHOICE_LAST
            };
            void _repaint(void) {
                ff4_blit_to_lcd((uint16_t *)lcd_get_active_buffer());
                common_ingame_overlay();
            }
            common_emu_input_loop(&joystick, options, &_repaint);
        }

        ff4_pump_buttons(&joystick);

#ifdef FF4_AUTOBOOT
        /* D3: sample PC bank once per host frame (cheap: 1 load + 1 incr) */
        if (ff4_snes != NULL && ff4_snes->cpu != NULL) {
            uint8_t bank = ff4_snes->cpu->k;
            g_diag_pc_bank_hist[bank]++;
            if (++g_diag_pc_sample_count >= 250) {
                uint32_t other = g_diag_pc_sample_count
                    - g_diag_pc_bank_hist[0x00] - g_diag_pc_bank_hist[0x01]
                    - g_diag_pc_bank_hist[0x04] - g_diag_pc_bank_hist[0x7E];
                printf("=== FF4_DIAG_PCHIST_2026_06_13 === host=%lu "
                       "$00=%lu $01=%lu $04=%lu $7E=%lu other=%lu last_pc=%02X:%04X\n",
                       (unsigned long)g_diag_host_frame,
                       (unsigned long)g_diag_pc_bank_hist[0x00],
                       (unsigned long)g_diag_pc_bank_hist[0x01],
                       (unsigned long)g_diag_pc_bank_hist[0x04],
                       (unsigned long)g_diag_pc_bank_hist[0x7E],
                       (unsigned long)other,
                       bank, ff4_snes->cpu->pc);
                memset(g_diag_pc_bank_hist, 0, sizeof g_diag_pc_bank_hist);
                g_diag_pc_sample_count = 0;
            }
        }

        /* DX: input diagnostic every 50 frames — localize the "buttons
         * never work" issue. Shows whether autoJoyRead is on (CPU asked
         * for auto-joypad), the raw button state ff4_pump_buttons wrote,
         * the resulting word in portAutoRead[0] that the CPU reads at
         * $4218/$4219, and the raw G&W joystick value for A. */
        if (ff4_snes != NULL && (g_diag_host_frame % 25) == 0) {
            extern uint32_t buttons_get(void);
            uint32_t raw = buttons_get();
            printf("=== FF4_DIAG_INPUT_2026_06_14 === host=%lu raw=%08lX "
                   "gw_A=%d gw_B=%d gw_UP=%d gw_DOWN=%d state=%04X "
                   "portA[0]=%04X autoJoy=%d\n",
                   (unsigned long)g_diag_host_frame,
                   (unsigned long)raw,
                   (int)joystick.values[ODROID_INPUT_A],
                   (int)joystick.values[ODROID_INPUT_B],
                   (int)joystick.values[ODROID_INPUT_UP],
                   (int)joystick.values[ODROID_INPUT_DOWN],
                   (unsigned)(ff4_snes->input1 ? ff4_snes->input1->currentState : 0),
                   (unsigned)ff4_snes->portAutoRead[0],
                   (int)ff4_snes->autoJoyRead);
        }

        /* D1: PPU/NMI state every 50 frames */
        if (ff4_snes != NULL && ff4_snes->ppu != NULL
            && (g_diag_host_frame % 50) == 0) {
            printf("=== FF4_DIAG_PPU_2026_06_13 === host=%lu snes_frames=%lu nmiEn=%d "
                   "inVbl=%d forceBlank=%d bright=%u vPos=%u\n",
                   (unsigned long)g_diag_host_frame,
                   (unsigned long)ff4_snes->frames,
                   ff4_snes->nmiEnabled, ff4_snes->inVblank,
                   ff4_snes->ppu->forcedBlank, ff4_snes->ppu->brightness,
                   ff4_snes->vPos);
        }

        /* D2: APU mailbox every 50 frames (offset 25 to interleave with D1) */
        if (ff4_snes != NULL && ff4_snes->apu != NULL
            && (g_diag_host_frame % 50) == 25) {
            printf("=== FF4_DIAG_APU_2026_06_13 === host=%lu "
                   "in=%02X %02X %02X %02X out=%02X %02X %02X %02X\n",
                   (unsigned long)g_diag_host_frame,
                   ff4_snes->apu->inPorts[0],  ff4_snes->apu->inPorts[1],
                   ff4_snes->apu->inPorts[2],  ff4_snes->apu->inPorts[3],
                   ff4_snes->apu->outPorts[0], ff4_snes->apu->outPorts[1],
                   ff4_snes->apu->outPorts[2], ff4_snes->apu->outPorts[3]);
        }

        /* D5: heartbeat every 60 frames (~1 Hz). Split 64-bit cycle counter
         * into two %lx halves since nano-printf lacks %llu. */
        if (ff4_snes != NULL && ff4_snes->cpu != NULL
            && (g_diag_host_frame % 60) == 0) {
            uint64_t cyc = ff4_snes->cycles;
            printf("=== FF4_DIAG_ALIVE_2026_06_13 === host=%lu "
                   "snes_cyc_hi=%08lx snes_cyc_lo=%08lx snes_frm=%lu pc=%02X:%04X\n",
                   (unsigned long)g_diag_host_frame,
                   (unsigned long)(cyc >> 32),
                   (unsigned long)(cyc & 0xFFFFFFFFu),
                   (unsigned long)ff4_snes->frames,
                   ff4_snes->cpu->k, ff4_snes->cpu->pc);
            printf("=== FF4_WRAM_NMI_2026_06_13 === host=%lu wram$0200:",
                   (unsigned long)g_diag_host_frame);
            for (int i = 0; i < 16; i++)
                printf(" %02X", ff4_snes->ram[0x0200 + i]);
            printf("\n");
        }

        /* D4: dispatch miss ring dump every 100 frames */
        if ((g_diag_host_frame % 100) == 0 && g_diag_host_frame > 0) {
            printf("=== FF4_DIAG_MISS_2026_06_13 === host=%lu hits=%lu misses=%lu uniq=",
                   (unsigned long)g_diag_host_frame,
                   (unsigned long)ff4_dispatch_hits,
                   (unsigned long)ff4_dispatch_misses);
            for (int i = 0; i < 8; i++) {
                printf("%06lX ", (unsigned long)g_diag_miss_ring[i]);
            }
            printf("\n");
        }

        g_diag_host_frame++;
#endif

#ifdef FF4_APU_ECHO
        /* Continuous APU mailbox echo: SPC700 is stubbed on G&W so it
         * never acknowledges. Mirror inPorts → outPorts each host frame
         * so the FF4 audio engine's "wait until $2140==X" loops see X
         * the next time they poll. */
        if (ff4_snes != NULL && ff4_snes->apu != NULL) {
            for (int i = 0; i < 4; i++) {
                ff4_snes->apu->outPorts[i] = ff4_snes->apu->inPorts[i];
            }
        }
#endif

#ifdef FF4_FORCE_NMI
        /* Hypothesis test: the post-savestate spin loop may wait on a
         * WRAM counter that only the NMI handler updates. The FF4 code
         * disabled nmiEnabled after a few frames because the SPC didn't
         * respond as expected. Re-enable NMI each frame so the handler
         * fires at VBlank and updates whatever counter the spin loop is
         * waiting on. */
        if (ff4_snes != NULL) {
            ff4_snes->nmiEnabled = true;
        }
#endif

#ifdef FF4_FORCE_DISPLAY_ON
        /* Diagnostic: force PPU display ON. If the savestate's VRAM/OAM/
         * CGRAM are still valid and the game code just hasn't gotten to
         * its fade-in yet, this will reveal whatever was supposed to be
         * on screen at savestate time. */
        if (ff4_snes != NULL && ff4_snes->ppu != NULL) {
            ff4_snes->ppu->forcedBlank = false;
            ff4_snes->ppu->brightness = 15;
        }
#endif

#ifdef FF4_AUTO_SAVESTATE_DUMP
        /* Hold A synthetically across [START, END). Must come AFTER
         * ff4_pump_buttons (which sets A=0 from the unpressed joystick)
         * and BEFORE ff4_step (which is where the SNES auto-joypad reads
         * input->currentState during its emulated VBlank). */
        if ((uint32_t)frame >= FF4_AUTO_A_PRESS_START
            && (uint32_t)frame < FF4_AUTO_A_PRESS_END) {
            ff4_set_button(1, SNES_BTN_A, true);
            if ((uint32_t)frame == FF4_AUTO_A_PRESS_START) {
                printf("=== FF4_AUTO_A_PRESS_DOWN === frame=%d\n", frame);
            }
        } else if ((uint32_t)frame == FF4_AUTO_A_PRESS_END) {
            ff4_set_button(1, SNES_BTN_A, false);
            printf("=== FF4_AUTO_A_PRESS_UP === frame=%d\n", frame);
        }
#endif

#ifdef FF4_AUTO_WALK
        /* Deterministic walking workload for field metrology: hold DPAD
         * directions in a 120-frame square from frame 60 on. Walking
         * scrolls the map every frame, which defeats the R4/R5 render
         * skips -- the idle savestate otherwise measures the skip path,
         * not real play. Same D6R blocks = same walked route, firmware
         * over firmware. */
        {
            /* FF4_AUTO_WALK_LR: hold left/right only (120 frames each).
             * The square's up/down legs stall against the corridor walls
             * of fixture 009 (no scroll, light frames), under-measuring
             * real play by ~50% -- found 2026-07-13 while chasing the
             * user's walking dip. LR scrolls on every frame. */
#ifdef FF4_AUTO_WALK_LR
            static const int walk_btn[4] = {SNES_BTN_LEFT,  SNES_BTN_RIGHT,
                                            SNES_BTN_LEFT,  SNES_BTN_RIGHT};
#else
            static const int walk_btn[4] = {SNES_BTN_RIGHT, SNES_BTN_DOWN,
                                            SNES_BTN_LEFT,  SNES_BTN_UP};
#endif
            uint32_t wf = (uint32_t)frame;
            if (wf >= 60) {
                int phase = ((wf - 60) / 120) & 3;
                int prev  = (phase + 3) & 3;
                ff4_set_button(1, walk_btn[prev], false);
                ff4_set_button(1, walk_btn[phase], true);
            }
        }
#endif
#ifdef FF4_AUTOBOOT
        uint32_t d6_t0 = HAL_GetTick();
#endif
        /* Emulate the skipped frames of this batch (no rendering; see
         * the FF4_FRAMESKIP comment at the top). Their audio is REAL
         * emulated time: produce it into the ring (the pre-ring submit
         * dropped it, which pitched the sound under frameskip).
         * Runtime-tunable from the pause menu (g_ff4_frameskip, 0=off). */
        for (int fs = 0; fs < g_ff4_frameskip; fs++) {
            ff4_ppu_render_enabled = 0;
            ff4_step();
            frame++;
            ff4_sound_produce();
            wdog_refresh();
        }
        ff4_ppu_render_enabled = 1;
#ifdef FF4_AUTOBOOT
        uint32_t d6_t1 = HAL_GetTick();
#endif
#if !defined(FF4_AUTO_WALK) && FF4_ADAPTIVE_SKIP
        ff4_ppu_render_enabled = !adaskip_this;
#endif
        ff4_step();
        frame++;

        /* One emulated frame of DSP output into the ring, then refill any
         * freed DMA half from it (see ff4_sound_produce/pump). */
        ff4_sound_produce();
        ff4_sound_pump();

#ifdef FF4_AUTO_SAVESTATE_DUMP
        if (!ff4_savestate_dump_done
            && (uint32_t)frame >= FF4_SAVESTATE_DUMP_FRAME) {
            ff4_savestate_dump_done = 1;
            ff4_dump_savestate_serial();
        }
#endif

        /* Blit the PPU frame to the active LCD buffer, then swap. */
#ifdef FF4_AUTOBOOT
        uint32_t d6_t2 = HAL_GetTick();
#endif
        /* Blit + swap EVERY frame, including render-skipped ones: the PPU
         * pixelBuffer persists the last rendered game frame, so this only
         * costs the ~0.4 ms blit -- and it purges pause-menu remnants from
         * both LCD buffers within two swaps (they flickered against game
         * frames when skipped frames left a stale buffer on screen). */
        ff4_blit_to_lcd((uint16_t *)lcd_get_active_buffer());
        lcd_swap();

#ifndef FF4_AUTO_WALK
        /* 60 Hz pacer. With the overclock the emulator outruns real time
         * (~63-65 emulated fps walking, 2026-07-12 ring), and nothing else
         * in this loop blocks: lcd_swap does not wait for vsync here.
         * Accumulate NTSC frame periods in thirds of a millisecond
         * (16.667 ms = 50/3 ms exactly) against HAL_GetTick. Metrology
         * builds (FF4_AUTO_WALK) stay unthrottled: the D6R ring wants the
         * true cost, not the pacing. */
        {
            static uint32_t pace_thirds;
            static uint32_t pace_base;
            if (pace_base == 0) { pace_base = HAL_GetTick(); pace_thirds = 0; }
            pace_thirds += 50;
            const uint32_t due = pace_base + pace_thirds / 3;
            uint32_t now = HAL_GetTick();
#if FF4_ADAPTIVE_SKIP
            /* Decide the NEXT frame's render skip from how far behind the
             * pacer we are. Rephase: after 16 skips, force two rendered
             * frames so the pattern's parity slips. */
            adaskip_prev = adaskip_this;
            adaskip_this = 0;
            if (g_ff4_skip_sel == FF4_SKIP_ADAPTIVE
                && !adaskip_prev && (int32_t)(now - due) > FF4_ADASKIP_BEHIND_MS) {
                if (adaskip_run >= 16) { adaskip_run = 0; /* rephase: render */ }
                else { adaskip_this = 1; adaskip_run++; g_adaskip_skipped++; }
            }
            if (!adaskip_this) g_adaskip_rendered++;
#endif
            if ((int32_t)(now - due) > 100) {
                /* fell way behind real time (heavy scene, savestate load,
                 * debugger halt): resynchronize instead of fast-forwarding
                 * through the accumulated debt once the scene gets light
                 * again -- the first pacer build had this comparison
                 * inverted, which made the game visibly SPEED UP after
                 * every heavy stretch. */
                pace_base = now; pace_thirds = 0;
            } else {
                while ((int32_t)(due - HAL_GetTick()) > 0) { __WFI(); }
            }
        }
#endif

#ifdef FF4_AUTOBOOT
        /* D6: real frame-budget breakdown, every ~300 emulated frames.
         * emu_ms   = the FF4_FRAMESKIP unrendered steps of each batch
         * rend_ms  = the rendered step (its emulation share included)
         * blit_ms  = ff4_blit_to_lcd + lcd_swap
         * game_fps = emulated SNES frames per wall second -- the number
         *            that decides whether the game feels right. */
        {
            static uint32_t d6_emu_ms, d6_rend_ms, d6_blit_ms;
            static uint32_t d6_frames, d6_win_start;
            uint32_t d6_t3 = HAL_GetTick();
            if (d6_win_start == 0) d6_win_start = d6_t0;
            d6_emu_ms  += d6_t1 - d6_t0;
            d6_rend_ms += d6_t2 - d6_t1;
            d6_blit_ms += d6_t3 - d6_t2;
            d6_frames  += g_ff4_frameskip + 1;
            if (d6_frames >= 300) {
                uint32_t win_ms = d6_t3 - d6_win_start;
                printf("=== FF4_DIAG_BUDGET_2026_07_08 === frames=%lu win_ms=%lu "
                       "emu_ms=%lu rend_ms=%lu blit_ms=%lu game_fps_x10=%lu\n",
                       (unsigned long)d6_frames, (unsigned long)win_ms,
                       (unsigned long)d6_emu_ms, (unsigned long)d6_rend_ms,
                       (unsigned long)d6_blit_ms,
                       (unsigned long)(win_ms ? (d6_frames * 10000UL) / win_ms : 0));
                /* D6R: deterministic per-block ring, readable in ONE gdb
                 * halt (g_d6_ring / g_d6_blocks). Block N covers emulated
                 * frames [300N, 300N+300) from the savestate boot with no
                 * input, so the same block index is the same workload on
                 * every firmware: A/B compares ring entries, immune to
                 * the +/-1.5 fps wall-clock window alignment noise. */
                if (g_d6_blocks < D6R_SLOTS) {
                    g_d6_ring[g_d6_blocks].win_ms  = win_ms;
                    g_d6_ring[g_d6_blocks].emu_ms  = d6_emu_ms;
                    g_d6_ring[g_d6_blocks].rend_ms = d6_rend_ms;
                    g_d6_ring[g_d6_blocks].blit_ms = d6_blit_ms;
                }
                g_d6_blocks++;
                d6_emu_ms = d6_rend_ms = d6_blit_ms = 0;
                d6_frames = 0;
                d6_win_start = 0;
            }
        }
#endif

        /* Liveness probe: every 5 host frames print Snes-side counters
         * so we can tell whether the interpreter is actually progressing
         * or just spinning. Remove once Phase 5.5/5.6 land real signal. */
        if ((frame % 5) == 0) {
            uint32_t snes_frames = 0;
            uint64_t snes_cycles = 0;
            ff4_get_state(&snes_frames, &snes_cycles);
            extern uint32_t ff4_dispatch_hits;
            extern uint32_t ff4_dispatch_misses;
            extern uint32_t ff4_miss_per_bank[256];
            uint32_t now = HAL_GetTick();
            printf("FF4 live: host=%d snes_frame=%lu dispatch=%lu/%lu wall_ms=%lu\n",
                   frame,
                   (unsigned long)snes_frames,
                   (unsigned long)ff4_dispatch_hits,
                   (unsigned long)(ff4_dispatch_hits + ff4_dispatch_misses),
                   (unsigned long)(now - t_start));
            /* Every 50 host frames, print the top-3 banks by miss count
             * so we know which module the boot/title sequence lives in. */
            if ((frame % 50) == 0) {
                int top[3] = {-1, -1, -1};
                uint32_t topv[3] = {0, 0, 0};
                for (int b = 0; b < 256; b++) {
                    uint32_t v = ff4_miss_per_bank[b];
                    if (v > topv[0]) { topv[2]=topv[1]; top[2]=top[1]; topv[1]=topv[0]; top[1]=top[0]; topv[0]=v; top[0]=b; }
                    else if (v > topv[1]) { topv[2]=topv[1]; top[2]=top[1]; topv[1]=v; top[1]=b; }
                    else if (v > topv[2]) { topv[2]=v; top[2]=b; }
                }
                printf("FF4 banks: ");
                for (int i = 0; i < 3; i++) {
                    if (top[i] < 0) break;
                    printf("$%02X=%lu ", top[i], (unsigned long)topv[i]);
                }
                printf("\n");
            }
        }
    }

    ff4_shutdown();
    return 0;
}
