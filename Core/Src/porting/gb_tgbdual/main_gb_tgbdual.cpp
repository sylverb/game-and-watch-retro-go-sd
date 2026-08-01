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
#include "odroid_settings.h"
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
#include "gb_core/tgbdual_sgb.h"
#include "gw_renderer.h"

// GB Palettes
int index_palette = 0;
static int gb_console_mode = GB_CONSOLE_DMG;
static char system_values[16];
static uint8_t rom_cgb_flag = 0;
static bool rom_sgb_compatible = false;
static bool sgb_border_enabled = true;

static gb *g_gb = nullptr;
static gw_renderer *render = nullptr;

static uint16_t *tgb_buffer = nullptr;
bool tgb_drawFrame = false;

// --- MAIN

typedef struct __attribute__((packed)) {
    char magic[4];          /* "GBST" */
    uint16_t version;       /* 1 = v1 with header; no header => v0 legacy */
    uint16_t flags;         /* reserved */
    uint32_t payload_size;  /* bytes after header */
    uint32_t saved_gb_type; /* gb_type at save time */
    char cart_name[18];
} gb_savestate_header_v1_t;

static const size_t gb_v0_rom_info_off = sizeof(gb_regs) + sizeof(gbc_regs);

/* v0 payload starts with gb_regs (34 bytes) — rom_info is 2-byte misaligned,
 * so never cast to rom_info* (gb_type at +70 faults on strict-align CPUs). */
static bool gb_v0_cart_name_matches(const unsigned char *payload, const char *expected)
{
    return strcmp((const char *)&payload[gb_v0_rom_info_off], expected) == 0;
}

static int gb_v0_read_gb_type(const unsigned char *payload)
{
    int gb_type = 1;
    memcpy(&gb_type, &payload[gb_v0_rom_info_off + offsetof(rom_info, gb_type)],
           sizeof(gb_type));
    return gb_type;
}

static size_t gb_max_payload_size(gb *core)
{
    size_t a = core->get_state_size_for_type(GB_SAVESTATE_V0, 1);
    size_t b = core->get_state_size_for_type(GB_SAVESTATE_V0, 3);
    size_t c = core->get_state_size_for_type(GB_SAVESTATE_V1, 1);
    size_t d = core->get_state_size_for_type(GB_SAVESTATE_V1, 2);
    size_t e = core->get_state_size_for_type(GB_SAVESTATE_V1, 3);
    size_t m = a > b ? a : b;
    if (c > m) m = c;
    if (d > m) m = d;
    if (e > m) m = e;
    return m;
}

static bool SaveState(const char *savePathName)
{
    /* Scratch in the non-displayed buffer (active, after swap has settled).
     * Must restore it afterwards: sleep wake's lcd_init() always points LTDC
     * at fb1 first — if that buffer still holds savestate bytes, the top of
     * the screen flashes garbage until the pause banner / next frame redraws
     * (seen when sleeping from the options menu or the blinking Pause screen). */
    lcd_wait_for_vblank();
    lcd_sleep_while_swap_pending();
    unsigned char *data = (unsigned char *)lcd_get_active_buffer();
    g_gb->save_state_mem((void *)data);

    gb_savestate_header_v1_t hdr;
    memcpy(hdr.magic, "GBST", 4);
    hdr.version = 1;
    hdr.flags = 0;
    hdr.payload_size = (uint32_t)g_gb->get_state_size(GB_SAVESTATE_V1);
    hdr.saved_gb_type = (uint32_t)g_gb->get_rom()->get_info()->gb_type;
    memcpy(hdr.cart_name, g_gb->get_rom()->get_info()->cart_name, sizeof(hdr.cart_name));

    FILE *file = fopen(savePathName, "wb");
    if (file == NULL)
        return false;

    size_t w1 = fwrite(&hdr, sizeof(hdr), 1, file);
    size_t w2 = fwrite(data, hdr.payload_size, 1, file);

    fclose(file);

    /* Put the last displayed frame back into the scratch buffer. */
    lcd_clone();
    return w1 == 1 && w2 == 1;
}

static bool LoadState(const char *savePathName)
{
    unsigned char *data = (unsigned char *)lcd_get_active_buffer();
    FILE *file = fopen(savePathName, "rb");
    if (file == NULL)
        return false;

    gb_savestate_header_v1_t hdr;
    size_t rh = fread(&hdr, 1, sizeof(hdr), file);

    /* v1: header present. */
    if (rh == sizeof(hdr) && memcmp(hdr.magic, "GBST", 4) == 0 && hdr.version == 1) {
        if (memcmp(hdr.cart_name, g_gb->get_rom()->get_info()->cart_name, sizeof(hdr.cart_name)) != 0) {
            fclose(file);
            return false;
        }

        /* Size depends on saved gb_type (WRAM/VRAM + optional SGB blob). */
        size_t expected = g_gb->get_state_size_for_type(GB_SAVESTATE_V1, (int)hdr.saved_gb_type);
        if (hdr.payload_size != expected) {
            fclose(file);
            return false;
        }

        size_t n = fread(data, 1, hdr.payload_size, file);
        fclose(file);
        if (n != hdr.payload_size)
            return false;

        if (!g_gb->restore_state_mem((void *)data, GB_SAVESTATE_V1))
            return false;

        if (memcmp(hdr.cart_name, g_gb->get_rom()->get_info()->cart_name, sizeof(hdr.cart_name)) != 0)
            return false;

        gb_console_mode = g_gb->get_console_mode();
        lcd_clear_active_buffer();
        return true;
    }

    /* v0 legacy: raw payload, no header.
     * Layout matches tgbdual-go before TAMA5/SGB/console_mode features. */
    fseek(file, 0, SEEK_SET);
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0 || (size_t)file_size < gb_v0_rom_info_off + sizeof(rom_info)) {
        fclose(file);
        return false;
    }

    size_t max_size = gb_max_payload_size(g_gb);
    if ((size_t)file_size > max_size) {
        fclose(file);
        return false;
    }

    size_t n = fread(data, 1, (size_t)file_size, file);
    fclose(file);
    if (n != (size_t)file_size)
        return false;

    const char *expected_cart = g_gb->get_rom()->get_info()->cart_name;

    if (!gb_v0_cart_name_matches(data, expected_cart))
        return false;

    int saved_gb_type = gb_v0_read_gb_type(data);
    size_t expected = g_gb->get_state_size_for_type(GB_SAVESTATE_V0, saved_gb_type);
    if ((size_t)file_size != expected) {
        printf("GB savestate v0 size mismatch: file=%ld expected=%u (gb_type=%d)\n",
               file_size, (unsigned)expected, saved_gb_type);
        return false;
    }

    g_gb->restore_state_mem((void *)data, GB_SAVESTATE_V0);

    gb_console_mode = g_gb->get_console_mode();
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

static void SaveSram() {
    if (g_gb->get_rom()->has_battery()) {
        char *sram_path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
        const int sram_size = g_gb->get_rom()->get_sram_size();
        FILE *sram_file = fopen(sram_path, "wb");
        if (sram_file != NULL) {
            fwrite(g_gb->get_rom()->get_sram(), sram_size, 1, sram_file);
            fclose(sram_file);
        }
        free(sram_path);
    }
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

    /* SGB mode: always use the 256×224 layout once enabled so we don't
     * flicker between scaled 160×144 and bordered output at boot / TRNs. */
    if (sgb_border_enabled &&
        g_gb && g_gb->get_sgb() && g_gb->get_sgb()->enabled() && tgb_buffer) {
        uint16_t *lcd = (uint16_t *)lcd_get_active_buffer();
        g_gb->get_sgb()->blit_frame(lcd, 320, 240,
                                    tgb_buffer, GB_WIDTH, GB_HEIGHT);
        return;
    }

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
        /* Keep the UI console selection (GB/SGB/GBC) across soft reset. */
        g_gb->set_console_mode(gb_console_mode);
        g_gb->reset();
        /* Custom DMG palettes only in pure GB mode — never overwrite SGB colors. */
        if (g_gb->get_rom()->get_info()->gb_type == 1)
            g_gb->get_lcd()->set_palette(index_palette);
    }
    return event == ODROID_DIALOG_ENTER;
}

static void gb_console_label(int mode, char *out, size_t out_len)
{
    switch (mode) {
    case GB_CONSOLE_CGB: snprintf(out, out_len, "GBC"); break;
    case GB_CONSOLE_SGB: snprintf(out, out_len, "SGB"); break;
    default:             snprintf(out, out_len, "GB"); break;
    }
}

static int gb_console_default(void)
{
    /* GBC carts → GBC. Everything else (DMG / SGB-enhanced) → GB. */
    return (rom_cgb_flag & 0x80) ? GB_CONSOLE_CGB : GB_CONSOLE_DMG;
}

static int gb_console_options(int *opts, int max_opts)
{
    /* GBC: only GBC. GB/SGB: GB, plus SGB when the cart supports it. */
    int n = 0;
    if (rom_cgb_flag & 0x80) {
        if (n < max_opts)
            opts[n++] = GB_CONSOLE_CGB;
    } else {
        if (n < max_opts)
            opts[n++] = GB_CONSOLE_DMG;
        if (rom_sgb_compatible && n < max_opts)
            opts[n++] = GB_CONSOLE_SGB;
    }
    return n;
}

static int gb_console_next(int mode, int dir)
{
    int opts[3];
    int n = gb_console_options(opts, 3);
    if (n <= 0)
        return GB_CONSOLE_DMG;

    int idx = 0;
    for (int i = 0; i < n; i++) {
        if (opts[i] == mode) { idx = i; break; }
    }
    idx = (idx + dir + n) % n;
    return opts[idx];
}

static bool system_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
        int prev = gb_console_mode;
        gb_console_mode = gb_console_next(gb_console_mode,
                                          event == ODROID_DIALOG_NEXT ? +1 : -1);
        /* Single choice (GB-only / GBC-only): left/right is a no-op — skip reset. */
        if (gb_console_mode != prev) {
            odroid_settings_app_int32_set("GBSystem", gb_console_mode);
            g_gb->set_console_mode(gb_console_mode);
            g_gb->reset();
            /* GB: restore user palette. SGB/GBC: leave default / let the game paint. */
            if (g_gb->get_rom()->get_info()->gb_type == 1)
                g_gb->get_lcd()->set_palette(index_palette);
            if (tgb_buffer && g_gb->get_rom()->get_info()->gb_type < 3) {
                g_gb->get_lcd()->clear_win_count();
                for (int y = 0; y < GB_HEIGHT; y++)
                    g_gb->get_lcd()->render(tgb_buffer, y);
            }
        }
    }

    gb_console_label(gb_console_mode, option->value, sizeof(system_values));
    return event == ODROID_DIALOG_ENTER;
}

static bool palette_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    /* Dynamic: GB only. Re-evaluated on every dialog redraw (INIT) so toggling
     * System GB↔SGB greys out / re-enables this row immediately. */
    bool gb_mode = (gb_console_mode == GB_CONSOLE_DMG);
    option->enabled = gb_mode ? 1 : -1;

    int max = g_gb->get_lcd()->get_palette_count() - 1;

    if (gb_mode && event == ODROID_DIALOG_PREV) {
        index_palette = index_palette > 0 ? index_palette - 1 : max;
    }

    if (gb_mode && event == ODROID_DIALOG_NEXT) {
        index_palette = index_palette < max ? index_palette + 1 : 0;
    }

    if (gb_mode && (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT)) {
        g_gb->get_lcd()->set_palette(index_palette);
        odroid_settings_Palette_set(index_palette);

        /* Re-paint the paused frame with the new palette so the
         * overlay background updates immediately (tgb_buffer is RGB565,
         * not indexed — set_palette alone only affects future frames). */
        if (tgb_buffer) {
            g_gb->get_lcd()->clear_win_count();
            for (int y = 0; y < GB_HEIGHT; y++) {
                g_gb->get_lcd()->render(tgb_buffer, y);
            }
        }
    }

    sprintf(option->value, "%d/%d", index_palette + 1, max + 1);
    return event == ODROID_DIALOG_ENTER;
}

static bool sgb_border_update_cb(odroid_dialog_choice_t *option,
                                 odroid_dialog_event_t event, uint32_t repeat)
{
    bool sgb_mode = (gb_console_mode == GB_CONSOLE_SGB);
    option->enabled = sgb_mode ? 1 : -1;

    if (sgb_mode &&
        (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT)) {
        sgb_border_enabled = !sgb_border_enabled;
        odroid_settings_app_int32_set("SGBBorder", sgb_border_enabled ? 1 : 0);
    }

    snprintf(option->value, 16, "%s",
             sgb_border_enabled ? curr_lang->s_Option_ON
                                : curr_lang->s_Option_OFF);
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
#if CHEAT_CODES == 1
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, NULL, NULL, &SaveSram, &update_cheats_gb);
#else
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, NULL, NULL, &SaveSram, NULL);
#endif

    uint8_t ram_size = 0;
    uint8_t is_gbcolor = 0;
    uint8_t sgb_flag = 0;
    uint8_t old_licensee = 0;
    rom_cgb_flag = 0;
    rom_sgb_compatible = false;
    FILE *hdr_file = fopen(ACTIVE_FILE->path, "rb");
    if (hdr_file) {
        if (fseek(hdr_file, 0x149, SEEK_SET) == 0)
            fread(&ram_size, 1, 1, hdr_file);
        if (fseek(hdr_file, 0x143, SEEK_SET) == 0)
            fread(&rom_cgb_flag, 1, 1, hdr_file);
        if (fseek(hdr_file, 0x146, SEEK_SET) == 0)
            fread(&sgb_flag, 1, 1, hdr_file);
        if (fseek(hdr_file, 0x14B, SEEK_SET) == 0)
            fread(&old_licensee, 1, 1, hdr_file);
        is_gbcolor = !((rom_cgb_flag & 0x80) == 0);
        /* Pan Docs: SGB functions unlocked when $0146==0x03 and $014B==0x33.
         * CGB-only carts ($0143==0xC0) cannot usefully run as SGB. */
        rom_sgb_compatible = (sgb_flag == 0x03) && (old_licensee == 0x33) &&
                             ((rom_cgb_flag & 0xC0) != 0xC0);
        fclose(hdr_file);
    }

    static const int tbl_sram[] = {1, 1, 1, 4, 16, 8};
    size_t reserved_size = 0;
    if (is_gbcolor) {
        if (ram_size >= 3)
            reserved_size = 0x2000 * (size_t)tbl_sram[ram_size];
    } else {
        if (ram_size >= 4)
            reserved_size = 0x2000 * (size_t)tbl_sram[ram_size];
    }

    byte *data = NULL;
    uint32_t size = ACTIVE_FILE->size;
    printf("is_gbcolor: %d ram_size: %d\n", is_gbcolor, ram_size);
    printf("free mem: %d reserved_size: %d\n", heap_free_mem(), (int)reserved_size);
    if (size > (heap_free_mem() - reserved_size)) {
        data = (byte *)odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &size, false);
    } else {
        data = (byte *)heap_alloc_mem(size);
        if (data != NULL) {
            odroid_overlay_cache_file_in_ram(ACTIVE_FILE->path, (uint8_t *)data);
        }
    }
    if (data == NULL)
        return;

    /* Same as legacy SD_CARD==0: keep ITC free for GB RAM when cart SRAM mapping doesn't need 32KB ITC */
    if (size <= 0x149 || ram_size != 3)
        heap_itc_alloc(true);

    render = new gw_renderer(0);
    g_gb = new gb(render, true, true);

    gb_console_mode = odroid_settings_app_int32_get("GBSystem", gb_console_default());
    /* Clamp to a mode valid for this cartridge. */
    gb_console_mode = gb_console_next(gb_console_mode, 0);
    g_gb->set_console_mode(gb_console_mode);

    if (!g_gb->load_rom(data, size, NULL, 0, true))
        return;

    if (load_state) {
        odroid_system_emu_load_state(save_slot);
    } else {
        lcd_clear_buffers();
    }

    // Load SRAM
    if (g_gb->get_rom()->has_battery()) {
        char *sram_path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
        const int sram_size = g_gb->get_rom()->get_sram_size();
        FILE *sram_file = fopen(sram_path, "rb");
        if (sram_file != NULL) {
            printf("Load SRAM file: %s\n", sram_path);
            fread(g_gb->get_rom()->get_sram(), sram_size, 1, sram_file);
            fclose(sram_file);
        }
        free(sram_path);
    }

    int max_palette = g_gb->get_lcd()->get_palette_count() - 1;
    index_palette = odroid_settings_Palette_get();
    if (index_palette < 0 || index_palette > max_palette) {
        index_palette = g_gb->get_lcd()->get_current_palette();
    }
    /* User DMG palettes only in pure GB mode (never in SGB — that wipes game colors). */
    if (g_gb->get_rom()->get_info()->gb_type == 1)
        g_gb->get_lcd()->set_palette(index_palette);

    sgb_border_enabled =
        odroid_settings_app_int32_get("SGBBorder", 1) != 0;

#if CHEAT_CODES == 1
    for(int i=0; i<MAX_CHEAT_CODES && i<ACTIVE_FILE->cheat_count; i++) {
        if (odroid_settings_ActiveGameGenieCodes_is_enabled(ACTIVE_FILE->path, i)) {
            apply_cheat_code(ACTIVE_FILE->cheat_codes[i]);
        }
    }
#endif

    gb_console_label(gb_console_mode, system_values, sizeof(system_values));
    char sgb_border_values[16] = {0};
    odroid_dialog_choice_t options[] = {
        /* Only cycle when more than one mode is valid (GB↔SGB). GBC is fixed. */
        {301, curr_lang->s_System, system_values, 1, &system_update_cb},
        /* enabled updated each frame: custom palettes only in GB (type 1) */
        {302, curr_lang->s_SGB_Border, sgb_border_values, -1, &sgb_border_update_cb},
        {300, curr_lang->s_Palette, (char *)palette_values, 1, &palette_update_cb},
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
