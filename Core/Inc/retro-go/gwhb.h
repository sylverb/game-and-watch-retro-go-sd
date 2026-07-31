/*
 * Universal Homebrew Header (GWHB)
 *
 * Fixed 64-byte prefix every out-of-tree homebrew binary must start with,
 * so it can run without any firmware-side dispatch-table entry, linker
 * overlay symbols, or appid.h enum: drop a .bin under /roms/homebrew/ and
 * it runs, as long as it starts with this header.
 *
 * The entry point is always at offset sizeof(gwhb_header_t) (64). New
 * fields are added to the reserved block using a 0-means-absent sentinel
 * (see name_table_offset/name_table_size below), rather than a struct
 * layout version, so old and new binaries can share one loader without it
 * needing to know which fields a given file's builder populated.
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
    uint32_t required_abi;          /* gw_firmware_abi_t version this binary needs */
    uint32_t required_abi_min_size; /* minimum acceptable sizeof(gw_firmware_abi_t) */

    /* Reserved, unused by the loader today; see file comment above. */
    uint32_t name_table_offset;
    uint32_t name_table_size;

    uint8_t  reserved[44];          /* zeroed; future fields carved from here, sentinel-gated */
} gwhb_header_t;

_Static_assert(sizeof(gwhb_header_t) == 64, "GWHB header must be exactly 64 bytes");
