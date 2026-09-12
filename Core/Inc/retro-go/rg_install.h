/*
 * /data/INSTALL — what is installed on this card.
 *
 * A tool that can only see the storage (a browser installer writing an SD card,
 * say) has no way to ask the device what firmware it is running: the ABI struct
 * and the version string live in internal flash, which is not on the card. This
 * file is the firmware's answer, written by the firmware itself so it is right
 * however the device was flashed — the web installer, gnwmanager, or `make
 * flash` all end up with a correct marker on the next boot.
 *
 * It is checked at boot and rewritten whenever it disagrees with the running
 * build. That same check is what invalidates the flash cache: see
 * rg_install_check() for why the two are one operation.
 *
 * Binary, in the house style of persistent_config_t (odroid_settings.c) and
 * rg_core_config_file_t: magic, version, fields, trailing crc32 computed over
 * the whole struct with the crc32 field zeroed. Not JSON — the device needs no
 * parser for this, and a user poking at the card is not invited to edit it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RG_INSTALL_MAGIC   0x4E494752u /* 'RGIN', LE bytes 52 47 49 4E */
#define RG_INSTALL_VERSION 1u

/* Where the firmware keeps its own files. Must stay in step with
 * ODROID_BASE_PATH_CONFIG (Core/Inc/porting/config.h). */
#define RG_INSTALL_FILE ODROID_BASE_PATH_CONFIG "/INSTALL"

/* rg_install_file_t.storage */
#define RG_INSTALL_STORAGE_FLASH 0u
#define RG_INSTALL_STORAGE_SD    1u

/* Version string field width. GIT_TAG is stored verbatim — prefix included — so
 * this field byte-compares against the release manifest's gitTag with no
 * normalisation on either side. The full display string is 34 characters today
 * ("Retro-Go SD v1.4.1-117-gcae346199+"); 48 leaves room for a longer describe
 * suffix without silently truncating the identity of the running build. */
#define RG_INSTALL_TAG_MAX 48

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;             /* RG_INSTALL_MAGIC */
    uint8_t  version;           /* RG_INSTALL_VERSION */
    uint8_t  bank;              /* 1 | 2 — INTFLASH_BANK */
    uint8_t  storage;           /* RG_INSTALL_STORAGE_* */
    uint8_t  reserved;
    uint32_t abi_version;       /* GW_FIRMWARE_ABI_VERSION */
    uint32_t abi_size;          /* sizeof(gw_firmware_abi_t) */
    uint16_t core_meta_version; /* GNW_CORE_META_VERSION */
    uint16_t reserved2;
    char     git_tag[RG_INSTALL_TAG_MAX];
    uint32_t installed_at;      /* unix seconds, 0 when the RTC is not set */
    uint32_t crc32;             /* crc32_le over this struct with crc32 == 0 */
} rg_install_file_t;
#pragma pack(pop)

/* Read /data/INSTALL and compare it against this build. When it is missing,
 * corrupt, or describes different firmware: drop the stale flash cache and
 * write a fresh marker.
 *
 * Call once at boot, after the filesystem is mounted and before anything can
 * reach the flash allocator. Returns true if a rewrite happened. */
bool rg_install_check(void);

#ifdef __cplusplus
}
#endif
