#pragma once
#include <stdint.h>

typedef struct
{
    uint16_t width;
    uint16_t height;
    char logo[];
} retro_logo_image;

enum {
    RG_LOGO_EMPTY = -1,
    /* Always in internal flash (INT_LOGO_COUNT in rg_logos.c). */
    RG_LOGO_RGO = 0,
    RG_LOGO_RGW,
    RG_LOGO_GNW,
    /* Remaining indices are sequential blobs in /bios/logo.bin
     * (extracted from .sdcard_logo). Keep enum order in sync with
     * LOGO_DATA definition order in rg_logos.c. */
    RG_LOGO_HEADER_HOMEBREW,
    RG_LOGO_HEADER_FAVORITES,
};

void odroid_overlay_draw_logo(uint16_t x_pos, uint16_t y_pos, int16_t logo_idx, uint16_t color);
void rg_reset_logo_buffers();
retro_logo_image *rg_get_logo(int16_t logo_index);

/* Registers a logo blob stored inside a dynamic core binary (metadata
 * see gnw_core_meta.h) and returns a negative "logo index" that can be
 * passed to add_emulator()/gui_add_tab() and later resolved back by
 * rg_get_logo() exactly like a compile-time RG_LOGO_* index.
 *
 * The firmware keeps only a fixed number of decode buffers and loads the
 * requested blob on demand, so this function does not allocate AHB/heap
 * memory per logo. Returns RG_LOGO_EMPTY if the registry is full or the
 * blob size is out of bounds. */
int16_t rg_register_dynamic_logo_blob(const char *core_path, uint32_t offset, uint32_t size);

extern const retro_logo_image logo_rgo;
extern const retro_logo_image logo_rgw;
extern const retro_logo_image logo_gnw;

extern const retro_logo_image header_homebrew;
extern const retro_logo_image header_favorites;

extern const unsigned char IMG_SPEAKER[];
extern const unsigned char IMG_SUN[];
extern const unsigned char IMG_FOLDER[];
extern const unsigned char IMG_DISKETTE[];
extern const unsigned char IMG_0_5X[];
extern const unsigned char IMG_0_75X[];
extern const unsigned char IMG_1X[];
extern const unsigned char IMG_1_25X[];
extern const unsigned char IMG_1_5X[];
extern const unsigned char IMG_2X[];
extern const unsigned char IMG_3X[];
extern const unsigned char IMG_SC[];
extern const unsigned char IMG_BUTTON_A[];
extern const unsigned char IMG_BUTTON_A_P[];
extern const unsigned char IMG_BUTTON_B[];
extern const unsigned char IMG_BUTTON_B_P[];

extern const unsigned char img_clock_00[];
extern const unsigned char img_clock_01[];
extern const unsigned char img_clock_02[];
extern const unsigned char img_clock_03[];
extern const unsigned char img_clock_04[];
extern const unsigned char img_clock_05[];
extern const unsigned char img_clock_06[];
extern const unsigned char img_clock_07[];
extern const unsigned char img_clock_08[];
extern const unsigned char img_clock_09[];

extern const unsigned char IMG_BORDER_ZELDA3[];
extern const unsigned char IMG_BORDER_LEFT_SMW[];
extern const unsigned char IMG_BORDER_RIGHT_SMW[];
