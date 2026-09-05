/*
 * Metadata embedded in an external "core" binary (/cores/<system>.bin) so
 * the launcher can discover classic emulator cores dynamically instead of
 * linking every system into the firmware ELF.
 *
 * This struct is the "header data" block of the generic CORE/CORI
 * container already used by load_core_bin_with_header() (see
 * rg_emulators.c): magic("CORE") + header_version(u16) + header_length(u16)
 * + header_data[header_length] + payload. When header_version ==
 * GNW_CORE_META_VERSION, header_data starts with exactly this struct,
 * followed by the optional pad/header logo blobs (raw retro_logo_image:
 * width(u16) + height(u16) + packed 1bpp rows) for each entry in
 * `systems[]`, referenced by that entry's offset+size pairs (offsets are
 * absolute from the start of the file, so a prober can fseek+fread them
 * directly), followed by the payload bytes for each entry in `segments[]`
 * back to back, in order.
 *
 * v3: a single core binary can now describe UP TO GNW_CORE_MAX_SYSTEMS
 * launcher tabs (e.g. PC Engine HuCard + PC Engine CD from one `pce.bin`,
 * dispatched at runtime by the core itself via ACTIVE_FILE->ext — exactly
 * like the old compile-time build did) and UP TO GNW_CORE_MAX_SEGMENTS
 * independently-loaded code+bss blobs, each targeting a different fixed
 * memory region (see gnw_core_region_t) instead of always landing in
 * RAM_EMU — e.g. PC Engine's CPU-hot code runs from ITCM, and a LUT8
 * core can place a cold/overflow blob in the 150 KiB LCD bonus.
 *
 * Backwards-compat rules mirror gw_firmware_abi_t: only ADD fields inside
 * "reserved" (shrinking it), never reorder/remove/resize existing fields.
 * Bump GNW_CORE_META_VERSION only for an incompatible layout change.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GNW_CORE_META_VERSION ((uint16_t)3u)

#define GNW_CORE_MAX_SEGMENTS 4u
#define GNW_CORE_MAX_SYSTEMS  4u

/* Target memory region for a gnw_core_segment_t's fixed load address (see
 * run_dynamic_core() in rg_emulators.c).
 *
 *   RAM_EMU — always segment[0], entry trampoline at offset 0
 *   ITCM    — optional extra segment(s) for CPU-hot code (ld/gnw_itcm_core.ld)
 *   RAM_UC  — optional extra segment in the LUT8 LCD bonus
 *             (ld/gnw_ram_uc_core.ld). Firmware switches the LTDC to LUT8
 *             before loading this region.
 *
 * AHB/DTCM are firmware dynamic pools (malloc / dtc_*), not load targets —
 * they are not part of this enum. */
typedef enum {
    GNW_CORE_REGION_RAM_EMU = 0,
    GNW_CORE_REGION_ITCM    = 1,
    GNW_CORE_REGION_RAM_UC  = 2,
} gnw_core_region_t;

/* How the launcher should populate a system's ROM browser list. GNW_PARSE_ROM
 * is the default (one file = one game, filtered by extension — see
 * scan_folder_cb()). GNW_PARSE_CDROM is the .cue-based folder scan PC Engine
 * CD already needed in the old compile-time build (see
 * emulator_scan_cdrom_folder() / cdrom_collapse_game_dir()): a game is a
 * folder containing track files, collapsed to a single .cue browser row. */
typedef enum {
    GNW_PARSE_ROM   = 0,
    GNW_PARSE_CDROM = 1,
} gnw_parse_type_t;

/* One independently-loaded code+bss blob. code_size bytes are read from the
 * file into this region's fixed base address (see run_dynamic_core()),
 * followed by bss_size zeroed bytes. For ITCM segments, the firmware also
 * reserves code_size+bss_size via itc_malloc right after loading so the
 * core's later itc_* allocations never collide with the fixed segment —
 * see docs/PICO8_EXTERNAL_MODULE.md's "ITCM Back-Page Allocation". For
 * RAM_UC segments, the same span is carved out of lcd_get_bonus_pool()
 * via lcd_claim_bonus_pool(). */
typedef struct {
    uint32_t region;    /* gnw_core_region_t */
    uint32_t code_size;
    uint32_t bss_size;
} gnw_core_segment_t;

/* One launcher tab. Multiple systems in one core binary all share the same
 * code (core_path + segments[]) but get their own dirname/extensions/logos
 * and browse strategy — the core dispatches its own behavior at runtime
 * (typically via ACTIVE_FILE->ext), same as the old compile-time build's
 * per-system add_emulator() calls that all pointed at one emulator core. */
typedef struct {
    char system_name[32];
    char dirname[16];
    char extensions[32];

    uint32_t parse_type; /* gnw_parse_type_t */

    /* Optional pad/console logo blobs, packed 1bpp retro_logo_image data
     * (see bitmaps.h / tools/png_to_logo.py), stored inline in this file.
     * Offsets are absolute from the start of the .bin; size 0 means "no
     * logo" (tab falls back to RG_LOGO_EMPTY). */
    uint32_t pad_logo_offset;
    uint32_t pad_logo_size;
    uint32_t header_logo_offset;
    uint32_t header_logo_size;

    /* Cheat file extension under /cheats/<dirname>/ (no leading '.'),
     * e.g. "ggcodes", "pceplus", "mcf". Empty string = this system does
     * not support cheat files (launcher skips probing). Taken from the
     * front of the former reserved[16] — still ABI-compatible. */
    char cheat_ext[8];
    uint8_t reserved[8];
} gnw_core_system_t;

typedef struct {
    /* Firmware ABI this core was built against. Checked the same way as
     * gwhb_meta_t: required_abi_version <= GW_FIRMWARE_ABI_VERSION and
     * required_abi_min_size <= sizeof(g_firmware_abi) at runtime. */
    uint32_t required_abi_version;
    uint32_t required_abi_min_size;

    uint32_t flags;

    uint32_t segments_count; /* 1..GNW_CORE_MAX_SEGMENTS; segments[0] is
                               * always GNW_CORE_REGION_RAM_EMU and carries
                               * the entry trampoline at offset 0. */
    gnw_core_segment_t segments[GNW_CORE_MAX_SEGMENTS];

    uint32_t systems_count; /* 1..GNW_CORE_MAX_SYSTEMS */
    gnw_core_system_t systems[GNW_CORE_MAX_SYSTEMS];

    /* Core identity shown in the in-game pause → Info dialog.
     * version_*: semantic X.Y.Z without the leading 'v' (0..255 each).
     * core_name: short pack name (e.g. "sms", "pce") — NUL-terminated.
     * Taken from the former reserved[32]; layout size unchanged.
     * All-zero version + empty name means "unset" (old / unversioned bin). */
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t version_patch;
    char core_name[24];

    uint8_t reserved[5];
} gnw_core_meta_t;

#ifdef __cplusplus
}
#endif
