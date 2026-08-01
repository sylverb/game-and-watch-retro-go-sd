#pragma GCC diagnostic ignored "-Wstack-usage="
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#pragma GCC diagnostic ignored "-Wchar-subscripts"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>  /* offsetof — used by i18n_load_language */
#include <stdbool.h>
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
#include "gw_malloc.h"

static uint8_t curr_font = 0;

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

#define LANG_DATA

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

static int utf8_decode(const char *str, uint32_t *codepoint);
static FontEntry *get_font_data(uint32_t codepoint);

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

static void invalidate_overlapping_entries(uint8_t *start_ptr, uint16_t length) {
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

static FontEntry *get_font_data(uint32_t codepoint) {
    FILE *file;

    if (font_cache == NULL) {
        font_cache = (FontEntry *)dtcm_malloc(CACHE_SIZE * sizeof(FontEntry));
        font_data_cache = (uint8_t *)dtcm_malloc(FONT_CACHE_SIZE * sizeof(uint8_t));
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
    entry->valid = false; // Mark valid only after glyph data is fully loaded

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
    entry->valid = true;
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

#if INCLUDED_NO_NB == 1
#include "rg_i18n_no_nb.c"
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

/* gui_lang[] removed — was an array of pointers to the per-language
 * lang_xx_xx static structs (each ~860 bytes of rodata + ~2 KB of
 * translated strings). Replaced by lang_metadata[] below for compile-
 * time descriptors (codepage, fn pointers, display name) and by
 * i18n_load_language() for the strings themselves (loaded from SD
 * at runtime).
 *
 * Once gui_lang[] is gone, the 9 non-en_us lang_xx_xx static structs
 * are unreferenced and the linker's --gc-sections drops them, freeing
 * ~18 KB of intflash. lang_en_us stays because it's the baked
 * fallback inside i18n_load_language(). */

lang_t *curr_lang = &lang_en_us;

/* ──── SD-backed i18n: runtime-loaded language strings ────────────────────
 *
 * Per-language string data lives in /lang/xx_xx.bin on the SD card (built
 * by tools/gen_i18n_bin.py). At app init we open the file for the active
 * language, malloc a small buffer, read the strings into it, then
 * populate the 215 `s_XXX` pointers in lang_active to point inside the
 * buffer. curr_lang is then set to &lang_active.
 *
 * Only one non-en_us language is kept in RAM at a time; switching
 * frees the previous buffer. en_us stays baked (lang_en_us) as a
 * guaranteed fallback when the SD card has no /lang/xx_xx.bin or the
 * file is corrupt.
 *
 * Binary format (matches tools/gen_i18n_bin.py):
 *   [0]            u32 magic = 'I18N' (0x4E383149)
 *   [4]            u16 version = 1
 *   [6]            u16 count
 *   [8]            u32 offsets[count]   (relative to start of strings)
 *   [8 + 4*count]  null-terminated UTF-8 strings, concatenated
 */
#define I18N_BIN_MAGIC   0x4E383149u
#define I18N_BIN_VERSION 1

typedef struct {
    uint32_t    codepage;
    const char *bin_path;       /* e.g. "/lang/de_de.bin" */
    const char *display_name;   /* shown in lang menu BEFORE .bin load */
    int (*fmt_Title_Date_Format)(char *outstr, const char *datefmt,
                                 uint16_t day, uint16_t month,
                                 const char *weekday, uint16_t hour,
                                 uint16_t minutes, uint16_t seconds);
    int (*fmtDate)(char *outstr, const char *datefmt,
                   uint16_t day, uint16_t month, uint16_t year,
                   const char *weekday);
    int (*fmtTime)(char *outstr, const char *timefmt,
                   uint16_t hour, uint16_t minutes, uint16_t seconds);
} lang_metadata_t;

/* The order here is the user-facing language index (persisted as
 * `lang` in odroid settings). Changing the order or removing an
 * entry shifts every saved selection, so prefer appending. */
static const lang_metadata_t lang_metadata[] = {
    /* en_us has bin_path = NULL because the baked lang_en_us struct in
     * rodata is always available; i18n_load_language() short-circuits
     * directly to it instead of doing a pointless SD read. The Makefile
     * also skips generating /lang/en_us.bin for the same reason. */
    { 1252, NULL, "English",
      en_us_fmt_Title_Date_Format, en_us_fmt_Date, en_us_fmt_Time },
#if INCLUDED_ES_ES == 1
    { 1252, "/lang/es_es.bin", "Spanish",
      es_es_fmt_Title_Date_Format, es_es_fmt_Date, es_es_fmt_Time },
#endif
#if INCLUDED_PT_PT == 1
    { 1252, "/lang/pt_pt.bin", "Portuguese",
      pt_pt_fmt_Title_Date_Format, pt_pt_fmt_Date, pt_pt_fmt_Time },
#endif
#if INCLUDED_FR_FR == 1
    { 1252, "/lang/fr_fr.bin", "French",
      fr_fr_fmt_Title_Date_Format, fr_fr_fmt_Date, fr_fr_fmt_Time },
#endif
#if INCLUDED_IT_IT == 1
    { 1252, "/lang/it_it.bin", "Italian",
      it_it_fmt_Title_Date_Format, it_it_fmt_Date, it_it_fmt_Time },
#endif
#if INCLUDED_DE_DE == 1
    { 1252, "/lang/de_de.bin", "Deutsch",
      de_de_fmt_Title_Date_Format, de_de_fmt_Date, de_de_fmt_Time },
#endif
#if INCLUDED_NO_NB == 1
    { 1252, "/lang/no_nb.bin", "Norwegian",
      no_nb_fmt_Title_Date_Format, no_nb_fmt_Date, no_nb_fmt_Time },
#endif
#if INCLUDED_RU_RU == 1
    { 1251, "/lang/ru_ru.bin", "Russian",
      ru_ru_fmt_Title_Date_Format, ru_ru_fmt_Date, ru_ru_fmt_Time },
#endif
#if INCLUDED_ZH_CN == 1
    {  936, "/lang/zh_cn.bin", "Simplified Chinese",
      zh_cn_fmt_Title_Date_Format, zh_cn_fmt_Date, zh_cn_fmt_Time },
#endif
#if INCLUDED_ZH_TW == 1
    {  950, "/lang/zh_tw.bin", "Traditional Chinese",
      zh_tw_fmt_Title_Date_Format, zh_tw_fmt_Date, zh_tw_fmt_Time },
#endif
#if INCLUDED_KO_KR == 1
    {  949, "/lang/ko_kr.bin", "Korean",
      ko_kr_fmt_Title_Date_Format, ko_kr_fmt_Date, ko_kr_fmt_Time },
#endif
#if INCLUDED_JA_JP == 1
    {  932, "/lang/ja_jp.bin", "Japanese",
      ja_jp_fmt_Title_Date_Format, ja_jp_fmt_Date, ja_jp_fmt_Time },
#endif
};

/* The 215 s_XXX fields in lang_t are contiguous `const char *` pointers
 * starting at &lang_t.s_LangUI (codepage precedes them, fn pointers
 * follow). Treating that region as a flat const-char-pointer array lets
 * the loader assign by index without naming each field. */
#define LANG_T_STRING_COUNT \
    ((offsetof(lang_t, fmt_Title_Date_Format) - offsetof(lang_t, s_LangUI)) \
     / sizeof(const char *))

static lang_t  lang_active;

/* Single-slot SD language strings. Only one non-en_us language is kept
 * in RAM at a time (~2–3 KB). en_us stays baked in flash (lang_en_us)
 * and needs no allocation. Switching languages frees the previous
 * buffer before installing the new one.
 *
 * Dialogs that capture curr_lang->s_XXX pointers at open time must
 * snapshot those strings (see odroid_overlay_dialog) — otherwise
 * browsing the language list would leave dangling labels.
 *
 * lang_failed_idx remembers an idx whose .bin failed to load so the
 * ~60 Hz redraw path does not re-open a missing file every frame. */
static char   *lang_strings_buf = NULL;
static int     lang_active_idx  = -1;
static int     lang_failed_idx  = -1;

static void i18n_free_active_strings(void)
{
    if (lang_strings_buf) {
        dtcm_free(lang_strings_buf);
        lang_strings_buf = NULL;
    }
}

/* Read /lang/xx_xx.bin for `idx` into a fresh buffer and install it as
 * the sole active language. On any error the previous language (if any)
 * is left untouched and false is returned. */
static bool i18n_load_from_sd(int idx)
{
    const lang_metadata_t *m = &lang_metadata[idx];
    if (!m->bin_path) return true;  /* en_us: nothing to load. */

    FILE *f = fopen(m->bin_path, "rb");
    if (!f) {
        fprintf(stderr, "i18n_load: cannot open '%s' (SD missing?) — using en_us\n",
                m->bin_path);
        return false;
    }

    uint32_t magic = 0;
    uint16_t version = 0, count = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != I18N_BIN_MAGIC) {
        fprintf(stderr, "i18n_load: '%s' bad magic 0x%08lx — using en_us\n",
                m->bin_path, (unsigned long)magic);
        fclose(f);
        return false;
    }
    if (fread(&version, 2, 1, f) != 1 || version != I18N_BIN_VERSION) {
        fprintf(stderr, "i18n_load: '%s' bad version %u — using en_us\n",
                m->bin_path, version);
        fclose(f);
        return false;
    }
    if (fread(&count, 2, 1, f) != 1 || count > LANG_T_STRING_COUNT + 4) {
        fprintf(stderr, "i18n_load: '%s' bad count %u (expected %u) — using en_us\n",
                m->bin_path, count, (unsigned)LANG_T_STRING_COUNT);
        fclose(f);
        return false;
    }

    uint32_t offsets[LANG_T_STRING_COUNT];
    const uint16_t to_read = count < LANG_T_STRING_COUNT ? count : LANG_T_STRING_COUNT;
    if (fread(offsets, 4, to_read, f) != to_read) {
        fprintf(stderr, "i18n_load: '%s' short read of offsets — using en_us\n",
                m->bin_path);
        fclose(f);
        return false;
    }
    if (count > LANG_T_STRING_COUNT)
        fseek(f, (count - LANG_T_STRING_COUNT) * 4L, SEEK_CUR);

    long header_end = ftell(f);
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    long strings_size = file_size - header_end;
    if (strings_size <= 0 || strings_size > 32 * 1024) {
        fprintf(stderr, "i18n_load: '%s' implausible strings size %ld — using en_us\n",
                m->bin_path, strings_size);
        fclose(f);
        return false;
    }
    fseek(f, header_end, SEEK_SET);

    char *buf = (char *)dtcm_malloc((size_t)strings_size);
    if (!buf) {
        fprintf(stderr, "i18n_load: '%s' OOM allocating %ld bytes — using en_us\n",
                m->bin_path, strings_size);
        fclose(f);
        return false;
    }
    if (fread(buf, 1, (size_t)strings_size, f) != (size_t)strings_size) {
        fprintf(stderr, "i18n_load: '%s' short read of strings — using en_us\n",
                m->bin_path);
        dtcm_free(buf);
        fclose(f);
        return false;
    }
    fclose(f);

    /* Commit: free the previous language, then point lang_active at the
     * new buffer. Populate from en_us first so any missing string keeps
     * the English fallback. */
    i18n_free_active_strings();
    lang_strings_buf = buf;

    memcpy(&lang_active, &lang_en_us, sizeof(lang_t));
    *(uint32_t *)&lang_active.codepage = m->codepage;
    *(void **)&lang_active.fmt_Title_Date_Format = (void *)m->fmt_Title_Date_Format;
    *(void **)&lang_active.fmtDate               = (void *)m->fmtDate;
    *(void **)&lang_active.fmtTime               = (void *)m->fmtTime;

    const char **dst = (const char **)&lang_active.s_LangUI;
    for (uint16_t i = 0; i < to_read; i++) {
        if (offsets[i] < (uint32_t)strings_size)
            dst[i] = buf + offsets[i];
    }
    lang_active_idx = idx;
    lang_failed_idx = -1;
    printf("i18n_load: '%s' loaded %u strings (%ld bytes)\n",
           m->bin_path, to_read, strings_size);
    return true;
}

/* Return a usable lang_t* for `idx`. The returned pointer is either
 * &lang_active (the single SD-loaded language) or &lang_en_us (baked
 * fallback when SD load failed, idx is en_us, or idx is out of range).
 *
 * At most one non-en_us language buffer is kept in RAM; switching
 * frees the previous one. Safe to call from the redraw loop — a
 * failed idx is not retried every frame. */
lang_t *i18n_load_language(int idx)
{
    const int n = (int)(sizeof(lang_metadata) / sizeof(*lang_metadata));
    if (idx < 0 || idx >= n) {
        fprintf(stderr, "i18n_load: idx=%d out of range, falling back to en_us\n", idx);
        return &lang_en_us;
    }
    const lang_metadata_t *m = &lang_metadata[idx];
    if (!m->bin_path) {
        /* en_us: rodata, no malloc, no SD touch. Drop any previously
         * loaded language so we never keep a stale buffer around. */
        i18n_free_active_strings();
        lang_active_idx = idx;
        lang_failed_idx = -1;
        return &lang_en_us;
    }

    /* Already the active language — nothing to do. */
    if (idx == lang_active_idx && lang_strings_buf)
        return &lang_active;

    /* Prior failure for this idx: do not hammer FatFs every redraw. */
    if (idx == lang_failed_idx)
        return &lang_en_us;

    if (!i18n_load_from_sd(idx)) {
        lang_failed_idx = idx;
        return &lang_en_us;
    }
    return &lang_active;
}


/* Public count of languages available at this build (replaces what
 * sizeof(gui_lang)/sizeof(*gui_lang) used to compute). */
const int gui_lang_count = (int)(sizeof(lang_metadata) / sizeof(*lang_metadata));

/* Native display name for the menu — usable BEFORE i18n_load_language
 * runs (no SD touch). Returns "?" for an out-of-range idx so the menu
 * doesn't crash on a corrupt setting. */
const char *i18n_lang_display_name(int idx)
{
    if (idx < 0 || idx >= gui_lang_count)
        return "?";
    return lang_metadata[idx].display_name;
}


static int utf8_decode(const char *str, uint32_t *codepoint) {
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

int i18n_get_char_width(uint32_t codepoint) {
    FontEntry *entry = get_font_data(codepoint);
    return entry ? entry->width : 0;
}

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

    lcd_pen_t fg = lcd_pen(color);
    lcd_pen_t bg = lcd_pen(color_bg);

    if (!transparent) {
        for (int y = 0; y < font_height; y++)
            lcd_pen_run(&bg, (y_pos + y) * ODROID_SCREEN_WIDTH + x_pos, width);
    }

    uint32_t codepoint;
    int bytes;
    while (*text) {
        bytes = utf8_decode(text, &codepoint);
        if (bytes == 0) break;
        text += bytes;
        FontEntry *entry = get_font_data(codepoint);
        if (entry == NULL) continue;
        int cw = entry->width;
        if ((x_offset + cw) > width) break;
        if (cw != 0 && entry->char_data != NULL) {
            int line_bytes = (cw + 7) / 8;
            for (int y = 0; y < font_height; y++) {
                uint32_t *pixels_data = (uint32_t *)&(entry->char_data[y * line_bytes]);
                int row_off = (y_pos + y) * ODROID_SCREEN_WIDTH + x_pos + x_offset;
                for (int x = 0; x < cw; x++) {
                    if (pixels_data[0] & (1 << x))
                        lcd_pen_set(&fg, row_off + x);
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
