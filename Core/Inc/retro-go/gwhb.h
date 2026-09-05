/*
 * Universal Homebrew Header (GWHB)
 *
 * Homebrew binaries live under /homebrews/*.bin (one launcher tab; covers
 * stay under /covers/homebrew/). The on-disk container is versioned like
 * CORE, but the meta is much smaller: identity + RAM_EMU load sizes + an
 * optional embedded JPEG cover for coverflow.
 *
 * File layout (all little-endian):
 *
 *   offset 0   "GWHB" magic (4 bytes)
 *   offset 4   header_version  u16  (== GWHB_META_VERSION for this struct)
 *   offset 6   header_length   u16  (== sizeof(gwhb_meta_t) + optional
 *                                    cover bytes immediately after meta)
 *   offset 8   gwhb_meta_t
 *   ...        optional cover JPEG (cover_offset/cover_size; 0 = absent)
 *   8+header_length  payload: code_size bytes linked at __RAM_EMU_START__
 *                    (entry trampoline at payload offset 0). Firmware zeroes
 *                    bss_size bytes after the payload.
 *
 * Legacy (pre-meta) binaries used a fixed 64-byte header with entry at
 * offset 64 and no BSS assist. The loader still accepts them when
 * header_length == 0 (see run_gwhb_homebrew()).
 *
 * Assets that do not fit in RAM_EMU (zelda3.ro, *_assets.dat, …) stay as
 * sibling files on the SD card; the homebrew loads them via the ABI.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GWHB_MAGIC 0x42485747u /* 'GWHB' little-endian */

#define GWHB_HEADER_MIN_SIZE 8u
#define GWHB_META_VERSION ((uint16_t)1u)

/* Legacy fixed-size prefix (header_length == 0). Entry was at offset 64. */
#define GWHB_LEGACY_HEADER_SIZE 64u

typedef struct {
    /* Firmware ABI this binary was built against (same checks as CORE /
     * the legacy GWHB header). */
    uint32_t required_abi_version;
    uint32_t required_abi_min_size;

    uint32_t flags; /* reserved; 0 today */

    /* RAM_EMU segment 0: payload bytes after the header envelope, then BSS. */
    uint32_t code_size;
    uint32_t bss_size;

    /* Optional coverflow JPEG (same format as /covers/.../*.img), absolute
     * offset from the start of the file. cover_size 0 → absent. Launcher
     * prefers /covers/homebrew/<stem>.img when present, else this blob.
     * Must fit the cover cache (COVER_SIZE, currently 10 KiB) AND decode
     * within COVER_MAX_WIDTH x COVER_MAX_HEIGHT (186x100) — larger JPEGs
     * overflow the HW scratch. */
    uint32_t cover_offset;
    uint32_t cover_size;

    /* Browser / Info title (NUL-terminated). Empty → use the filename stem. */
    char display_name[32];

    /* Semantic X.Y.Z shown in pause → Info (0.0.0 = unset). */
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t version_patch;
    uint8_t reserved0;

    uint8_t reserved[32];
} gwhb_meta_t;

_Static_assert(sizeof(gwhb_meta_t) == 96, "gwhb_meta_t must be exactly 96 bytes");

#ifdef __cplusplus
}
#endif
