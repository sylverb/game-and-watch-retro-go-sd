#include <stdio.h>
#include <string.h>
#include <ctype.h>
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
#include "fceu.h"
#include "fceu-state.h"
#include "fceu-cart.h"
#include "fds.h"
#include "driver.h"
#include "video.h"
#include "gw_malloc.h"
#include "odroid_overlay.h"
#include "rg_storage.h"
#include "rg_frogfs.h"

#define NES_WIDTH  256
#define NES_HEIGHT 240

extern CartInfo iNESCart;

static uint8_t nes_framebuffer[(NES_WIDTH+16)*NES_HEIGHT];
static bool crop_overscan_v;
static bool crop_overscan_h;

static char palette_values_text[50];
static char sprite_limit_text[5];
static char crop_overscan_v_text[5];
static char crop_overscan_h_text[5];
static char next_disk_text[32];
static char eject_insert_text[32];
static char overclocking_text[32];
static uint8_t palette_index = 0;
static uint8_t overclocking_type = 0;
static uint8_t allow_swap_disk = 0;
static bool disable_sprite_limit = false;

uint8_t *UNIFchrrama = 0;

unsigned overclock_enabled = -1;
unsigned overclocked = 0;
unsigned skip_7bit_overclocking = 1; /* 7-bit samples have priority over overclocking */
unsigned totalscanlines = 240;
unsigned normal_scanlines = 240;
unsigned extrascanlines = 0;
unsigned vblankscanlines = 0;

#define NES_FREQUENCY_18K 18000 // 18 kHz to limit cpu usage
#define NES_FREQUENCY_48K 48000
static uint samplesPerFrame;

static int32_t *sound = 0;

static uint32_t fceu_joystick; /* player input data, 1 byte per player (1-4) */

static void blit(uint8_t *src, uint16_t *framebuffer);
static bool crop_overscan_v_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat);
static bool crop_overscan_h_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat);
static bool fds_eject_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat);
static bool fds_side_swap_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat);
static bool overclocking_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat);

static SFORMAT gnw_save_data[] = {
	{ &crop_overscan_v, 1, "HCRO" },
	{ &crop_overscan_h, 1, "VCRO" },
	{ &overclocking_type, 1, "OCPR" },
	{ &disable_sprite_limit, 1, "SPLI" },
    { &palette_index, 1, "NPAL"},
	{ 0 }
};

/* table for currently loaded palette */
static uint8_t base_palette[192];

struct st_palettes {
   char name[32];
   unsigned int data[64];
};

#define PALETTE_FILENAME "/bios/nes/palettes.bin"
static uint16_t palettes_count;
static struct st_palettes palette;

static uint16_t get_palettes_count() {
    FILE *file = fopen(PALETTE_FILENAME, "rb");
    if (!file) {
        return 0;
    }

    fseek(file, 0, SEEK_END);
    uint16_t size = (uint16_t)ftell(file);
    fclose(file);

    return size / sizeof(struct st_palettes);
}

static int get_palette_name(int index, char *name_out) {
    FILE *file = fopen(PALETTE_FILENAME, "rb");
    if (!file) {
        return 0;
    }

    fseek(file, index * sizeof(struct st_palettes), SEEK_SET);
    fread(name_out, sizeof(char), 21, file);
    name_out[20] = '\0';

    fclose(file);
    return 1;
}

static int load_palette(int index, struct st_palettes *palette) {
    FILE *file = fopen(PALETTE_FILENAME, "rb");
    if (!file) {
        return 0;
    }

    fseek(file, index * sizeof(struct st_palettes), SEEK_SET);
    fread(palette, sizeof(struct st_palettes), 1, file);

    fclose(file);
    return 1;
}

void setCustomPalette(uint16_t palette_idx) {
      load_palette(palette_idx, &palette);
      unsigned *palette_data = palette.data;
      for (int i = 0; i < 64; i++ )
      {
         unsigned data = palette_data[i];
         base_palette[ i * 3 + 0 ] = ( data >> 16 ) & 0xff; /* red */
         base_palette[ i * 3 + 1 ] = ( data >>  8 ) & 0xff; /* green */
         base_palette[ i * 3 + 2 ] = ( data >>  0 ) & 0xff; /* blue */
      }
      FCEUI_SetPaletteArray( base_palette, 64 );
}

/* Unified palette selection used by both new game and savestate load so the
 * colors match in every case.
 *   idx == 0 (or no palette file)  -> fceumm built-in default palette
 *                                     (this is what a fresh "new game" shows).
 *   idx 1..palettes_count          -> custom palette entry [idx-1] from
 *                                     /bios/nes/palettes.bin.
 */
static void apply_palette(uint8_t idx) {
    if (idx == 0 || palettes_count == 0) {
        FCEUI_SetPaletteArray(NULL, 0); /* clears the user palette -> built-in default */
    } else {
        setCustomPalette(idx - 1);
    }
}

void FCEUD_PrintError(char *c)
{
    printf("%s", c);
}

void FCEUD_DispMessage(enum retro_log_level level, unsigned duration, const char *str)
{
    printf("%s", str);
}

void FCEUD_Message(char *s)
{
    printf("%s", s);
}

static bool SaveState(const char *savePathName)
{
    FCEUSS_Save_Fs(savePathName);
    return true;
}

static bool LoadState(const char *savePathName)
{
    FCEUSS_Load_Fs(savePathName);
    /* palette_index was just restored from the state; rebuild the RGB565 LUT so
     * the palette matches (otherwise blacks/colors differ from a fresh start). */
    apply_palette(palette_index);
    return true;
}

static void *Screenshot()
{
    lcd_wait_for_vblank();

    lcd_clear_active_buffer();
    blit(nes_framebuffer, lcd_get_active_buffer());
    return lcd_get_active_buffer();
}

/*
 * .sram file
 * - Cartridge: concatenation of SaveGame[0..3] (FCEU order), typically same layout as a .sav.
 * - FDS: no cartridge SRAM; we read/write the in-RAM disk image (FDSROM) after load from flash/ROM.
 *   That buffer is what changes when the game writes to the virtual disk; .sram snapshots that
 *   working copy, not the original .fds file on flash.
 */

 static CartInfo *nes_fceu_cart_for_sram(void)
 {
     if (!GameInfo || (GameInfo->type != GIT_CART && GameInfo->type != GIT_VSUNI))
         return NULL;
     if (iNESCart.battery && iNESCart.SaveGame[0] && iNESCart.SaveGameLen[0])
         return &iNESCart;
 /*	if (UNIFCart.battery && UNIFCart.SaveGame[0] && UNIFCart.SaveGameLen[0])
         return &UNIFCart;*/
     return NULL;
 }
 
 static int nes_fceu_sram_count_blocks(CartInfo *c)
 {
     int n = 0;
     for (int i = 0; i < 4; i++)
         if (c->SaveGame[i] && c->SaveGameLen[i])
             n++;
     return n;
 }
 
 static void nes_fceu_sram_load(void)
 {
     if (!GameInfo || GameInfo->type == GIT_NSF)
         return;
 
     char *path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
     FILE *f = fopen(path, "rb");
     if (!f) {
         free(path);
         return;
     }
 
     if (fseek(f, 0, SEEK_END) != 0) {
         fclose(f);
         free(path);
         return;
     }
     long fsz = ftell(f);
     if (fsz <= 0 || fseek(f, 0, SEEK_SET) != 0) {
         fclose(f);
         free(path);
         return;
     }
 
     if (GameInfo->type == GIT_FDS) {
         /* Load back into the in-RAM disk buffer (same memory the core emulates) */
         uint8 *p = FDSROM_ptr();
         uint32 cap = FDSROM_size();
         if (p && cap)
             fread(p, 1, (size_t)fsz < cap ? (size_t)fsz : cap, f);
     } else {
         CartInfo *c = nes_fceu_cart_for_sram();
         if (c) {
             size_t remaining = (size_t)fsz;
             for (int i = 0; i < 4; i++) {
                 if (!c->SaveGame[i] || !c->SaveGameLen[i])
                     continue;
                 size_t cap = (size_t)c->SaveGameLen[i];
                 size_t n = remaining < cap ? remaining : cap;
                 if (n)
                     fread(c->SaveGame[i], 1, n, f);
                 remaining -= n;
                 if (!remaining)
                     break;
             }
         }
     }
 
     fclose(f);
     free(path);
 }
 
 static void nes_fceu_sram_save_cb(void)
 {
     if (!GameInfo || GameInfo->type == GIT_NSF)
         return;
 
     CartInfo *cart = NULL;
     int n_cart_blocks = 0;
     uint8 *fds_p = NULL;
     uint32 fds_sz = 0;
 
     if (GameInfo->type == GIT_FDS) {
         /* In-RAM disk copy (see nes_getromdata / fds.c): current state including in-game disk writes */
         fds_p = FDSROM_ptr();
         fds_sz = FDSROM_size();
         if (!fds_p || !fds_sz)
             return;
     } else {
         cart = nes_fceu_cart_for_sram();
         if (!cart)
             return;
         n_cart_blocks = nes_fceu_sram_count_blocks(cart);
         if (!n_cart_blocks)
             return;
     }
 
     char *path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
     FILE *f = fopen(path, "wb");
     if (!f) {
         free(path);
         return;
     }
 
     if (GameInfo->type == GIT_FDS) {
         /* Serialize the writable in-RAM disk copy, not the original flash ROM image */
         fwrite(fds_p, 1, fds_sz, f);
     } else {
         for (int i = 0; i < 4; i++) {
             if (!cart->SaveGame[i] || !cart->SaveGameLen[i])
                 continue;
             fwrite(cart->SaveGame[i], 1, cart->SaveGameLen[i], f);
         }
     }
 
     fclose(f);
     free(path);
 }
  
unsigned dendy = 0;

static uint16_t palette565[256];
static uint32_t palette_spaced_565[256];

#define RED_SHIFT 11
#define GREEN_SHIFT 5
#define BLUE_SHIFT 0
#define RED_EXPAND 3
#define GREEN_EXPAND 2
#define BLUE_EXPAND 3
#define BUILD_PIXEL_RGB565(R,G,B) (((int) ((R)&0x1f) << RED_SHIFT) | ((int) ((G)&0x3f) << GREEN_SHIFT) | ((int) ((B)&0x1f) << BLUE_SHIFT))

void FCEUD_SetPalette(uint16 index, uint8_t r, uint8_t g, uint8_t b)
{
   if (index >= 256)
      return;
    uint16_t color_565 = BUILD_PIXEL_RGB565(r >> RED_EXPAND, g >> GREEN_EXPAND, b >> BLUE_EXPAND);
    palette565[index] = color_565;
    uint32_t sc = ((0b1111100000000000&color_565)<<10) | ((0b0000011111100000&color_565)<<5) | ((0b0000000000011111&color_565));
    palette_spaced_565[index] = sc;
}

static void nesInputUpdate(odroid_gamepad_state_t *joystick)
{
    uint8_t input_buf  = 0;
    if (joystick->values[ODROID_INPUT_LEFT]) {
        input_buf |= JOY_LEFT;
    }
    if (joystick->values[ODROID_INPUT_RIGHT]) {
        input_buf |= JOY_RIGHT;
    }
    if (joystick->values[ODROID_INPUT_UP]) {
        input_buf |= JOY_UP;
    }
    if (joystick->values[ODROID_INPUT_DOWN]) {
        input_buf |= JOY_DOWN;
    }
    if (joystick->values[ODROID_INPUT_A]) {
        input_buf |= JOY_A;
    }
    if (joystick->values[ODROID_INPUT_B]) {
        input_buf |= JOY_B;
    }
    // Game button on G&W
    if (joystick->values[ODROID_INPUT_START]) {
        input_buf |= JOY_START;
    }
    // Time button on G&W
    if (joystick->values[ODROID_INPUT_SELECT]) {
        input_buf |= JOY_SELECT;
    }
    // Start button on Zelda G&W
    if (joystick->values[ODROID_INPUT_X]) {
        input_buf |= JOY_START;
    }
    // Select button on Zelda G&W
    if (joystick->values[ODROID_INPUT_Y]) {
        input_buf |= JOY_SELECT;
    }
    fceu_joystick = input_buf;
}

// No scaling
__attribute__((optimize("unroll-loops")))
static inline void blit_normal(uint8_t *src, uint16_t *framebuffer) {
    uint32_t x, y;
    uint8_t incr   = 0;
    uint16_t width  = NES_WIDTH;
    uint16_t height = NES_HEIGHT;
    uint8_t offset_x  = (GW_LCD_WIDTH - width) / 2;
    uint8_t offset_y  = 0;

    incr     += (crop_overscan_h ? 16 : 0);
    width    -= (crop_overscan_h ? 16 : 0);
    height   -= (crop_overscan_v ? 16 : 0);
    src      += (crop_overscan_v ? ((crop_overscan_h ? 8 : 0) + NES_WIDTH * 8) : (crop_overscan_h ? 8 : 0));
    offset_x += (crop_overscan_h ? 8 : 0);
    offset_y  = (crop_overscan_v ? 8 : 0);

    for (y = 0; y < height; y++, src += incr) {
        for (x = 0; x < width; x++, src++) {
            framebuffer[(y+offset_y) * GW_LCD_WIDTH + x + offset_x] = palette565[*src];
        }
    }
}

__attribute__((optimize("unroll-loops")))
static inline void screen_blit_nn(uint8_t *src, uint16_t *framebuffer)
{
    uint16_t w1 = NES_WIDTH - (crop_overscan_h ? 16 : 0);
    uint16_t h1 = NES_HEIGHT - (crop_overscan_v ? 16 : 0);
    uint16_t w2 = GW_LCD_WIDTH;
    uint16_t h2 = GW_LCD_HEIGHT;
    uint8_t src_x_offset = (crop_overscan_h ? 8 : 0);
    uint8_t src_y_offset = (crop_overscan_v ? 8 : 0);
    int x_ratio = (int)((w1<<16)/w2) +1;
    int y_ratio = (int)((h1<<16)/h2) +1;

    int x2;
    int y2;

    for (int i=0;i<h2;i++) {
        for (int j=0;j<w2;j++) {
            x2 = ((j*x_ratio)>>16) ;
            y2 = ((i*y_ratio)>>16) ;
            uint8_t b2 = src[((y2+src_y_offset)*NES_WIDTH)+x2+src_x_offset];
            framebuffer[(i*w2)+j] = palette565[b2];
        }
    }
}

__attribute__((optimize("unroll-loops")))
static inline void blit_nearest(uint8_t *src, uint16_t *framebuffer)
{
    int w1 = NES_WIDTH - (crop_overscan_h ? 16 : 0);
    int w2 = GW_LCD_WIDTH;
    int h2 = GW_LCD_HEIGHT - (crop_overscan_v ? 16 : 0);
    int src_x_offset = (crop_overscan_h ? 8 : 0);
    int dst_x_offset = (crop_overscan_h ? 10 : 0);
    uint8_t y_offset = (crop_overscan_v ? 8 : 0);
    // duplicate one column every 3 lines -> x1.25
    int scale_ctr = 3;

    for (int y = y_offset; y < h2; y++) {
        int ctr = 0;
        uint8_t  *src_row  = &src[y*NES_WIDTH+src_x_offset];
        uint16_t *dest_row = &framebuffer[y * w2 + dst_x_offset];
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
}

#define CONV(_b0) ((0b11111000000000000000000000&_b0)>>10) | ((0b000001111110000000000&_b0)>>5) | ((0b0000000000011111&_b0));

__attribute__((optimize("unroll-loops")))
static void blit_4to5(uint8_t *src, uint16_t *framebuffer) {
    int w1 = NES_WIDTH - (crop_overscan_h ? 16 : 0);
    int w2 = GW_LCD_WIDTH;
    int h2 = GW_LCD_HEIGHT - (crop_overscan_v ? 16 : 0);

    int src_x_offset = (crop_overscan_h ? 8 : 0);
    int dst_x_offset = (crop_overscan_h ? 10 : 0);
    uint8_t y_offset = (crop_overscan_v ? 8 : 0);

    for (int y = y_offset; y < h2; y++) {
        uint8_t  *src_row  = &src[y*NES_WIDTH+src_x_offset];
        uint16_t *dest_row = &framebuffer[y * w2 + dst_x_offset];
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
static void blit_5to6(uint8_t *src, uint16_t *framebuffer) {
    int w1_adjusted = NES_WIDTH - 4 - (crop_overscan_h ? 16 : 0);
    int w2 = WIDTH;
    int h2 = GW_LCD_HEIGHT - (crop_overscan_v ? 16 : 0);
    int dst_x_offset = (WIDTH - 307) / 2 + (crop_overscan_h ? 9 : 0);

    int src_x_offset = (crop_overscan_h ? 8 : 0);
    uint8_t y_offset = (crop_overscan_v ? 8 : 0);

    // x 1.2
    for (int y = y_offset; y < h2; y++) {
        uint8_t  *src_row  = &src[y*NES_WIDTH+src_x_offset];
        uint16_t *dest_row = &framebuffer[y * w2 + dst_x_offset];
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

static void blit(uint8_t *src, uint16_t *framebuffer)
{
    odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();
    odroid_display_filter_t filtering = odroid_display_get_filter_mode();

    switch (scaling) {
    case ODROID_DISPLAY_SCALING_OFF:
        // Full height, borders on the side
        blit_normal(src, framebuffer);
        break;
    case ODROID_DISPLAY_SCALING_FIT:
        // Full height and width, with cropping removal
        screen_blit_nn(src, framebuffer);
        break;
    case ODROID_DISPLAY_SCALING_FULL:
        // full height, full width
        if (filtering == ODROID_DISPLAY_FILTER_OFF) {
            blit_nearest(src, framebuffer);
        } else {
            blit_4to5(src, framebuffer);
        }
        break;
    case ODROID_DISPLAY_SCALING_CUSTOM:
        // full height, almost full width
        blit_5to6(src, framebuffer);
        break;
    default:
        printf("Unknown scaling mode %d\n", scaling);
        assert(!"Unknown scaling mode");
        break;
    }
}

static void update_sound_nes(int32_t *sound, uint16_t size) {
    if (common_emu_sound_loop_is_muted()) {
        return;
    }

    int32_t factor = common_emu_sound_get_volume();
    int16_t* sound_buffer = audio_get_active_buffer();
    uint16_t sound_buffer_length = audio_get_buffer_length();

    // Write to DMA buffer and lower the volume accordingly
    for (int i = 0; i < sound_buffer_length; i++) {
        int32_t sample = sound[i];
        sound_buffer[i] = ((sample * factor) >> 8) & 0xFFFF;
    }
}
extern uint32_t __RAM_EMU_END__;

static size_t nes_getromdata(unsigned char **data)
{
    uint32_t size = ACTIVE_FILE->size; 
#ifndef GNW_DISABLE_COMPRESSION
#if SD_CARD == 1
#error "Roms compression is not supported on SD Card"
#else
    unsigned char *dest = (unsigned char *)&_OVERLAY_NES_FCEU_BSS_END;
    ram_start = (uint32_t)dest;
    // We do not use ram_get_free_size as we may want to update ram_start after
    uint32_t free_ram = ((uint32_t)&__RAM_EMU_END__) - ram_start;
    if(strcmp(ACTIVE_FILE->ext, "lzma") == 0) {
        printf("Decompressing NES ROM...\n");
        uint32_t src_size = size;
        const unsigned char *src;
        rg_frogfs_get_file_data(ACTIVE_FILE->path, &src, &src_size);
        size_t n_decomp_bytes = lzma_inflate(dest, free_ram, src, src_size);
        *data = dest;
        ram_start += n_decomp_bytes;
        return n_decomp_bytes;
    } else {
        // FDS disks has to be stored in ram for games
        // that want to write to the disk
        if (size <= 262000) {
            uint32_t src_size;
            const unsigned char *src;
            rg_frogfs_get_file_data(ACTIVE_FILE->path, &src, &src_size);
            memcpy(dest, src, src_size);
            *data = (unsigned char *)dest;
            ram_start = (uint32_t)dest + src_size;
            return src_size;
        } else {
            rg_frogfs_get_file_data(ACTIVE_FILE->path, (const uint8_t **)data, &size);
            return size;
        }
    }
#endif
#else
    ram_start = (uint32_t)&_OVERLAY_NES_FCEU_BSS_END;
    if (size > ram_get_free_size()) {
        *data = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &size, false);
    } else {
        *data = ram_malloc(size);
        if (*data != NULL) {
            odroid_overlay_cache_file_in_ram(ACTIVE_FILE->path, *data);
        }
    }
    return size;
#endif
}

static bool palette_update_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (palettes_count > 0) {
        /* index 0 is the built-in default, 1..palettes_count are the .bin entries */
        int max = palettes_count;

        if (event == ODROID_DIALOG_PREV) palette_index = palette_index > 0 ? palette_index - 1 : max;
        if (event == ODROID_DIALOG_NEXT) palette_index = palette_index < max ? palette_index + 1 : 0;

        if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
            apply_palette(palette_index);
        }
        if (palette_index == 0) {
            sprintf(option->value, "%10s", curr_lang->s_Default);
        } else {
            get_palette_name(palette_index - 1, palette.name);
            sprintf(option->value, "%10s", palette.name);
        }
    } else {
        option->value = '\0';
    }

    return event == ODROID_DIALOG_ENTER;
}

static bool sprite_limit_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if ((event == ODROID_DIALOG_NEXT) || (event == ODROID_DIALOG_PREV)) {
        disable_sprite_limit = !disable_sprite_limit;
    }
    sprintf(option->value,"%s",disable_sprite_limit?curr_lang->s_Yes:curr_lang->s_No);

    FCEUI_DisableSpriteLimitation(disable_sprite_limit);

    return event == ODROID_DIALOG_ENTER;
}

static void update_overclocking(uint8_t oc_profile)
{
    switch (oc_profile) {
        case 0: // No overclocking
            skip_7bit_overclocking = 1;
            extrascanlines         = 0;
            vblankscanlines        = 0;
            overclock_enabled      = 0;
            break;
        case 1: // 2x-Postrender
            skip_7bit_overclocking = 1;
            extrascanlines         = 266;
            vblankscanlines        = 0;
            overclock_enabled      = 1;
            break;
        case 2: // 2x-VBlank
            skip_7bit_overclocking = 1;
            extrascanlines         = 0;
            vblankscanlines        = 266;
            overclock_enabled      = 1;
            break;
    }
}

static bool overclocking_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    uint8_t max_index = 2;

    if (event == ODROID_DIALOG_NEXT) {
        overclocking_type = overclocking_type < max_index ? overclocking_type + 1 : 0;
    }
    if (event == ODROID_DIALOG_PREV) {
        overclocking_type = overclocking_type > 0 ? overclocking_type - 1 : max_index;
    }
    switch (overclocking_type) {
        case 0: // No overclocking
            skip_7bit_overclocking = 1;
            extrascanlines         = 0;
            vblankscanlines        = 0;
            overclock_enabled      = 0;
            sprintf(option->value,"%s",curr_lang->s_No);
            break;
        case 1: // 2x-Postrender
            skip_7bit_overclocking = 1;
            extrascanlines         = 266;
            vblankscanlines        = 0;
            overclock_enabled      = 1;
            sprintf(option->value,"%s","2x-Postrender");
            break;
        case 2: // 2x-VBlank
            skip_7bit_overclocking = 1;
            extrascanlines         = 0;
            vblankscanlines        = 266;
            overclock_enabled      = 1;
            sprintf(option->value,"%s","2x-VBlank");
            break;
    }
    return event == ODROID_DIALOG_ENTER;
}

static bool crop_overscan_v_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if ((event == ODROID_DIALOG_NEXT) || (event == ODROID_DIALOG_PREV)) {
        crop_overscan_v = (crop_overscan_v+1)%2;
    }
    sprintf(option->value,"%s",crop_overscan_v?curr_lang->s_Yes:curr_lang->s_No);
    return event == ODROID_DIALOG_ENTER;
}

static bool crop_overscan_h_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if ((event == ODROID_DIALOG_NEXT) || (event == ODROID_DIALOG_PREV)) {
        crop_overscan_h = (crop_overscan_h+1)%2;
    }
    sprintf(option->value,"%s",crop_overscan_h?curr_lang->s_Yes:curr_lang->s_No);
    return event == ODROID_DIALOG_ENTER;
}

static bool reset_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_ENTER) {
        FCEUI_ResetNES();
    }
    return event == ODROID_DIALOG_ENTER;
}

static bool fds_side_swap_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_NEXT) {
        FCEU_FDSSelect();          /* Swap FDisk side */
    }
    if (event == ODROID_DIALOG_PREV) {
        FCEU_FDSSelect_previous(); /* Swap FDisk side */
    }
    int8 diskinfo = FCEU_FDSCurrentSideDisk();
    sprintf(option->value,curr_lang->s_NES_FDS_Side_Format,1+((diskinfo&2)>>1),(diskinfo&1)?"B":"A");
    if (event == ODROID_DIALOG_ENTER) {
        allow_swap_disk = false;
        FCEU_FDSInsert(-1);        /* Insert the disk */
    }
    return event == ODROID_DIALOG_ENTER;
}

static bool fds_eject_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    bool inserted = FCEU_FDSIsDiskInserted();
    if (event == ODROID_DIALOG_ENTER) {
        if (inserted) {
            allow_swap_disk = true;
        } else {
            allow_swap_disk = false;
        }
        FCEU_FDSInsert(-1);        /* Insert or eject the disk */
    }
    sprintf(option->value,inserted?curr_lang->s_NES_Eject_FDS:curr_lang->s_NES_Insert_FDS);
    return event == ODROID_DIALOG_ENTER;
}

#if CHEAT_CODES == 1
static int checkGG(char c)
{
   static const char lets[16] = { 'A', 'P', 'Z', 'L', 'G', 'I', 'T', 'Y', 'E', 'O', 'X', 'U', 'K', 'S', 'V', 'N' };
   int x;

   for (x = 0; x < 16; x++)
      if (lets[x] == toupper(c))
         return 1;
   return 0;
}

static int GGisvalid(const char *code)
{
   size_t len = strlen(code);
   uint32 i;

   if (len != 6 && len != 8)
      return 0;

   for (i = 0; i < len; i++)
      if (!checkGG(code[i]))
         return 0;
   return 1;
}

void apply_cheat_code(const char *cheatcode) {
    uint16 a;
    uint8  v;
    int    c;
    int    type = 1;
    char temp[256];
    char *codepart;

    strcpy(temp, cheatcode);
    codepart = strtok(temp, "+,;._ ");

    while (codepart)
    {
        size_t codepart_len = strlen(codepart);
        if ((codepart_len == 7) && (codepart[4]==':'))
        {
            /* raw code in xxxx:xx format */
            printf("Cheat code added: '%s' (Raw)\n", codepart);
            codepart[4] = '\0';
            a = strtoul(codepart, NULL, 16);
            v = strtoul(codepart + 5, NULL, 16);
            c = -1;
            /* Zero-page addressing modes don't go through the normal read/write handlers in FCEU, so
            * we must do the old hacky method of RAM cheats. */
            if (a < 0x0100) type = 0;
            FCEUI_AddCheat(NULL, a, v, c, type);
        }
        else if ((codepart_len == 10) && (codepart[4] == '?') && (codepart[7] == ':'))
        {
            /* raw code in xxxx?xx:xx */
            printf("Cheat code added: '%s' (Raw)\n", codepart);
            codepart[4] = '\0';
            codepart[7] = '\0';
            a = strtoul(codepart, NULL, 16);
            v = strtoul(codepart + 8, NULL, 16);
            c = strtoul(codepart + 5, NULL, 16);
            /* Zero-page addressing modes don't go through the normal read/write handlers in FCEU, so
            * we must do the old hacky method of RAM cheats. */
            if (a < 0x0100) type = 0;
            FCEUI_AddCheat(NULL, a, v, c, type);
        }
        else if (GGisvalid(codepart) && FCEUI_DecodeGG(codepart, &a, &v, &c))
        {
            FCEUI_AddCheat(NULL, a, v, c, type);
            printf("Cheat code added: '%s' (GG)\n", codepart);
        }
        else if (FCEUI_DecodePAR(codepart, &a, &v, &c, &type))
        {
            FCEUI_AddCheat(NULL, a, v, c, type);
            printf("Cheat code added: '%s' (PAR)\n", codepart);
        }
        codepart = strtok(NULL,"+,;._ ");
    }
}
#endif

/* gw_sleep() restores the *settings* OC level on wake, but this core forces the
 * maximum OC during gameplay when the user left the setting at 0.  Without this
 * the game keeps running at the (slower) settings clock after a sleep/wake
 * cycle.  Re-apply the boost and reinit audio (SystemClock_Config also
 * reprograms the audio PLL). */
static void nes_fceu_sleep_wake_up()
{
    if (odroid_settings_cpu_oc_level_get() == 0) {
        SystemClock_Config(2);
        odroid_audio_init(odroid_audio_sample_rate_get());
        audio_start_playing_full_length(audio_get_buffer_full_length());
    }
}

int app_main_nes_fceu(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    uint8_t *rom_data;
    uint32_t rom_size;
    bool drawFrame;
    uint8_t *gfx;
    int32_t ssize = 0;

    // Set maximum clock speed for better performance if CPU is not overclocked
    if (odroid_settings_cpu_oc_level_get() == 0) {
        SystemClock_Config(2);
    }

    uint32_t sndsamplerate = NES_FREQUENCY_48K;
    odroid_gamepad_state_t joystick;

    crop_overscan_v = false;
    crop_overscan_h = false;

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }

    XBuf = nes_framebuffer;

    palettes_count = get_palettes_count();

    // We allocate 4 bytes in itc RAM to prevent FCEU_gmalloc to return 0x00 for the first
    // allocation (as itc RAM is starting at 0x00000000) as it can cause a lot of problems
    // due to the way FCEUmm works.
    itc_malloc(4);

    FCEUI_Initialize();

    rom_size = nes_getromdata(&rom_data);
    FCEUGI *gameInfo = FCEUI_LoadGame(ACTIVE_FILE->name, rom_data, rom_size,
                                     NULL);

    PowerNES();

    FCEUI_SetInput(0, SI_GAMEPAD, &fceu_joystick, 0);

    // If mapper is 85 (with YM2413 FM sound) and CPU is not overclocked,
    // we have to use lower sample rate as STM32H7 CPU can't otherwise handle FM sound emulation at 48kHz
    if (odroid_settings_cpu_oc_level_get() == 0 && gameInfo->type == GIT_CART && iNESCart.mapper == 85) {
        sndsamplerate = NES_FREQUENCY_18K;
    }
    FCEUI_Sound(sndsamplerate);
    FCEUI_SetSoundVolume(150);

    odroid_system_init(APPID_NES, sndsamplerate);
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, NULL, &nes_fceu_sleep_wake_up, &nes_fceu_sram_save_cb, NULL);

    if (FSettings.PAL) {
        lcd_set_refresh_rate(50);
        common_emu_state.frame_time_10us = (uint16_t)(100000 / 50 + 0.5f);
        samplesPerFrame = sndsamplerate / 50;
    } else {
        lcd_set_refresh_rate(60);
        common_emu_state.frame_time_10us = (uint16_t)(100000 / 60 + 0.5f);
        samplesPerFrame = sndsamplerate / 60;
    }

    // Init Sound
    audio_start_playing(samplesPerFrame);

    AddExState(&gnw_save_data, ~0, 0, 0);

    /* Cart SRAM or FDS in-RAM disk — before savestate so a resumed slot overwrites .sram */
    nes_fceu_sram_load();

    if (load_state) {
        /* LoadState() reapplies the palette from the restored palette_index */
        odroid_system_emu_load_state(save_slot);

        // Update local settings
        update_overclocking(overclocking_type);
        FCEUI_DisableSpriteLimitation(disable_sprite_limit);

        bool inserted = FCEU_FDSIsDiskInserted();
        if (inserted) {
            allow_swap_disk = false;
        } else {
            allow_swap_disk = true;
        }
    } else {
        apply_palette(palette_index);
        lcd_clear_buffers();
    }

#if CHEAT_CODES == 1
    for(int i=0; i<MAX_CHEAT_CODES && i<ACTIVE_FILE->cheat_count; i++) {
        if (odroid_settings_ActiveGameGenieCodes_is_enabled(ACTIVE_FILE->path, i)) {
            apply_cheat_code(ACTIVE_FILE->cheat_codes[i]);
        }
    }
#endif

    void _blit()
    {
        blit(nes_framebuffer, lcd_get_active_buffer());
        common_ingame_overlay();
    }

    while(1) {
        odroid_dialog_choice_t options[] = {
            // {101, "More...", "", 1, &advanced_settings_cb},
            {302, curr_lang->s_Palette, palette_values_text, palettes_count > 0 ? 1 : -1, &palette_update_cb},
            {302, curr_lang->s_Reset, NULL, 1, &reset_cb},
            {302, curr_lang->s_Crop_Vertical_Overscan,crop_overscan_v_text,1,&crop_overscan_v_cb},
            {302, curr_lang->s_Crop_Horizontal_Overscan,crop_overscan_h_text,1,&crop_overscan_h_cb},
            {302, curr_lang->s_Disable_Sprite_Limit,sprite_limit_text,1,&sprite_limit_cb},
            {302, curr_lang->s_NES_CPU_OC,overclocking_text,1,&overclocking_cb},
            {302, curr_lang->s_NES_Eject_Insert_FDS,eject_insert_text,GameInfo->type == GIT_FDS ? 1 : -1,&fds_eject_cb},
            {302, curr_lang->s_NES_Swap_Side_FDS,next_disk_text,allow_swap_disk ? 1 : -1,&fds_side_swap_cb},
            ODROID_DIALOG_CHOICE_LAST
        };

        wdog_refresh();

        drawFrame = common_emu_frame_loop();

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &_blit);
        common_emu_input_loop_handle_turbo(&joystick);

        nesInputUpdate(&joystick);

        FCEUI_Emulate(&gfx, &sound, &ssize, !drawFrame);

        if (drawFrame)
        {
            _blit();
            lcd_swap();
        }

        update_sound_nes(sound,ssize);

        common_emu_sound_sync(false);
    }

    return 0;
}
