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
 * width(u16) + height(u16) + packed 1bpp rows), referenced by the
 * pad_logo and header_logo offset+size pairs below (offsets are absolute
 * from the start of the file, so a prober can fseek+fread them directly).
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

#define GNW_CORE_META_VERSION ((uint16_t)2u)

typedef struct {
    /* Same sizes as retro_emulator_t (rg_emulators.h) — copied verbatim
     * into the dynamically-registered tab. */
    char system_name[32];
    char dirname[16];
    char extensions[32];

    /* Firmware ABI this core was built against. Checked the same way as
     * gwhb_header_t: required_abi_version <= GW_FIRMWARE_ABI_VERSION and
     * required_abi_min_size <= sizeof(g_firmware_abi) at runtime. */
    uint32_t required_abi_version;
    uint32_t required_abi_min_size;

    /* Size of the loadable payload (code+data, from file offset 0 of the
     * payload) and of the BSS region the firmware must zero right after
     * it in RAM_EMU, both computed by the packaging tool from the core's
     * own linked ELF. */
    uint32_t code_size;
    uint32_t bss_size;

    /* Optional pad/console logo blobs, packed 1bpp retro_logo_image data
     * (see bitmaps.h / tools/png_to_logo.py), stored inline in this file.
     * Offsets are absolute from the start of the .bin; size 0 means "no
     * logo" (tab falls back to RG_LOGO_EMPTY). */
    uint32_t pad_logo_offset;
    uint32_t pad_logo_size;
    uint32_t header_logo_offset;
    uint32_t header_logo_size;

    uint32_t flags;

    uint8_t reserved[32];
} gnw_core_meta_t;

#ifdef __cplusplus
}
#endif
