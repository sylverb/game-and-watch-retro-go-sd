extern "C" {
#include <odroid_system.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "main.h"
#include "bilinear.h"
#include "gw_linker.h"
#include "rg_i18n.h"
#include "gw_buttons.h"
#include "common.h"
#include "rom_manager.h"
#include "appid.h"
#include "cpp_init_array.h"
#include "main_gb_tgbdual.h"
#include "heap.hpp"
#include "odroid_overlay.h"
#include "rg_storage.h"

extern void __libc_init_array(void);
}

static void gb_process_blit();

#define GB_WIDTH (160)
#define GB_HEIGHT (144)


#define VIDEO_REFRESH_RATE 60
#define GB_AUDIO_FREQUENCY 44100

// Use 60Hz for GB
#define AUDIO_BUFFER_LENGTH_GB (int)(GB_AUDIO_FREQUENCY / VIDEO_REFRESH_RATE)

#include "heap.hpp"

#include <cstdio>
#include <cstddef>
#include <cassert>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <algorithm>
#include <cmath>
#include "gb_core/gb.h"
#include "gw_renderer.h"

// GB Palettes
int index_palette = 0;
static const int kPaletteStorageDefault = 0;

static gb *g_gb = nullptr;
static gw_renderer *render = nullptr;

static uint16_t *tgb_buffer = nullptr;
bool tgb_drawFrame = false;

// --- MAIN

static bool SaveState(const char *savePathName)
{
    if (g_gb->get_rom()->has_battery()) {
        char *sram_path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
        if (sram_path) {
            const int sram_size = g_gb->get_rom()->get_sram_size();
            FILE *sram_file = fopen(sram_path, "wb");
            if (sram_file != NULL) {
                fwrite(g_gb->get_rom()->get_sram(), sram_size, 1, sram_file);
                fclose(sram_file);
            }
            free(sram_path);
        }
    }

    if (savePathName == NULL) {
        return true;
    }

    size_t size = g_gb->get_state_size();

    // We store data in the not visible framebuffer
    lcd_wait_for_vblank();
    unsigned char *data = (unsigned char *)lcd_get_active_buffer();
    g_gb->save_state_mem((void *)data);

    FILE *file = fopen(savePathName, "wb");
    if (file == NULL) {
        fclose(file);
        return false;
    }

    size_t written = fwrite(data, size, 1, file);

    fclose(file);
    
    if (!written) {
        return false;
    }

    return true;
}

static bool LoadState(const char *savePathName)
{
    if (savePathName == NULL) {
        if (!g_gb->get_rom()->has_battery()) {
            return false;
        }

        bool loaded = false;
        char *sram_path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
        if (sram_path) {
            const int sram_size = g_gb->get_rom()->get_sram_size();
            FILE *sram_file = fopen(sram_path, "rb");
            if (sram_file != NULL) {
                loaded = (fread(g_gb->get_rom()->get_sram(), sram_size, 1, sram_file) == 1);
                fclose(sram_file);
            }
            free(sram_path);
        }
        return loaded;
    }

    // We store data in the not visible framebuffer
    unsigned char *data = (unsigned char *)lcd_get_active_buffer();
    size_t size = g_gb->get_state_size();

    FILE *file = fopen(savePathName, "rb");
    if (file == NULL) {
        return false;
    }

    size_t read = fread(data, size, 1, file);

    fclose(file);

    if (!read) {
        return false;
    }

    if (strcmp((const char *)&(data[34]),g_gb->get_rom()->get_info()->cart_name) == 0)
        g_gb->restore_state_mem((void *)data);

    lcd_clear_active_buffer();

    return true;
}

static void *Screenshot()
{
    lcd_wait_for_vblank();

    lcd_clear_active_buffer();
    gb_process_blit();
    return lcd_get_active_buffer();
}


void gb_pcm_submit(int16_t *stream, int samples) {
    if (common_emu_sound_loop_is_muted()) {
        return;
    }

    int32_t factor = common_emu_sound_get_volume();
    int16_t* sound_buffer = audio_get_active_buffer();
    uint16_t sound_buffer_length = audio_get_buffer_length();
    
    for (int i = 0; i < sound_buffer_length; i++) {
        int32_t sample = ((int32_t)stream[i*2]+(int32_t)stream[i*2+1])/2;
        sound_buffer[i] = (sample * factor) >> 8;
    }
}

__attribute__((optimize("unroll-loops")))
static inline void screen_blit_nn(uint16_t *buffer, int32_t dest_width, int32_t dest_height)
{
    static uint32_t lastFPSTime = 0;
    static uint32_t frames = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;

    if (delta >= 1000) {
//        int fps = (10000 * frames) / delta;
//        printf("FPS: %d.%d, frames %ld, delta %ld ms, skipped %d\n", fps / 10, fps % 10, delta, frames, common_emu_state.skipped_frames);
        frames = 0;
        common_emu_state.skipped_frames = 0;
        lastFPSTime = currentTime;
    }

    int w1 = GB_WIDTH;
    int h1 = GB_HEIGHT;
    int w2 = dest_width;
    int h2 = dest_height;

    int x_ratio = (int)((w1<<16)/w2) +1;
    int y_ratio = (int)((h1<<16)/h2) +1;
    int hpad = (320 - dest_width) / 2;
    int wpad = (240 - dest_height) / 2;

    int x2;
    int y2;

    uint16_t* screen_buf = buffer;
    uint16_t *dest = (uint16_t *)lcd_get_active_buffer();

    for (int i=0;i<h2;i++) {
        for (int j=0;j<w2;j++) {
            x2 = ((j*x_ratio)>>16) ;
            y2 = ((i*y_ratio)>>16) ;
            uint16_t b2 = screen_buf[(y2*w1)+x2];
            dest[((i+wpad)*WIDTH)+j+hpad] = b2;
        }
    }
}

static void screen_blit_bilinear(uint16_t *buffer, int32_t dest_width)
{
    static uint32_t lastFPSTime = 0;
    static uint32_t frames = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;

    if (delta >= 1000) {
        frames = 0;
        common_emu_state.skipped_frames = 0;
        lastFPSTime = currentTime;
    }

    int w1 = GB_WIDTH;
    int h1 = GB_HEIGHT;

    int w2 = dest_width;
    int h2 = 240;
    int stride = 320;
    int hpad = (320 - dest_width) / 2;

    uint16_t *dest = (uint16_t *)lcd_get_active_buffer();

    image_t dst_img;
    dst_img.w = dest_width;
    dst_img.h = 240;
    dst_img.bpp = 2;
    dst_img.pixels = ((uint8_t *) dest) + hpad * 2;

    if (hpad > 0) {
        memset(dest, 0x00, hpad * 2);
    }

    image_t src_img;
    src_img.w = GB_WIDTH;
    src_img.h = GB_HEIGHT;
    src_img.bpp = 2;
    src_img.pixels = (uint8_t *)buffer;

    float x_scale = ((float) w2) / ((float) w1);
    float y_scale = ((float) h2) / ((float) h1);

    imlib_draw_image(&dst_img, &src_img, 0, 0, stride, x_scale, y_scale, NULL, -1, 255, NULL,
                     NULL, IMAGE_HINT_BILINEAR, NULL, NULL);
}

static inline void screen_blit_v3to5(uint16_t *buffer) {
    static uint32_t lastFPSTime = 0;
    static uint32_t frames = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;

    if (delta >= 1000) {
        frames = 0;
        common_emu_state.skipped_frames = 0;
        lastFPSTime = currentTime;
    }

    uint16_t *dest = (uint16_t *)lcd_get_active_buffer();

#define CONV(_b0)    (((0b11111000000000000000000000&_b0)>>10) | ((0b000001111110000000000&_b0)>>5) | ((0b0000000000011111&_b0)))
#define EXPAND(_b0)  (((0b1111100000000000 & _b0) << 10) | ((0b0000011111100000 & _b0) << 5) | ((0b0000000000011111 & _b0)))

    int y_src = 0;
    int y_dst = 0;
    int w = GB_WIDTH;
    int h = GB_HEIGHT;
    for (; y_src < h; y_src += 3, y_dst += 5) {
        int x_src = 0;
        int x_dst = 0;
        for (; x_src < w; x_src += 1, x_dst += 2) {
            uint16_t *src_col = &((uint16_t *)buffer)[(y_src * w) + x_src];
            uint32_t b0 = EXPAND(src_col[w * 0]);
            uint32_t b1 = EXPAND(src_col[w * 1]);
            uint32_t b2 = EXPAND(src_col[w * 2]);

            dest[((y_dst + 0) * WIDTH) + x_dst] = CONV(b0);
            dest[((y_dst + 1) * WIDTH) + x_dst] = CONV((b0+b1)>>1);
            dest[((y_dst + 2) * WIDTH) + x_dst] = CONV(b1);
            dest[((y_dst + 3) * WIDTH) + x_dst] = CONV((b1+b2)>>1);
            dest[((y_dst + 4) * WIDTH) + x_dst] = CONV(b2);

            dest[((y_dst + 0) * WIDTH) + x_dst + 1] = CONV(b0);
            dest[((y_dst + 1) * WIDTH) + x_dst + 1] = CONV((b0+b1)>>1);
            dest[((y_dst + 2) * WIDTH) + x_dst + 1] = CONV(b1);
            dest[((y_dst + 3) * WIDTH) + x_dst + 1] = CONV((b1+b2)>>1);
            dest[((y_dst + 4) * WIDTH) + x_dst + 1] = CONV(b2);
        }
    }
}

static inline void screen_blit_jth(uint16_t *buffer) {
    static uint32_t lastFPSTime = 0;
    static uint32_t frames = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;

    if (delta >= 1000) {
        frames = 0;
        common_emu_state.skipped_frames = 0;
        lastFPSTime = currentTime;
    }


    uint16_t* screen_buf = (uint16_t*)buffer;
    uint16_t *dest = (uint16_t *)lcd_get_active_buffer();

    int w1 = GB_WIDTH;
    int h1 = GB_HEIGHT;
    int w2 = 320;
    int h2 = 240;

    const int border = 24;

    // Iterate on dest buf rows
    for(int y = 0; y < border; ++y) {
        uint16_t *src_row  = &screen_buf[y * w1];
        uint16_t *dest_row = &dest[y * w2];
        for (int x = 0, xsrc=0; x < w2; x+=2,xsrc++) {
            dest_row[x]     = src_row[xsrc];
            dest_row[x + 1] = src_row[xsrc];
        }
    }

    for (int y = border, src_y = border; y < h2-border; y+=2, src_y++) {
        uint16_t *src_row  = &screen_buf[src_y * w1];
        uint32_t *dest_row0 = (uint32_t *) &dest[y * w2];
        for (int x = 0, xsrc=0; x < w2; x++,xsrc++) {
            uint32_t col = src_row[xsrc];
            dest_row0[x] = (col | (col << 16));
        }
    }

    for (int y = border, src_y = border; y < h2-border; y+=2, src_y++) {
        uint16_t *src_row  = &screen_buf[src_y * w1];
        uint32_t *dest_row1 = (uint32_t *)&dest[(y + 1) * w2];
        for (int x = 0, xsrc=0; x < w2; x++,xsrc++) {
            uint32_t col = src_row[xsrc];
            dest_row1[x] = (col | (col << 16));
        }
    }

    for(int y = 0; y < border; ++y) {
        uint16_t *src_row  = &screen_buf[(h1-border+y) * w1];
        uint16_t *dest_row = &dest[(h2-border+y) * w2];
        for (int x = 0, xsrc=0; x < w2; x+=2,xsrc++) {
            dest_row[x]     = src_row[xsrc];
            dest_row[x + 1] = src_row[xsrc];
        }
    }
}

static void gb_process_blit()
{
    odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();
    odroid_display_filter_t filtering = odroid_display_get_filter_mode();

    switch (scaling) {
    case ODROID_DISPLAY_SCALING_OFF:
        // Original Resolution
        screen_blit_nn(tgb_buffer, 160, 144);
        break;
    case ODROID_DISPLAY_SCALING_FIT:
        // Full height, borders on the side
        switch (filtering) {
        case ODROID_DISPLAY_FILTER_OFF:
            /* fall-through */
        case ODROID_DISPLAY_FILTER_SHARP:
            // crisp nearest neighbor scaling
            screen_blit_nn(tgb_buffer, 266, 240);
            break;
        case ODROID_DISPLAY_FILTER_SOFT:
            // soft bilinear scaling
            screen_blit_bilinear(tgb_buffer, 266);
            break;
        default:
            printf("Unknown filtering mode %d\n", filtering);
            assert(!"Unknown filtering mode");
        }
        break;
        break;
    case ODROID_DISPLAY_SCALING_FULL:
        // full height, full width
        switch (filtering) {
        case ODROID_DISPLAY_FILTER_OFF:
            // crisp nearest neighbor scaling
            screen_blit_nn(tgb_buffer, 320, 240);
            break;
        case ODROID_DISPLAY_FILTER_SHARP:
            // sharp bilinear-ish scaling
            screen_blit_v3to5(tgb_buffer);
            break;
        case ODROID_DISPLAY_FILTER_SOFT:
            // soft bilinear scaling
            screen_blit_bilinear(tgb_buffer, 320);
            break;
        default:
            printf("Unknown filtering mode %d\n", filtering);
            assert(!"Unknown filtering mode");
        }
        break;
    case ODROID_DISPLAY_SCALING_CUSTOM:
        // compressed top and bottom sections, full width
        screen_blit_jth(tgb_buffer);
        break;
    default:
        printf("Unknown scaling mode %d\n", scaling);
        assert(!"Unknown scaling mode");
        break;
    }
}

void gb_blit(uint16_t *buffer) {
    tgb_buffer = buffer;
    gb_process_blit();
    common_ingame_overlay();
}

static bool reset_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_ENTER) {
        g_gb->reset();
    }
    return event == ODROID_DIALOG_ENTER;
}

static bool palette_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max = g_gb->get_lcd()->get_palette_count() - 1;

    if (event == ODROID_DIALOG_PREV) {
        index_palette = index_palette > 0 ? index_palette - 1 : max;
    }

    if (event == ODROID_DIALOG_NEXT) {
        index_palette = index_palette < max ? index_palette + 1 : 0;
    }

    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
        g_gb->get_lcd()->set_palette(index_palette);
        odroid_settings_Palette_set(index_palette);
        odroid_settings_commit();
    }

    sprintf(option->value, "%d/%d", index_palette+1, max+1);

    return event == ODROID_DIALOG_ENTER;
}

#if CHEAT_CODES == 1
int charToInt(char val) {
    if (val >= '0' && val <= '9') {
        return val - '0';
    } else {
        return val - 'A' + 10;
    }
}

void apply_cheat_code(const char *cheatcode) {
    char temp[256];

    strcpy(temp, cheatcode);
    char *codepart = strtok(temp, "+,;._ ");

    while (codepart)
    {
        size_t codepart_len = strlen(codepart);
        if (codepart_len == 8) {
            // gameshark format for "ABCDEFGH",
            // AB    External RAM bank number
            // CD    New Data
            // GHEF  Memory Address (internal or external RAM, A000-DFFF)
            cheat_dat *cheat = (cheat_dat *)heap_alloc_mem(sizeof(cheat_dat));
            cheat->code = ((charToInt(*codepart))<<4) + charToInt(*(codepart+1));
            cheat->dat = (charToInt(*(codepart+2))<<4) + charToInt(*(codepart+3));
            cheat->adr = (charToInt(*(codepart+6))<<12) + (charToInt(*(codepart+7))<<8) +
                         (charToInt(*(codepart+4))<<4) + charToInt(*(codepart+5));
            g_gb->get_cheat()->add_cheat(cheat);
        } else if (codepart_len == 9) {
            // game genie format: for "ABCDEFGHI",
            // AB   = New data
            // FCDE = Memory address, XORed by 0F000h
            // GIH  = Check data (can be ignored for our purposes)
            cheat_dat *cheat = (cheat_dat *)heap_alloc_mem(sizeof(cheat_dat));
            word scramble;
            cheat->code = 1;
            cheat->dat = ((charToInt(*codepart))<<4) + charToInt(*(codepart+1));
            scramble   = (charToInt(*(codepart+2))<<12) + (charToInt(*(codepart+3))<<8) +
                         (charToInt(*(codepart+4))<<4) + charToInt(*(codepart+5));
            cheat->adr = (((scramble&0xF) << 12) ^ 0xF000) | (scramble >> 4);
            g_gb->get_cheat()->add_cheat(cheat);
        } else if (codepart_len == 11) {
            // game genie format: for "ABC-DEF-GHI",
            // AB   = New data
            // FCDE = Memory address, XORed by 0F000h
            // GIH  = Check data (can be ignored for our purposes)
            cheat_dat *cheat = (cheat_dat *)heap_alloc_mem(sizeof(cheat_dat));
            word scramble;
            cheat->code = 1;
            cheat->dat = ((charToInt(*codepart))<<4) + charToInt(*(codepart+1));
            scramble   = (charToInt(*(codepart+2))<<12) + (charToInt(*(codepart+4))<<8) +
                         (charToInt(*(codepart+5))<<4) + charToInt(*(codepart+6));
            cheat->adr = (((scramble&0xF) << 12) ^ 0xF000) | (scramble >> 4);
            g_gb->get_cheat()->add_cheat(cheat);
        }
        codepart = strtok(NULL,"+,;._ ");
    }
}

extern "C" void update_cheats_gb() {
    g_gb->get_cheat()->clear();
    for(int i=0; i<MAX_CHEAT_CODES && i<ACTIVE_FILE->cheat_count; i++) {
        if (odroid_settings_ActiveGameGenieCodes_is_enabled(ACTIVE_FILE->path, i)) {
            apply_cheat_code(ACTIVE_FILE->cheat_codes[i]);
        }
    }
}
#endif

void app_main_gb_tgbdual_cpp(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    printf("app_main_gb_tgbdual_cpp\n");
    char palette_values[16];
    odroid_gamepad_state_t joystick;

    if (start_paused) {
        common_emu_state.pause_after_frames = 3;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }
    common_emu_state.frame_time_10us = (uint16_t)(100000 / VIDEO_REFRESH_RATE + 0.5f);

    odroid_system_init(APPID_GB, GB_AUDIO_FREQUENCY);
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, NULL, NULL);

#if SD_CARD == 0
    // To optimize free memory for bank caching, we make sure that maximum
    // data will be set in itc ram. If RAM size info tell that we need
    // to allocate 32KB of GB extended ram, we disable itc allocation to
    // keep the 64KB of itc ram free so we can fully allocate these 64KB
    // by using them for the 32KB Gameboy RAM and the game extended ram
    if (ROM_DATA[0x149] != 3) // Ram size info
        heap_itc_alloc(true);
#elif SD_CARD == 1
    uint8_t ram_size = 0;
    uint8_t is_gbcolor = 0;
    FILE *file = fopen(ACTIVE_FILE->path,"rb");
    if (file) {
        fseek(file, 0x149, SEEK_SET);
        fread(&ram_size, 1, 1, file);
        fseek(file, 0x143, SEEK_SET);
        fread(&is_gbcolor, 1, 1, file);
        is_gbcolor = !((is_gbcolor & 0x80) == 0);
        fclose(file);
    }
#endif

    render = new gw_renderer(0);
    g_gb   = new gb(render, true, true);

// TODO : load rom from sd card (in ram or in flash)
#if SD_CARD == 1
    // We have to determine if the sram will be allocated in itc ram or in standard ram
    // to know how much standard ram is really available
	static const int tbl_sram[]={1,1,1,4,16,8};//0と1は保険
    size_t reserved_size = 0;
    if (is_gbcolor) {
        if (ram_size >= 3)
            reserved_size = 0x2000*tbl_sram[ram_size];
    } else {
        if (ram_size >= 4)
            reserved_size = 0x2000*tbl_sram[ram_size];
    }

    byte *data = NULL;
    uint32_t size = 0;
    size = ACTIVE_FILE->size;
    printf("is_gbcolor: %d ram_size: %d\n", is_gbcolor, ram_size);
    printf("free mem: %d reserved_size: %d\n", heap_free_mem(), reserved_size);
    if (size > (heap_free_mem() - reserved_size)) {
        data = (byte *)odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &size, false);
    } else {
        data = (byte *)heap_alloc_mem(size);
        if (data != NULL) {
            odroid_overlay_cache_file_in_ram(ACTIVE_FILE->path, (uint8_t *)data);
        }
    }
    if (!g_gb->load_rom(data, size, NULL, 0, true))
        return;
#else
    if (!g_gb->load_rom((byte *)ROM_DATA, ROM_DATA_LENGTH, NULL, 0, true))
        return;
#endif

    if (load_state) {
        odroid_system_emu_load_state(save_slot);
    } else {
		// Load cartridge SRAM by default so battery saves are available
        // even when starting without savestates.
        LoadState(NULL);
        lcd_clear_buffers();
    }

    if (g_gb->get_rom()->get_info()->gb_type != 3) {
        int max_palette = g_gb->get_lcd()->get_palette_count() - 1;
        int stored_palette = odroid_settings_Palette_get();
        if (stored_palette < 0 || stored_palette > max_palette) {
            stored_palette = kPaletteStorageDefault;
        }
        index_palette = stored_palette;
        g_gb->get_lcd()->set_palette(index_palette);
    } else {
        index_palette = g_gb->get_lcd()->get_current_palette();
    }

#if CHEAT_CODES == 1
    for(int i=0; i<MAX_CHEAT_CODES && i<ACTIVE_FILE->cheat_count; i++) {
        if (odroid_settings_ActiveGameGenieCodes_is_enabled(ACTIVE_FILE->path, i)) {
            apply_cheat_code(ACTIVE_FILE->cheat_codes[i]);
        }
    }
#endif

    odroid_dialog_choice_t options[] = {
        {300, curr_lang->s_Palette, (char *)palette_values, (g_gb->get_rom()->get_info()->gb_type!=3)?1:-1, &palette_update_cb},
        {300, curr_lang->s_Reset, NULL, 1, &reset_cb},
        ODROID_DIALOG_CHOICE_LAST
    };

    audio_start_playing(AUDIO_BUFFER_LENGTH_GB);

    while (true)
    {
        wdog_refresh();

        tgb_drawFrame = common_emu_frame_loop();

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &gb_process_blit);
        common_emu_input_loop_handle_turbo(&joystick);

        for (int line = 0;line < 154; line++) {
            g_gb->run();
        }

        common_emu_sound_sync(false);
    }
}

extern "C" void app_main_gb_tgbdual(uint8_t load_state, uint8_t start_paused, uint8_t save_slot)
 {
 	// Call static c++ constructors now, *after* OSPI and other memory is copied
    // Do not use __libc_init_array() as it will not work with the overlay
    cpp_init_array(__init_array_tgb_start__, __init_array_tgb_end__);

 	app_main_gb_tgbdual_cpp(load_state, start_paused,save_slot);
 }
