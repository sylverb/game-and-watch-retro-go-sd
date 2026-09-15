/*
 * Universal Homebrew Header (GWHB)
 *
 * Homebrew binaries live under /homebrews/*.bin (one launcher tab; covers
 * stay under /covers/homebrew/). The on-disk container is versioned like
 * CORE.
 *
 * File layout (all little-endian):
 *
 *   offset 0   "GWHB" magic (4 bytes)
 *   offset 4   header_version  u16  (== GWHB_META_VERSION)
 *   offset 6   header_length   u16  (== sizeof(gwhb_meta_t) + optional
 *                                    cover bytes immediately after meta)
 *   offset 8   gwhb_meta_t
 *   ...        optional cover JPEG (cover_offset/cover_size; 0 = absent)
 *   8+header_length  payload: segments[0].code_size bytes, then
 *                    segments[1].code_size, ... back to back — same
 *                    multi-segment model as CORE (gnw_core_segment_t /
 *                    load_gnw_segments()). segments[0] is always RAM_EMU
 *                    with the entry trampoline at offset 0.
 *
 * Assets that do not fit in RAM_EMU stay as sibling files on the SD card;
 * the homebrew loads them via the ABI.
 */
#pragma once

#include <stdint.h>

#include "gnw_core_meta.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GWHB_MAGIC 0x42485747u /* 'GWHB' little-endian */

#define GWHB_HEADER_MIN_SIZE 8u
#define GWHB_META_VERSION ((uint16_t)1u)

typedef struct {
    /* Firmware ABI this binary was built against (same checks as CORE). */
    uint32_t required_abi_version;
    uint32_t required_abi_min_size;

    uint32_t flags; /* reserved; 0 today */

    /* Same segment table as gnw_core_meta_t — load path is shared. */
    uint32_t segments_count; /* 1..GNW_CORE_MAX_SEGMENTS; [0] = RAM_EMU */
    gnw_core_segment_t segments[GNW_CORE_MAX_SEGMENTS];

    /* Optional coverflow JPEG (same format as /covers/.../*.img), absolute
     * offset from the start of the file. cover_size 0 → absent. Launcher
     * prefers /covers/homebrew/<stem>.img when present, else this blob.
     * Must fit the cover cache (COVER_SIZE, currently 10 KiB) AND decode
     * within COVER_MAX_WIDTH x COVER_MAX_HEIGHT (186x100). */
    uint32_t cover_offset;
    uint32_t cover_size;

    /* Browser / Info title (NUL-terminated). Empty → use the filename stem. */
    char display_name[32];

    /* Semantic X.Y.Z shown in pause → Info (0.0.0 = unset). */
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t version_patch;
    uint8_t reserved0;

    uint8_t reserved[16];
} gwhb_meta_t;

/* 4+4+4+4 + 4*12 + 4+4 + 32 + 4 + 16 = 124 */
_Static_assert(sizeof(gwhb_meta_t) == 124, "gwhb_meta_t must be exactly 124 bytes");

#ifdef __cplusplus
}
#endif
