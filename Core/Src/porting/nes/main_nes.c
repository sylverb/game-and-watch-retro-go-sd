#include <odroid_system.h>

#include <string.h>
#include <nofrendo.h>
#include <bitmap.h>
#include <nes.h>
#include <nes_input.h>
#include <nes_state.h>
#include <nes_input.h>
#include <osd.h>
#include "main.h"
#include "gw_buttons.h"
#include "gw_lcd.h"
#include "gw_linker.h"
#include "common.h"
#include "rom_manager.h"
#include "rg_i18n.h"
#include <assert.h>
#ifndef GNW_DISABLE_COMPRESSION
#include "lzma.h"
#endif
#include "appid.h"

static uint samplesPerFrame;
static uint32_t vsync_wait_ms = 0;

static int8_t save_slot_load = 0;

static bool autoload = false;


// if i counted correctly this should max be 23077
uint8_t nes_save_buffer[24000];

// TODO: Expose properly
extern int nes_state_save(uint8_t *flash_ptr, size_t size);

void nes_audio_submit(int16_t *buffer);

static bool SaveState(const char *savePathName)
{
    printf("Saving state...\n");

    nes_state_save(nes_save_buffer, sizeof(nes_save_buffer));

    FILE *file = fopen(savePathName, "wb");
    if (file == NULL) {
        return false;
    }
    size_t written = fwrite(nes_save_buffer, sizeof(nes_save_buffer), 1, file);
    fclose(file);
    if (!written) {
        return false;
    }

    return true;
}

// TODO: Expose properly
extern int nes_state_load(uint8_t* flash_ptr, size_t size);

static bool LoadState(const char *savePathName)
{
    FILE *file = fopen(savePathName, "rb");
    if (file == NULL) {
        return false;
    }

    size_t read = fread(nes_save_buffer, sizeof(nes_save_buffer), 1, file);
    fclose(file);

    if (!read) {
        return false;
    }

    nes_state_load((uint8_t *) nes_save_buffer, sizeof(nes_save_buffer));
    return true;
}

int osd_init()
{
   return 0;
}

// TODO: Move to lcd.c/h
extern LTDC_HandleTypeDef hltdc;

static rgb_t *palette = NULL;
static uint16_t palette565[256];
static uint32_t palette_spaced_565[256];


void osd_setpalette(rgb_t *pal)
{
    palette = pal;
#ifdef GW_LCD_MODE_LUT8
    uint32_t clut[256];

    for (int i = 0; i < 64; i++)
    {
        uint16_t c = (pal[i].b>>3) | ((pal[i].g>>2)<<5) | ((pal[i].r>>3)<<11);

        // The upper bits are used to indicate background and transparency.
        // They need to be indexed as well.
        clut[i]        = (pal[i].b) | (pal[i].g << 8) | (pal[i].r << 16);
        clut[i | 0x40] = (pal[i].b) | (pal[i].g << 8) | (pal[i].r << 16);
        clut[i | 0x80] = (pal[i].b) | (pal[i].g << 8) | (pal[i].r << 16);
    }

    // Update the color-LUT in the LTDC peripheral
    HAL_LTDC_ConfigCLUT(&hltdc, clut, 256, 0);
    HAL_LTDC_EnableCLUT(&hltdc, 0);

    // color 13 is "black". Makes for a nice border.
    memset(framebuffer1, 13, lcd_get_frame_size());
    memset(framebuffer2, 13, lcd_get_frame_size());

    odroid_display_force_refresh();
#else
    for (int i = 0; i < 64; i++)
    {
        uint16_t c = (pal[i].b>>3) | ((pal[i].g>>2)<<5) | ((pal[i].r>>3)<<11);

        // The upper bits are used to indicate background and transparency.
        // They need to be indexed as well.
        palette565[i]        = c;
        palette565[i | 0x40] = c;
        palette565[i | 0x80] = c;

        uint32_t sc = ((0b1111100000000000&c)<<10) | ((0b0000011111100000&c)<<5) | ((0b0000000000011111&c));
        palette_spaced_565[i] = sc;
        palette_spaced_565[i | 0x40] = sc;
        palette_spaced_565[i | 0x80] = sc;

    }

#endif
}

void osd_vsync()
{
    uint32_t t0;
    bool draw_frame = common_emu_frame_loop();

    nes_audio_submit(nes_getptr()->apu->buffer);

    nes_getptr()->drawframe = draw_frame;

    t0 = get_elapsed_time();
    // Wait until the audio buffer has been transmitted
    common_emu_sound_sync(false);

    vsync_wait_ms += get_elapsed_time_since(t0);
}

void nes_audio_submit(int16_t *buffer)
{
    // apu_process(audiobuffer_emulator, audioSamples, false); //get audio data
    if (common_emu_sound_loop_is_muted()) {
        return;
    }

    int32_t factor = common_emu_sound_get_volume();
    int16_t* sound_buffer = audio_get_active_buffer();
    uint16_t sound_buffer_length = audio_get_buffer_length();

    // Write to DMA buffer and lower the volume accordingly
    for (int i = 0; i < sound_buffer_length; i++) {
        int32_t sample = buffer[i];
        sound_buffer[i] = (sample * factor) >> 8;
    }
}

#ifdef GW_LCD_MODE_LUT8
static inline void blit_normal(bitmap_t *bmp, uint8_t *framebuffer) {
        // LCD is 320 wide, framebuffer is only 256
    const int hpad = (WIDTH - NES_SCREEN_WIDTH) / 2;

    for (int y = 0; y < bmp->height; y++) {
        uint8_t *row = bmp->line[y];
        uint32 *dest = NULL;
        if(active_framebuffer == 0) {
            dest = &framebuffer[WIDTH * y + hpad];
        } else {
            dest = &framebuffer[WIDTH * y + hpad];
        }
        memcpy(dest, row, bmp->width);
    }
}

static inline void blit_nearest(bitmap_t *bmp, uint8_t *framebuffer) {
    int w1 = bmp->width;
    int h1 = bmp->height;
    int w2 = WIDTH;
    int h2 = h1;

    // Blit: 5581 us
    // This can still be improved quite a bit by using aligned accesses.

    int ctr = 0;
    for (int y = 0; y < h2; y++) {
        uint8_t  *src_row  = bmp->line[y];
        uint8_t *dest_row = &framebuffer[y * w2];
        int x2 = 0;
        for (int x = 0; x < w1; x++) {
            uint8_t b2 = src_row[x];
            dest_row[x2++] = b2;
            if (ctr++ == 4) {
                ctr = 0;
                dest_row[x2++] = b2;
            }
        }
    }
}
#else

// No scaling
__attribute__((optimize("unroll-loops")))
static inline void blit_normal(bitmap_t *bmp, uint16_t *framebuffer) {
    const int w1 = bmp->width;
    const int w2 = 320;
    const int h2 = 240;
    const int hpad = 27;

    for (int y = 0; y < h2; y++) {
        uint8_t  *src_row  = bmp->line[y];
        uint16_t *dest_row = &framebuffer[y * w2 + hpad];
        for (int x = 0; x < w1; x++) {
            dest_row[x] = palette565[src_row[x]];
        }
    }
}

__attribute__((optimize("unroll-loops")))
static inline void blit_nearest(bitmap_t *bmp, uint16_t *framebuffer, bool full_width)
{
    int w1 = bmp->width;
    int w2 = WIDTH;
    int h2 = 240;
    int hpad;
    int scale_ctr;

    if (full_width) {
        // scale to 320
        hpad = 0;
        scale_ctr = 3;
    } else {
        // scale to 307
        hpad = (WIDTH - 307) / 2;
        scale_ctr = 4;
    }

    // 1767 us
    PROFILING_INIT(t_blit);
    PROFILING_START(t_blit);

    for (int y = 0; y < h2; y++) {
        int ctr = 0;
        uint8_t  *src_row  = bmp->line[y];
        uint16_t *dest_row = &framebuffer[y * w2 + hpad];
        int x2 = 0;
        for (int x = 0; x < w1; x++) {
            uint16_t b2 = palette565[src_row[x]];
            dest_row[x2++] = b2;
            if (ctr++ == scale_ctr) {
                ctr = 0;
                dest_row[x2++] = b2;
            }
        }
    }

    PROFILING_END(t_blit);

#ifdef PROFILING_ENABLED
    printf("Blit: %d us\n", (1000000 * PROFILING_DIFF(t_blit)) / t_blit_t0.SecondFraction);
#endif
}

#define CONV(_b0) ((0b11111000000000000000000000&_b0)>>10) | ((0b000001111110000000000&_b0)>>5) | ((0b0000000000011111&_b0));

__attribute__((optimize("unroll-loops")))
static void blit_4to5(bitmap_t *bmp, uint16_t *framebuffer) {
    int w1 = bmp->width;
    int w2 = WIDTH;
    int h2 = 240;

    // 1767 us

    for (int y = 0; y < h2; y++) {
        uint8_t  *src_row  = bmp->line[y];
        uint16_t *dest_row = &framebuffer[y * w2];
        for (int x_src = 0, x_dst=0; x_src < w1; x_src+=4, x_dst+=5) {
            uint32_t b0 = palette_spaced_565[src_row[x_src]];
            uint32_t b1 = palette_spaced_565[src_row[x_src+1]];
            uint32_t b2 = palette_spaced_565[src_row[x_src+2]];
            uint32_t b3 = palette_spaced_565[src_row[x_src+3]];

            dest_row[x_dst]   = CONV(b0);
            dest_row[x_dst+1] = CONV((b0+b0+b0+b1)>>2);
            dest_row[x_dst+2] = CONV((b1+b2)>>1);
            dest_row[x_dst+3] = CONV((b2+b2+b2+b3)>>2);
            dest_row[x_dst+4] = CONV(b3);
        }
    }
}


__attribute__((optimize("unroll-loops")))
static void blit_5to6(bitmap_t *bmp, uint16_t *framebuffer) {
    int w1_adjusted = bmp->width - 4;
    int w2 = WIDTH;
    int h2 = 240;
    const int hpad = (WIDTH - 307) / 2;

    // Blit: 2015 us

    for (int y = 0; y < h2; y++) {
        uint8_t  *src_row  = bmp->line[y];
        uint16_t *dest_row = &framebuffer[y * w2 + hpad];
        int x_src = 0;
        int x_dst = 0;
        for (; x_src < w1_adjusted; x_src+=5, x_dst+=6) {
            uint32_t b0 = palette_spaced_565[src_row[x_src]];
            uint32_t b1 = palette_spaced_565[src_row[x_src+1]];
            uint32_t b2 = palette_spaced_565[src_row[x_src+2]];
            uint32_t b3 = palette_spaced_565[src_row[x_src+3]];
            uint32_t b4 = palette_spaced_565[src_row[x_src+4]];

            dest_row[x_dst]   = CONV(b0);
            dest_row[x_dst+1] = CONV((b0+b1+b1+b1)>>2);
            dest_row[x_dst+2] = CONV((b1+b2)>>1);
            dest_row[x_dst+3] = CONV((b2+b3)>>1);
            dest_row[x_dst+4] = CONV((b3+b3+b3+b4)>>2);
            dest_row[x_dst+5] = CONV(b4);
        }
        // Last column, x_src=255
        dest_row[x_dst] = palette565[src_row[x_src]];
    }
}
#endif

static void blit(bitmap_t *bmp)
{
    uint16_t *framebuffer = lcd_get_active_buffer();
    odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();
    odroid_display_filter_t filtering = odroid_display_get_filter_mode();

    switch (scaling) {
    case ODROID_DISPLAY_SCALING_OFF:
        /* fall-through */
    case ODROID_DISPLAY_SCALING_FIT:
        // Full height, borders on the side
        blit_normal(bmp, framebuffer);
        break;
    case ODROID_DISPLAY_SCALING_FULL:
        // full height, full width
        if (filtering == ODROID_DISPLAY_FILTER_OFF) {
            blit_nearest(bmp, framebuffer, true);
        } else {
            blit_4to5(bmp, framebuffer);
        }
        break;
    case ODROID_DISPLAY_SCALING_CUSTOM:
        // full height, almost full width
        blit_5to6(bmp, framebuffer);
        break;
    default:
        printf("Unknown scaling mode %d\n", scaling);
        assert(!"Unknown scaling mode");
        break;
    }
    common_ingame_overlay();
}

void osd_blitscreen(bitmap_t *bmp)
{
    static uint32_t lastFPSTime = 0;
    static uint32_t frames = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;

    if (delta >= 1000) {
        int fps = (10000 * frames) / delta;
        printf("FPS: %d.%d, frames %ld, delta %ld ms, skipped %d\n", fps / 10, fps % 10, frames, delta, common_emu_state.skipped_frames);
        frames = 0;
        common_emu_state.skipped_frames = 0;
        vsync_wait_ms = 0;
        lastFPSTime = currentTime;
    }

    PROFILING_INIT(t_blit);
    PROFILING_START(t_blit);

    // This takes less than 1ms
    blit(bmp);
    lcd_swap();

    PROFILING_END(t_blit);

#ifdef PROFILING_ENABLED
    printf("Blit: %d us\n", (1000000 * PROFILING_DIFF(t_blit)) / t_blit_t0.SecondFraction);
#endif
}

static bool palette_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
   int pal = ppu_getopt(PPU_PALETTE_RGB);
   int max = PPU_PAL_COUNT - 1;

   if (event == ODROID_DIALOG_PREV) pal = pal > 0 ? pal - 1 : max;
   if (event == ODROID_DIALOG_NEXT) pal = pal < max ? pal + 1 : 0;

   if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
      odroid_settings_Palette_set(pal);
      ppu_setopt(PPU_PALETTE_RGB, pal);
   }
   sprintf(option->value, "%10s", ppu_getpalette(pal)->name);
   return event == ODROID_DIALOG_ENTER;
}

void osd_getinput(bitmap_t *bmp)
{
    uint16 pad0 = 0;
    //char pal_name[16];

    wdog_refresh();

    odroid_gamepad_state_t joystick;
    odroid_input_read_gamepad(&joystick);
    char palette_values[16];
    snprintf(palette_values, sizeof(palette_values), "%s", curr_lang->s_Default);
    odroid_dialog_choice_t options[] = {
            {100, curr_lang->s_Palette, (char *)palette_values, 1, &palette_update_cb},
            // {101, "More...", "", 1, &advanced_settings_cb},
            ODROID_DIALOG_CHOICE_LAST
    };
    void _blit()
    {
      blit(bmp);
    }
    common_emu_input_loop(&joystick, options, &_blit);
    common_emu_input_loop_handle_turbo(&joystick);

    if ((joystick.values[ODROID_INPUT_START]) || (joystick.values[ODROID_INPUT_X])) pad0 |= INP_PAD_START;
    if ((joystick.values[ODROID_INPUT_SELECT]) || (joystick.values[ODROID_INPUT_Y])) pad0 |= INP_PAD_SELECT;
    if (joystick.values[ODROID_INPUT_UP]) pad0 |= INP_PAD_UP;
    if (joystick.values[ODROID_INPUT_DOWN]) pad0 |= INP_PAD_DOWN;
    if (joystick.values[ODROID_INPUT_LEFT]) pad0 |= INP_PAD_LEFT;
    if (joystick.values[ODROID_INPUT_RIGHT]) pad0 |= INP_PAD_RIGHT;
    if (joystick.values[ODROID_INPUT_A]) pad0 |= INP_PAD_A;
    if (joystick.values[ODROID_INPUT_B]) pad0 |= INP_PAD_B;

    // Enable to log button presses
#if 0
    static old_pad0;
    if (pad0 != old_pad0) {
        printf("pad0=%02x\n", pad0);
        old_pad0 = pad0;
    }
#endif

    input_update(INP_JOYPAD0, pad0);
}

size_t osd_getromdata(unsigned char **data)
{
    /* src pointer to the ROM data in the external flash (raw or LZ4) */
#ifndef GNW_DISABLE_COMPRESSION
    const unsigned char *src = ROM_DATA;
    unsigned char *dest = (unsigned char *)&_NES_ROM_UNPACK_BUFFER;
    uint32_t available_size = (uint32_t)&_NES_ROM_UNPACK_BUFFER_SIZE;

    wdog_refresh();
    if(strcmp(ROM_EXT, "lzma") == 0){
        size_t n_decomp_bytes;
        n_decomp_bytes = lzma_inflate(dest, available_size, src, ROM_DATA_LENGTH);
        *data = dest;
        return n_decomp_bytes;
    }
    else
#endif
    {
        *data = (unsigned char *)ROM_DATA;

        return ROM_DATA_LENGTH;
    }
}

uint osd_getromcrc()
{
   return 0x1337;
}

void osd_loadstate()
{
    if(autoload) {
        autoload = false;
        odroid_system_emu_load_state(save_slot_load);
    }
}


int app_main_nes(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    region_t nes_region;

    save_slot_load = save_slot;

    odroid_system_init(APPID_NES, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, NULL, NULL, NULL, NULL, NULL);

    if (start_paused) {
        common_emu_state.pause_after_frames = 4;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }

    autoload = load_state;
    if (!load_state) {
        lcd_clear_buffers();
    }

    printf("Nofrendo start!\n");

    if (ACTIVE_FILE->region == REGION_PAL) {
        lcd_set_refresh_rate(50);
        nes_region = NES_PAL;
        common_emu_state.frame_time_10us = (uint16_t)(100000 / 50 + 0.5f);
        samplesPerFrame = (AUDIO_SAMPLE_RATE) / 50;
    } else {
        lcd_set_refresh_rate(60);
        nes_region = NES_NTSC;
        common_emu_state.frame_time_10us = (uint16_t)(100000 / 60 + 0.5f);
        //printf("frame_time_10us: %d\n", common_emu_state.frame_time_10us);
        samplesPerFrame = (AUDIO_SAMPLE_RATE) / 60;
    }

    audio_start_playing(samplesPerFrame);

    int cheat_count = 0;
    const char **active_cheat_codes = NULL;
#if CHEAT_CODES == 1
    for(int i=0; i<MAX_CHEAT_CODES && i<ACTIVE_FILE->cheat_count; i++) {
        if (odroid_settings_ActiveGameGenieCodes_is_enabled(ACTIVE_FILE->path, i)) {
            cheat_count++;
        }
    }

    active_cheat_codes = rg_alloc(cheat_count * sizeof(char**), MEM_ANY);
    for(int i=0, j=0; i<MAX_CHEAT_CODES && i<ACTIVE_FILE->cheat_count; i++) {
        if (odroid_settings_ActiveGameGenieCodes_is_enabled(ACTIVE_FILE->path, i)) {
            active_cheat_codes[j] = ACTIVE_FILE->cheat_codes[i];
            j++;
        }
    }
#endif

    nofrendo_start(ACTIVE_FILE->name, active_cheat_codes, cheat_count, nes_region, AUDIO_SAMPLE_RATE, false);

#if CHEAT_CODES == 1
    rg_free(active_cheat_codes); // No need to clean up the objects in the array as they're allocated in read only space
#endif

    return 0;
}
