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
    RG_LOGO_RGO = 0,
    RG_LOGO_RGW,
//    RG_LOGO_FLASH,
    RG_LOGO_GNW,
    // Headers
    RG_LOGO_HEADER_SG1000,
    RG_LOGO_HEADER_COL,
    RG_LOGO_HEADER_GB,
    RG_LOGO_HEADER_GBC,
    RG_LOGO_HEADER_GG,
    RG_LOGO_HEADER_NES,
    RG_LOGO_HEADER_PCE,
    RG_LOGO_HEADER_SMS,
    RG_LOGO_HEADER_GW,
    RG_LOGO_HEADER_MSX,
    RG_LOGO_HEADER_GEN,
    RG_LOGO_HEADER_WSV,
    RG_LOGO_HEADER_A2600,
    RG_LOGO_HEADER_A7800,
    RG_LOGO_HEADER_AMSTRAD,
    RG_LOGO_HEADER_HOMEBREW,
    RG_LOGO_HEADER_TAMA,
    RG_LOGO_HEADER_PKMINI,
    RG_LOGO_HEADER_LYNX,
    // Pads
    RG_LOGO_PAD_SG1000,
    RG_LOGO_PAD_COL,
    RG_LOGO_PAD_GB,
    RG_LOGO_PAD_GG,
    RG_LOGO_PAD_NES,
    RG_LOGO_PAD_PCE,
    RG_LOGO_PAD_SMS,
    RG_LOGO_PAD_GW,
    RG_LOGO_PAD_MSX,
    RG_LOGO_PAD_GEN,
    RG_LOGO_PAD_WSV,
    RG_LOGO_PAD_A2600,
    RG_LOGO_PAD_A7800,
    RG_LOGO_PAD_AMSTRAD,
    RG_LOGO_PAD_SNES,
    RG_LOGO_PAD_TAMA,
    RG_LOGO_PAD_PKMINI,
    RG_LOGO_PAD_LYNX,
    // Logos
    RG_LOGO_COLECO,
    RG_LOGO_NINTENDO,
    RG_LOGO_SEGA,
    RG_LOGO_PCE,
    RG_LOGO_MICROSOFT,
    RG_LOGO_WATARA,
    RG_LOGO_ATARI,
    RG_LOGO_AMSTRAD,
    RG_LOGO_TAMA,
    // PICO-8 (appended last to not shift any existing enum values)
    RG_LOGO_HEADER_PICO8,
};

void odroid_overlay_draw_logo(uint16_t x_pos, uint16_t y_pos, int16_t logo_idx, uint16_t color);
void rg_reset_logo_buffers();
retro_logo_image *rg_get_logo(int16_t logo_index);

extern const retro_logo_image logo_rgo;
//extern const retro_logo_image logo_flash;
extern const retro_logo_image logo_rgw;
extern const retro_logo_image logo_gnw;

extern const retro_logo_image header_sg1000;
extern const retro_logo_image header_col;
extern const retro_logo_image header_gb;
extern const retro_logo_image header_gbc;
extern const retro_logo_image header_gg;
extern const retro_logo_image header_nes;
extern const retro_logo_image header_pce;
extern const retro_logo_image header_sms;
extern const retro_logo_image header_gw;
extern const retro_logo_image header_msx;
extern const retro_logo_image header_wsv;
extern const retro_logo_image header_gen;
extern const retro_logo_image header_a2600;
extern const retro_logo_image header_a7800;
extern const retro_logo_image header_amstrad;
extern const retro_logo_image header_zelda3;
extern const retro_logo_image header_smw;
extern const retro_logo_image header_homebrew;
extern const retro_logo_image header_tama;
extern const retro_logo_image header_pkmini;
extern const retro_logo_image header_lynx;
extern const retro_logo_image header_pico8;

extern const retro_logo_image pad_sg1000;
extern const retro_logo_image pad_col;
extern const retro_logo_image pad_gb;
extern const retro_logo_image pad_gg;
extern const retro_logo_image pad_nes;
extern const retro_logo_image pad_pce;
extern const retro_logo_image pad_sms;
extern const retro_logo_image pad_gw;
extern const retro_logo_image pad_msx;
extern const retro_logo_image pad_wsv;
extern const retro_logo_image pad_gen;
extern const retro_logo_image pad_a2600;
extern const retro_logo_image pad_a7800;
extern const retro_logo_image pad_amstrad;
extern const retro_logo_image pad_snes;
extern const retro_logo_image pad_tama;
extern const retro_logo_image pad_pkmini;
extern const retro_logo_image pad_lynx;

extern const retro_logo_image logo_coleco;
extern const retro_logo_image logo_nintendo;
extern const retro_logo_image logo_sega;
extern const retro_logo_image logo_pce;
extern const retro_logo_image logo_microsoft;
extern const retro_logo_image logo_watara;
extern const retro_logo_image logo_atari;
extern const retro_logo_image logo_amstrad;
extern const retro_logo_image logo_tama;


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

