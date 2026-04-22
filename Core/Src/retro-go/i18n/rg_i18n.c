#pragma GCC diagnostic ignored "-Wstack-usage="
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#pragma GCC diagnostic ignored "-Wchar-subscripts"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#if !defined (INCLUDED_ZH_CN)
#define INCLUDED_ZH_CN 0
#endif
#if !defined (INCLUDED_ZH_TW)
#define INCLUDED_ZH_TW 0
#endif
#if !defined (INCLUDED_JA_JP)
#define INCLUDED_JA_JP 0
#endif
#if !defined (INCLUDED_KO_KR)
#define INCLUDED_KO_KR 0
#endif
#if !defined (INCLUDED_ES_ES)
#define INCLUDED_ES_ES 1
#endif
#if !defined (INCLUDED_PT_PT)
#define INCLUDED_PT_PT 1
#endif
#if !defined (INCLUDED_FR_FR)
#define INCLUDED_FR_FR 1
#endif
#if !defined (INCLUDED_IT_IT)
#define INCLUDED_IT_IT 1
#endif
#if !defined (INCLUDED_DE_DE)
#define INCLUDED_DE_DE 1
#endif
#if !defined (INCLUDED_DE_DE)
#define INCLUDED_DE_DE 1
#endif
#if !defined (INCLUDED_RU_RU)
#define INCLUDED_RU_RU 1
#endif


#if !defined (BIG_BANK)
#define BIG_BANK 1
#endif

#include "rg_i18n.h"
#include "rg_i18n_lang.h"
#include "gw_lcd.h"
#include "main.h"
#include "odroid_system.h"
#include "odroid_overlay.h"

static uint8_t curr_font = 0;

#if SD_CARD == 1
#define FONT_DATA_CP1251_GREYBEARD __attribute__((section(".sd_fonts_cp1251_greybeard")))
#define FONT_DATA_CP1251_SANS_SERIF_BOLD __attribute__((section(".sd_fonts_cp1251_sans_serif_bold")))
#define FONT_DATA_CP1251_SANS_SERIF __attribute__((section(".sd_fonts_cp1251_sans_serif")))
#define FONT_DATA_CP1251_SERIF_BOLD __attribute__((section(".sd_fonts_cp1251_serif_bold")))
#define FONT_DATA_CP1251_SERIF __attribute__((section(".sd_fonts_cp1251_serif")))

#define FONT_DATA_CP1252_GREYBEARD __attribute__((section(".sd_fonts_cp1252_greybeard")))
#define FONT_DATA_CP1252_HAEBERLI12 __attribute__((section(".sd_fonts_cp1252_haeberli12")))
#define FONT_DATA_CP1252_ROCK12 __attribute__((section(".sd_fonts_cp1252_rock12")))
#define FONT_DATA_CP1252_SANS_SERIF_BOLD __attribute__((section(".sd_fonts_cp1252_sans_serif_bold")))
#define FONT_DATA_CP1252_SANS_SERIF __attribute__((section(".sd_fonts_cp1252_sans_serif")))
#define FONT_DATA_CP1252_SERIF_BOLD __attribute__((section(".sd_fonts_cp1252_serif_bold")))
#define FONT_DATA_CP1252_SERIF_CJK __attribute__((section(".sd_fonts_cp1252_serif_cjk")))
#define FONT_DATA_CP1252_SERIF __attribute__((section(".sd_fonts_cp1252_serif")))
#define FONT_DATA_CP1252_UNBALANCED __attribute__((section(".sd_fonts_cp1252_unbalanced")))

#elif ((BIG_BANK == 1) && (EXTFLASH_SIZE <= 16*1024*1024))
#define FONT_DATA_CP1251_GREYBEARD
#define FONT_DATA_CP1251_SANS_SERIF_BOLD
#define FONT_DATA_CP1251_SANS_SERIF
#define FONT_DATA_CP1251_SERIF_BOLD
#define FONT_DATA_CP1251_SERIF

#define FONT_DATA_CP1252_GREYBEARD
#define FONT_DATA_CP1252_HAEBERLI12
#define FONT_DATA_CP1252_ROCK12
#define FONT_DATA_CP1252_SANS_SERIF_BOLD
#define FONT_DATA_CP1252_SANS_SERIF
#define FONT_DATA_CP1252_SERIF_BOLD
#define FONT_DATA_CP1252_SERIF_CJK
#define FONT_DATA_CP1252_SERIF
#define FONT_DATA_CP1252_UNBALANCED
#else
#define FONT_DATA_CP1251_GREYBEARD __attribute__((section(".extflash_font")))
#define FONT_DATA_CP1251_SANS_SERIF_BOLD __attribute__((section(".extflash_font")))
#define FONT_DATA_CP1251_SANS_SERIF __attribute__((section(".extflash_font")))
#define FONT_DATA_CP1251_SERIF_BOLD __attribute__((section(".extflash_font")))
#define FONT_DATA_CP1251_SERIF __attribute__((section(".extflash_font")))

#define FONT_DATA_CP1252_GREYBEARD __attribute__((section(".extflash_font")))
#define FONT_DATA_CP1252_HAEBERLI12 __attribute__((section(".extflash_font")))
#define FONT_DATA_CP1252_ROCK12 __attribute__((section(".extflash_font")))
#define FONT_DATA_CP1252_SANS_SERIF_BOLD __attribute__((section(".extflash_font")))
#define FONT_DATA_CP1252_SANS_SERIF __attribute__((section(".extflash_font")))
#define FONT_DATA_CP1252_SERIF_BOLD __attribute__((section(".extflash_font")))
#define FONT_DATA_CP1252_SERIF_CJK __attribute__((section(".extflash_font")))
#define FONT_DATA_CP1252_SERIF __attribute__((section(".extflash_font")))
#define FONT_DATA_CP1252_UNBALANCED __attribute__((section(".extflash_font")))
#endif

#if ((BIG_BANK == 1) && (EXTFLASH_SIZE <= 16*1024*1024)) || SD_CARD == 1
#define LANG_DATA
#else
#define LANG_DATA __attribute__((section(".extflash_emu_data")))
#endif

#include "fonts/font_cp1252_Serif.h"

#if !SINGLE_FONT
#include "fonts/font_cp1252_Serif_Bold.h"
#include "fonts/font_cp1252_Serif_CJK.h"
#include "fonts/font_cp1252_Sans_serif.h"
#include "fonts/font_cp1252_Sans_serif_Bold.h"
#include "fonts/font_cp1252_Greybeard.h"
#include "fonts/font_cp1252_Unbalanced.h"
#include "fonts/font_cp1252_rock12.h"
#include "fonts/font_cp1252_haeberli12.h"
#endif

#if SD_CARD == 0
#if INCLUDED_JA_JP == 1
#include "fonts/font_cp932_ja_jp.h"
#endif
#if INCLUDED_ZH_CN == 1
#include "fonts/font_cp936_zh_cn.h"
#endif
#if INCLUDED_KO_KR == 1
#include "fonts/font_cp949_ko_kr.h"
#endif
#if INCLUDED_ZH_TW == 1
#include "fonts/font_cp950_zh_tw.h"
#endif
#endif // SD_CARD == 0

#if INCLUDED_RU_RU == 1
#include "fonts/font_cp1251_Serif.h"
#if !SINGLE_FONT
#include "fonts/font_cp1251_Serif_Bold.h"
#include "fonts/font_cp1251_Sans_serif.h"
#include "fonts/font_cp1251_Sans_serif_Bold.h"
#include "fonts/font_cp1251_Greybeard.h"
#endif
#endif

// Font cache management
#define CACHE_SIZE 256  // Stores up to 128 character widths (adjustable)
#define FONT_CACHE_SIZE 5*1024  // 5KB cache for raw font data

typedef struct {
    uint32_t codepoint; // Unicode codepoint
    uint8_t width;      // Cached width of the character
    bool valid;         // If entry is valid
    uint8_t *char_data; // Pointer to the character data
} FontEntry;

static FontEntry *font_cache = NULL/*[CACHE_SIZE]*/;
static uint16_t cache_index = 0; // Index of the next cache entry
static uint8_t *font_data_cache = NULL/*[FONT_CACHE_SIZE]*/; // Raw font data buffer
static uint16_t cache_data_index = 0; // Index of the next cache data entry

// Fallback glyph for unknown/unsupported codepoints � : U+FFFD (replacement character)
// Pre-rasterized at 11px wide, 12 rows, LSB-first (NotoSansCJK size 12)
#define UNKNOWN_GLYPH_WIDTH 11
static const uint8_t unknown_glyph_data[] = {
    0x20, 0x00,  // row  0  ·····█·····
    0x70, 0x00,  // row  1  ····███····
    0x88, 0x00,  // row  2  ···█···█···
    0x7C, 0x01,  // row  3  ··█████·█··
    0x7E, 0x03,  // row  4  ·██████·██·
    0xBF, 0x07,  // row  5  ██████·████
    0xDE, 0x03,  // row  6  ·████·████·
    0xDC, 0x01,  // row  7  ··███·███··
    0xF8, 0x00,  // row  8  ···█████···
    0x50, 0x00,  // row  9  ····█·█····
    0x20, 0x00,  // row 10  ·····█·····
    0x00, 0x00,  // row 11  ···········
};
static FontEntry unknown_glyph_entry = {
    .codepoint = 0xFFFD,
    .width     = UNKNOWN_GLYPH_WIDTH,
    .valid     = true,
    .char_data = (uint8_t *)unknown_glyph_data,
};

// Latin fonts
const char *font_files[] = {
    "/fonts/cp1252_serif.bin",
    "/fonts/cp1252_serif_bold.bin",
    "/fonts/cp1252_serif_cjk.bin",
    "/fonts/cp1252_sans_serif.bin",
    "/fonts/cp1252_sans_serif_bold.bin",
    "/fonts/cp1252_greybeard.bin",
    "/fonts/cp1252_unbalanced.bin",
    "/fonts/cp1252_rock12.bin",
    "/fonts/cp1252_haeberli12.bin"
};

// Cyrillic fonts
const char *font_files_cp1251[] = {
    "/fonts/cp1251_serif.bin",
    "/fonts/cp1251_serif_bold.bin",
    "/fonts/cp1251_serif.bin",
    "/fonts/cp1251_sans_serif.bin",
    "/fonts/cp1251_sans_serif_bold.bin",
    "/fonts/cp1251_greybeard.bin"
    "/fonts/cp1251_serif_bold.bin",
    "/fonts/cp1251_serif_bold.bin",
    "/fonts/cp1251_serif_bold.bin",
};

const char *get_font_file(uint32_t codepoint) {
    if (codepoint >= 0x0400 && codepoint <= 0x04FF) {
        return font_files_cp1251[curr_font];        // Cyrillic (CP1251)
    } else if (codepoint >= 0x0370 && codepoint <= 0x03FF) {
        return "fonts/unicode_greek.bin";           // Greek and Coptic
    } else if (codepoint >= 0x2200 && codepoint <= 0x22FF) {
        return "fonts/unicode_math_operators.bin";  // Mathematical Operators
    } else if (codepoint >= 0x25A0 && codepoint <= 0x25FF) {
        return "fonts/unicode_geometric.bin";       // Geometric Shapes
    } else if (codepoint >= 0x2600 && codepoint <= 0x26FF) {
        return "fonts/unicode_misc_symbols.bin";    // Miscellaneous Symbols
    } else if (codepoint >= 0x3000 && codepoint <= 0x303F) {
        return "fonts/unicode_cjk_punct.bin";       // CJK Symbols and Punctuation
    } else if (codepoint >= 0x3040 && codepoint <= 0x309F) {
        return "fonts/unicode_hiragana.bin";        // Hiragana
    } else if (codepoint >= 0x30A0 && codepoint <= 0x30FF) {
        return "fonts/unicode_katakana.bin";        // Katakana
    } else if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) {
        return "fonts/unicode_cjk.bin";             // CJK Unified Ideographs
    } else if (codepoint >= 0xAC00 && codepoint <= 0xD7A3) {
        return "fonts/unicode_hangul.bin";          // Hangul Syllables
    } else if (codepoint >= 0xFF00 && codepoint <= 0xFFEF) {
        return "fonts/unicode_fullwidth.bin";       // Halfwidth and Fullwidth Forms
    }
    return font_files[curr_font];  // CP1252 latin font
}

void invalidate_overlapping_entries(uint8_t *start_ptr, uint16_t length) {
    uint8_t *end_ptr = start_ptr + length;

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (font_cache[i].valid) {
            uint8_t *entry_start = font_cache[i].char_data;
            uint8_t *entry_end = entry_start + (12 * ((font_cache[i].width + 7) / 8));

            // Check if entry's data is inside the overwritten range
            if (!(entry_end <= start_ptr || entry_start >= end_ptr)) {
                printf("Invalidating entry %d\n", i);
                font_cache[i].valid = false;  // Invalidate the entry
            }
        }
    }
}

void init_font_cache() {
    memset(font_cache, 0, CACHE_SIZE * sizeof(FontEntry));
    memset(font_data_cache, 0, FONT_CACHE_SIZE * sizeof(uint8_t));
    cache_index = 0;
    cache_data_index = 0;
}

FontEntry *get_font_data(uint32_t codepoint) {
    FILE *file;

    if (font_cache == NULL) {
        font_cache = (FontEntry *)malloc(CACHE_SIZE * sizeof(FontEntry));
        font_data_cache = (uint8_t *)malloc(FONT_CACHE_SIZE * sizeof(uint8_t));
        init_font_cache();
    }

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (font_cache[i].valid && font_cache[i].codepoint == codepoint) {
            return &font_cache[i];
        }
    }
//    printf("Font cache miss for codepoint: %c adding @ %d\n", (char)codepoint, cache_index);
    // Not found, load from SD and add to cache
    FontEntry *entry = &font_cache[cache_index];
    entry->codepoint = codepoint;
    entry->valid = true;

    const char *filename = get_font_file(codepoint);
    uint16_t char_offset = codepoint;
    file = fopen(filename, "rb");
    if (!file) {
        // Font file missing or SD unavailable: return replacement character
        return &unknown_glyph_entry;
    }

    bool is_fixed_width = false;
    uint32_t varwidth_N = 256; // block size for variable-width files (header = N*3 bytes)
                               // default 256 = CP1252/CP1251 files (header = 0x300)

    if (codepoint >= 0x0410 && codepoint <= 0x044F) {
        // Cyrillic: CP1251 variable-width file, N=256
        char_offset = codepoint - 0x0410 + 0xC0;
        varwidth_N = 256;
    } else if (codepoint >= 0x0370 && codepoint <= 0x03FF) {
        // Greek and Coptic: variable-width, N=144
        char_offset = codepoint - 0x0370;
        varwidth_N = 144;
    } else if (codepoint >= 0x2200 && codepoint <= 0x22FF) {
        // Mathematical Operators: variable-width, N=256
        char_offset = codepoint - 0x2200;
        varwidth_N = 256;
    } else if (codepoint >= 0x25A0 && codepoint <= 0x25FF) {
        // Geometric Shapes: variable-width, N=96
        char_offset = codepoint - 0x25A0;
        varwidth_N = 96;
    } else if (codepoint >= 0x2600 && codepoint <= 0x26FF) {
        // Miscellaneous Symbols: variable-width, N=256
        char_offset = codepoint - 0x2600;
        varwidth_N = 256;
    } else if (codepoint >= 0x3000 && codepoint <= 0x303F) {
        // CJK Symbols and Punctuation: fixed-width 12px, N=64
        char_offset = codepoint - 0x3000;
        entry->width = 12;
        is_fixed_width = true;
    } else if (codepoint >= 0x3040 && codepoint <= 0x309F) {
        // Hiragana: fixed-width 12px, N=96
        char_offset = codepoint - 0x3040;
        entry->width = 12;
        is_fixed_width = true;
    } else if (codepoint >= 0x30A0 && codepoint <= 0x30FF) {
        // Katakana: fixed-width 12px, N=96
        char_offset = codepoint - 0x30A0;
        entry->width = 12;
        is_fixed_width = true;
    } else if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) {
        // CJK Unified Ideographs: fixed-width 12px
        char_offset = codepoint - 0x4E00;
        entry->width = 12;
        is_fixed_width = true;
    } else if (codepoint >= 0xAC00 && codepoint <= 0xD7A3) {
        // Hangul Syllables: fixed-width 12px
        char_offset = codepoint - 0xAC00;
        entry->width = 12;
        is_fixed_width = true;
    } else if (codepoint >= 0xFF00 && codepoint <= 0xFFEF) {
        // Halfwidth and Fullwidth Forms: variable-width, N=240
        char_offset = codepoint - 0xFF00;
        varwidth_N = 240;
    } else if (codepoint >= 0x100) {
        // Codepoint not handled by any known block: return U+FFFD replacement character
        fclose(file);
        return &unknown_glyph_entry;
    }

    // Read glyph width for variable-width files
    if (!is_fixed_width) {
        fseek(file, char_offset, SEEK_SET);
        fread(&(entry->width), 1, sizeof(uint8_t), file);
    }

    uint32_t char_data_offset;
    if (is_fixed_width) {
        // Dense fixed-width array: offset = index * bytes_per_glyph
        char_data_offset = char_offset * ((entry->width + 7) / 8) * i18n_get_text_height();
    } else {
        // Variable-width header: widths[N] + offsets[N*2] + pixdata
        // data offset field is at: N + char_offset*2
        uint16_t char_data_offset_u16;
        fseek(file, varwidth_N + char_offset * 2, SEEK_SET);
        fread(&char_data_offset_u16, 1, sizeof(uint16_t), file);
        char_data_offset = char_data_offset_u16;
        char_data_offset += varwidth_N * 3; // skip widths[N] + offsets[N*2]
    }

    uint16_t data_length = i18n_get_text_height() * ((entry->width + 7) / 8);

    if (cache_data_index + data_length > FONT_CACHE_SIZE) {
        cache_data_index = 0;
    }

    // Invalidate overwritten cache data
    invalidate_overlapping_entries(&font_data_cache[cache_data_index], data_length);

    entry->char_data = &font_data_cache[cache_data_index];
    fseek(file, char_data_offset, SEEK_SET);
    fread(&font_data_cache[cache_data_index], 1, data_length, file);
//    printf("Font data: 0x%lx (is_fixed_width = %d)\n",codepoint, is_fixed_width);
//    for (int i = 0; i < data_length; i++) {
//        printf("0x%02X ", font_data_cache[cache_data_index + i]);
//    }
//    printf("\n");
    cache_data_index += data_length;

    fclose(file);
    cache_index = (cache_index + 1) % CACHE_SIZE;
    return entry;
}

// end font cache management

const int gui_font_count = FONT_COUNT;

void set_font(uint8_t font_index) {
    if (font_index != curr_font) {
        curr_font = font_index;
        memset(font_cache, 0, CACHE_SIZE * sizeof(FontEntry));
        cache_index = 0;
        cache_data_index = 0;
    }
}

uint8_t get_font() {
    return curr_font;
}

#if SD_CARD == 0
#if SINGLE_FONT
const char *gui_fonts[9] = {
    font_cp1252_Serif, font_cp1252_Serif, font_cp1252_Serif,
    font_cp1252_Serif, font_cp1252_Serif, font_cp1252_Serif,
    font_cp1252_Serif, font_cp1252_Serif, font_cp1252_Serif,
    };
#else
const char *gui_fonts[9] = {
    font_cp1252_Serif,    font_cp1252_Serif_Bold,    font_cp1252_Serif_CJK,
    font_cp1252_Sans_serif,    font_cp1252_Sans_serif_Bold,    font_cp1252_Greybeard,
    font_cp1252_Unbalanced,    font_cp1252_rock12,    font_cp1252_haeberli12,
    };
#endif

#if INCLUDED_JA_JP == 1
/*const char *ja_jp_fonts[9] = {
    font_cp932_ja_jp,    font_cp932_ja_jp,    font_cp932_ja_jp,
    font_cp932_ja_jp,    font_cp932_ja_jp,    font_cp932_ja_jp,
    font_cp932_ja_jp,    font_cp932_ja_jp,    font_cp932_ja_jp,
    };*/
#endif
#if INCLUDED_ZH_CN == 1
const char *zh_cn_fonts[9] = {
    font_cp936_zh_cn,    font_cp936_zh_cn,    font_cp936_zh_cn,
    font_cp936_zh_cn,    font_cp936_zh_cn,    font_cp936_zh_cn,
    font_cp936_zh_cn,    font_cp936_zh_cn,    font_cp936_zh_cn,
    };
#endif
#if INCLUDED_KO_KR == 1
const char *ko_kr_fonts[9] = {
    font_cp949_ko_kr,    font_cp949_ko_kr,    font_cp949_ko_kr,
    font_cp949_ko_kr,    font_cp949_ko_kr,    font_cp949_ko_kr,
    font_cp949_ko_kr,    font_cp949_ko_kr,    font_cp949_ko_kr,
    };
#endif
#if INCLUDED_ZH_TW == 1
const char *zh_tw_fonts[9] = {
    font_cp950_zh_tw,    font_cp950_zh_tw,    font_cp950_zh_tw,
    font_cp950_zh_tw,    font_cp950_zh_tw,    font_cp950_zh_tw,
    font_cp950_zh_tw,    font_cp950_zh_tw,    font_cp950_zh_tw,
    };
#endif
#if INCLUDED_RU_RU == 1
#if SINGLE_FONT
const char *cp1251_fonts[9] = {
    font_cp1251_Serif, font_cp1251_Serif, font_cp1251_Serif,
    font_cp1251_Serif, font_cp1251_Serif, font_cp1251_Serif,
    font_cp1251_Serif, font_cp1251_Serif, font_cp1251_Serif,
    };
#else
const char *cp1251_fonts[9] = {
    font_cp1251_Serif,    font_cp1251_Serif_Bold,    font_cp1251_Serif,
    font_cp1251_Sans_serif,    font_cp1251_Sans_serif_Bold,    font_cp1251_Greybeard,
    font_cp1251_Serif_Bold,    font_cp1251_Serif_Bold,    font_cp1251_Serif_Bold,
    };
#endif
#endif
#endif


#include "rg_i18n_en_us.c"

#if INCLUDED_ES_ES == 1
#include "rg_i18n_es_es.c"
#endif

#if INCLUDED_PT_PT == 1
#include "rg_i18n_pt_pt.c"
#endif

#if INCLUDED_FR_FR == 1
#include "rg_i18n_fr_fr.c"
#endif

#if INCLUDED_IT_IT == 1
#include "rg_i18n_it_it.c"
#endif

#if INCLUDED_DE_DE == 1
#include "rg_i18n_de_de.c"
#endif

#if INCLUDED_ZH_CN == 1
#include "rg_i18n_zh_cn.c"
#endif

#if INCLUDED_ZH_TW == 1
#include "rg_i18n_zh_tw.c"
#endif

#if INCLUDED_KO_KR == 1
#include "rg_i18n_ko_kr.c"
#endif

#if INCLUDED_JA_JP == 1
#include "rg_i18n_ja_jp.c"
#endif

#if INCLUDED_RU_RU == 1
#include "rg_i18n_ru_ru.c"
#endif

/* overlay_buffer removed — all drawing goes directly to LCD framebuffer */

const lang_t *gui_lang[] = {
    &lang_en_us,
#if INCLUDED_ES_ES == 1
    &lang_es_es,
#endif
#if INCLUDED_PT_PT == 1
    &lang_pt_pt,
#endif
#if INCLUDED_FR_FR == 1
    &lang_fr_fr,
#endif
#if INCLUDED_IT_IT == 1
    &lang_it_it,
#endif
#if INCLUDED_DE_DE == 1
    &lang_de_de,
#endif
#if INCLUDED_RU_RU == 1
    &lang_ru_ru,
#endif
#if INCLUDED_ZH_CN == 1
    &lang_zh_cn,
#endif
#if INCLUDED_ZH_TW == 1
    &lang_zh_tw,
#endif
#if INCLUDED_KO_KR == 1
    &lang_ko_kr,
#endif
#if INCLUDED_JA_JP == 1
    &lang_ja_jp,
#endif
};

lang_t *curr_lang = &lang_en_us;
const int gui_lang_count = sizeof(gui_lang) / sizeof(*gui_lang);

int utf8_decode(const char *str, uint32_t *codepoint) {
    if (!str || !codepoint) return 0;

    unsigned char c = str[0];
    if (c < 0x80) {
        *codepoint = c;
        return 1; // Single-byte ASCII
    } else if ((c & 0xE0) == 0xC0) {
        *codepoint = ((c & 0x1F) << 6) | (str[1] & 0x3F);
        return 2; // Two-byte sequence
    } else if ((c & 0xF0) == 0xE0) {
        *codepoint = ((c & 0x0F) << 12) | ((str[1] & 0x3F) << 6) | (str[2] & 0x3F);
        return 3; // Three-byte sequence
    } else if ((c & 0xF8) == 0xF0) {
        *codepoint = ((c & 0x07) << 18) | ((str[1] & 0x3F) << 12) | ((str[2] & 0x3F) << 6) | (str[3] & 0x3F);
        return 4; // Four-byte sequence
    }

    printf("Invalid UTF-8 byte sequence\n");
    return 0;
}

#if SD_CARD == 1
int i18n_get_char_width(uint32_t codepoint) {
    FontEntry *entry = get_font_data(codepoint);
    return entry ? entry->width : 0;
}
#else
int i18n_get_char_width(uint32_t codepoint)
{
    char *font;
    if (codepoint < 0x100) {
        font = gui_fonts[curr_font];
        return font[codepoint]; // Basic Latin (ASCII) characters
    } else if (codepoint >= 0x410 && codepoint <= 0x44F) {
        codepoint = codepoint - 0x410 + 0xC0; // 0x400 utf8 codepoint is 0x80 in cp1251 font
        font = cp1251_fonts[curr_font];
        return font[codepoint]; // Cyrillic characters
    } else if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) {
        return 12; // CJK Unified Ideographs
    } else if (codepoint >= 0x1100 && codepoint <= 0x11FF) {
        return 12; // Hangul Jamo
    } else {
        return 8; // Default width for other Unicode characters
    }
}
#endif

int i18n_get_text_height()
{
    return 12;
}

int i18n_get_text_width(const char *text)
{
    if (!text) return 0;

    int width = 0;
    uint32_t codepoint;
    int bytes;
    while (*text) {
        bytes = utf8_decode(text, &codepoint);
        if (bytes == 0) break; // Invalid sequence
        width += i18n_get_char_width(codepoint);
        text += bytes;
    }

    return width;
};

int i18n_get_text_lines(const char *text, const int fix_width)
{
    if (text == NULL || text[0] == '\0') return 0;

    int w = 0;             // Current line width
    int lines = 1;         // Number of lines
    uint32_t codepoint;    // Decoded UTF-8 codepoint
    int bytes;             // Number of bytes in the UTF-8 sequence
    const char *current = text;

    while (*current) {
        if (*current == '\n') {
            // Newline resets line width
            lines++;
            w = 0;
            current++;
            continue;
        }

        bytes = utf8_decode(current, &codepoint);
        if (bytes == 0) break; // Invalid UTF-8 sequence

        // Determine character width based on the Unicode code point
        int char_width = i18n_get_char_width(codepoint);

        // Check if the character fits in the current line
        if ((fix_width - w) < char_width) {
            lines++;
            w = 0;
        }

        w += char_width;
        current += bytes; // Advance to the next character
    }

    return lines;
}

/* odroid_overlay_read_screen_rect removed — direct framebuffer access instead */

#if 0
int i18n_draw_text_line(uint16_t x_pos, uint16_t y_pos, uint16_t width, const char *text, uint16_t color, uint16_t color_bg, char transparent, const lang_t* lang)
{
    if (text == NULL || text[0] == 0)
        return 0;
    int font_height = 12;
    int x_offset = 0;
    char realtxt[161];
    uint8_t cc;
    bool is_cjk = IS_CJK(lang);
    char *font = gui_fonts[curr_font];
    char *extra_font = gui_fonts[curr_font];
    if ((lang->extra_font != NULL) && (lang->extra_font[curr_font] != NULL))
            extra_font = lang->extra_font[curr_font];
    
    if (transparent)
        odroid_overlay_read_screen_rect(x_pos, y_pos, width, font_height);
    else
    {
        for (int x = 0; x < width; x++)
            for (int y = 0; y < font_height; y++)
                overlay_buffer[x + y * width] = color_bg;
    }
    int w = i18n_get_text_width(text, lang);
    sprintf(realtxt, "%.*s", 160, text);
    bool dByte = false;
    if (w > width)
    {
        w = 0;
        int i = 0;
        while (w < width)
        {
            dByte = false;
            if (realtxt[i] > 0x80)
            {
                if (is_cjk)
                {
                    if ((lang->codepage == 932) && (realtxt[i] > 0xa0) && (realtxt[i] < 0xe0))
                        w += 6;
                    else if (realtxt[i] < 0xa1)
                        w += font[realtxt[i]];
                    {
                        w += 12;
                        dByte = true;
                        i++;
                    }
                }
                else
                    w += extra_font[realtxt[i]];
            }
            else
                w += font[realtxt[i]];
            i++;
        }
        realtxt[i - (dByte ? 2 : 1)] = 0;
        // paint end point
        overlay_buffer[width * (font_height - 3) - 1] = get_darken_pixel(color, 80);
        overlay_buffer[width * (font_height - 3) - 3] = get_darken_pixel(color, 80);
        overlay_buffer[width * (font_height - 3) - 6] = get_darken_pixel(color, 80);
    };

    int text_len = strlen(realtxt);

    for (int i = 0; i < text_len; i++)
    {
        uint8_t c1 = realtxt[i];
        bool ofont = ((c1 > 0x80) && (lang->codepage == 932)) || ((c1 > 0xA0) && is_cjk); 
        if (! ofont)
        {
            char *draw_font = (c1 >= 0x80) ? (is_cjk ? font :extra_font) : font;
            int cw = draw_font[c1]; // width;
            if ((x_offset + cw) > width)
                break;
            if (cw != 0)
            {
                int d_pos = draw_font[c1 * 2 + 0x100] + draw_font[c1 * 2 + 0x101] * 0x100; // data pos
                int line_bytes = (cw + 7) / 8;
                for (int y = 0; y < font_height; y++)
                {
                    uint32_t *pixels_data = (uint32_t *)&(draw_font[0x300 + d_pos + y * line_bytes]);
                    int offset = x_offset + (width * y);

                    for (int x = 0; x < cw; x++)
                    {
                        if (pixels_data[0] & (1 << x))
                            overlay_buffer[offset + x] = color;
                    }
                }
            }
            x_offset += cw;
        }
        else
        {
            uint32_t location = 0;
            uint8_t c2 = text[i + 1];
            bool half = false;
            if (lang->codepage == 950) // zh_tw
                location = ((c1 - 0xa1) * 157 + ((c2 > 0xa0) ? (c2 - 0x62) : (c2 - 0x40))) * 24;
            else if (lang->codepage == 932) // ja_jp
            {
                half = (c1 > 0xa0) && (c1 < 0xe0);
                location = half ? ((c1 - 0xa1) * 12) : ((((c1 <= 0x9F) ? (c1 - 0x81) : (c1 - 0x41)) * 188 + ((c2 >= 0x80) ? (c2 - 0x41) : (c2 - 0x40)) + 32)  * 24 - 12);
            }
            else
                location = ((c1 - 0xa1) * 94 + (c2 - 0xa1)) * 24;
            for (int y = 0; y < font_height; y++)
            { // height :12;
                int offset = x_offset + (width * y);
                cc = extra_font[location + y * (half ? 1 : 2)];

                if (cc & 0x04)
                    overlay_buffer[offset + 5] = color;
                if (cc & 0x08)
                    overlay_buffer[offset + 4] = color;
                if (cc & 0x10)
                    overlay_buffer[offset + 3] = color;
                if (cc & 0x20)
                    overlay_buffer[offset + 2] = color;
                if (cc & 0x40)
                    overlay_buffer[offset + 1] = color;
                if (cc & 0x80)
                    overlay_buffer[offset + 0] = color;
                
                if (!half)
                {
                    if (cc & 0x01)
                        overlay_buffer[offset + 7] = color;
                    if (cc & 0x02)
                        overlay_buffer[offset + 6] = color;

                    cc = extra_font[location + y * 2 + 1];

                    if (cc & 0x10)
                        overlay_buffer[offset + 11] = color;
                    if (cc & 0x20)
                        overlay_buffer[offset + 10] = color;
                    if (cc & 0x40)
                        overlay_buffer[offset + 9] = color;
                    if (cc & 0x80)
                        overlay_buffer[offset + 8] = color;
                }
            }
            x_offset += half ? 6 : 12;
            if (! half)
                i++;
        }
    }
    odroid_display_write(x_pos, y_pos, width, font_height, overlay_buffer);
    return font_height;
}
#endif

int i18n_draw_text_line(uint16_t x_pos, uint16_t y_pos, uint16_t width, const char *text, uint16_t color, uint16_t color_bg, char transparent)
{
    if (text == NULL || text[0] == 0)
        return 0;
    int font_height = 12;
    int x_offset = 0;
    uint16_t *fb = lcd_get_active_buffer();

    /* Fill background (skip if transparent — existing pixels are the background) */
    if (!transparent) {
        for (int y = 0; y < font_height; y++) {
            uint16_t *row = &fb[(y_pos + y) * ODROID_SCREEN_WIDTH + x_pos];
            for (int x = 0; x < width; x++)
                row[x] = color_bg;
        }
    }

    /* Render glyphs directly to framebuffer */
    uint32_t codepoint;
    int bytes;
    while (*text) {
        bytes = utf8_decode(text, &codepoint);
        if (bytes == 0) break;
        text += bytes;

        FontEntry *entry = get_font_data(codepoint);
        if (entry == NULL)
            continue;
        int cw = entry->width;
        if ((x_offset + cw) > width)
            break;
        if (cw != 0 && entry->char_data != NULL) {
            int line_bytes = (cw + 7) / 8;
            for (int y = 0; y < font_height; y++) {
                uint32_t *pixels_data = (uint32_t *)&(entry->char_data[y * line_bytes]);
                uint16_t *row = &fb[(y_pos + y) * ODROID_SCREEN_WIDTH + x_pos + x_offset];
                for (int x = 0; x < cw; x++) {
                    if (pixels_data[0] & (1 << x))
                        row[x] = color;
                }
            }
        }
        x_offset += cw;
    }

    return font_height;
}

int i18n_draw_text(uint16_t x_pos, uint16_t y_pos, uint16_t width, uint16_t max_height, const char *text, uint16_t color, uint16_t color_bg, char transparent)
{
    int text_len = 1;
    int height = 0;

    if (text == NULL || text[0] == 0)
        text = " ";

    text_len = strlen(text);
    if (x_pos < 0)
        x_pos = ODROID_SCREEN_WIDTH + x_pos;

    if (width < 1)
        width = i18n_get_text_width(text);

    if (width > (ODROID_SCREEN_WIDTH - x_pos))
        width = (ODROID_SCREEN_WIDTH - x_pos);

    uint32_t codepoint;
    int line_len = 160; // min width is 2, max 160 char everline;
    char buffer_utf8[line_len + 1];
    int buffer_utf8_pos = 0;

    for (int pos = 0; pos < text_len;)
    {
        if ((height + i18n_get_text_height()) > max_height)
            break;
        // Build a single line buffer
        int w = 0;
        int bytes;
        buffer_utf8_pos = 0;
        while (*text)
        {
            bytes = utf8_decode(text, &codepoint);
            if (bytes == 0) break; // Invalid sequence
            int chr_width = i18n_get_char_width(codepoint);

            memcpy(buffer_utf8 + buffer_utf8_pos, text, bytes);

            if ((codepoint == '\n') ||
                (codepoint == 0) ||
                ((width - w) < chr_width))
            {
                break;
            }
            buffer_utf8_pos += bytes;
            text += bytes;
            w += chr_width;
        }
        buffer_utf8[buffer_utf8_pos] = 0;

        height += i18n_draw_text_line(x_pos, y_pos + height, width, buffer_utf8, color, color_bg, transparent);
        pos += strlen(buffer_utf8);
    }

    return height;
}
