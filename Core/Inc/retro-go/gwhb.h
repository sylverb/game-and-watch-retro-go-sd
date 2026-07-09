/*
 * Universal Homebrew Header (GWHB)
 *
 * Fixed 512-byte prefix every out-of-tree homebrew binary must start with,
 * so it can run without any firmware-side dispatch-table entry, linker
 * overlay symbols, or appid.h enum: drop a .bin under /roms/homebrew/ and
 * it runs, as long as it starts with this header.
 *
 * The entry point is always at offset sizeof(gwhb_header_t) (512), no
 * matter what header_version says, so future header_version bumps can add
 * fields out of the reserved block without ever moving the jump target
 * again.
 *
 * name_table_offset/name_table_size are not read or acted on by the
 * loader yet. They're reserved so a build can optionally point at a
 * variable-size, elsewhere-in-the-file table of (language, display name)
 * entries for a future menu/coverflow feature; 0/0 means absent, and
 * nothing currently falls back to it.
 */
#pragma once

#include <stdint.h>

#define GWHB_MAGIC 0x42485747u /* 'GWHB' little-endian */

typedef struct {
    uint32_t magic;                 /* GWHB_MAGIC */
    uint32_t header_version;        /* layout of this struct itself */
    uint32_t required_abi;          /* gw_firmware_abi_t version this binary needs */
    uint32_t required_abi_min_size; /* minimum acceptable sizeof(gw_firmware_abi_t) */
    uint32_t total_size;            /* whole file size, header included */

    /* Reserved, unused by the loader today; see file comment above. */
    uint32_t name_table_offset;
    uint32_t name_table_size;

    uint8_t  reserved[484];         /* zeroed; future header_version bumps carve fields from here */
} gwhb_header_t;

_Static_assert(sizeof(gwhb_header_t) == 512, "GWHB header must be exactly 512 bytes");
