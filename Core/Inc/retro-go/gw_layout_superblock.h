/*
 * Layout superblock — a tiny, magic-located struct compiled into any FrogFS
 * firmware (SD_CARD=0; bank-agnostic — present in bank-1 and bank-2 builds) that
 * makes the FrogFS asset location (and optional extflash geometry) patchable in a
 * prebuilt binary, so ONE binary serves any extflash offset/size.
 *
 * A plain `make` build bakes frogfs_offset from the linker's __EXTFLASH_OFFSET__
 * and leaves crc32 = 0 → the firmware's CRC check fails and it falls back to the
 * linker defaults (== today's behavior). A host patcher (browser app or
 * patch_superblock.py) locates this struct by magic, writes the fields, and
 * recomputes crc32; the firmware then honors the overrides. Never bricks: any
 * invalid/absent superblock degrades to exactly the unpatched behavior.
 *
 * See docs/BINARY_PATCHING.md (in the gnw-web-builder repo).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GNW_LAYOUT_MAGIC    0x424C5747u  /* "GWLB" — LE bytes 47 57 4C 42 */
#define GNW_LAYOUT_VERSION  2u

/* flags bits: each marks "this override field is authoritative" (so a real 0 is
 * distinguishable from "unset → use runtime default"). */
#define GNW_LAYOUT_FLAG_FROGFS_OFFSET    (1u << 0)
#define GNW_LAYOUT_FLAG_EXTFLASH_SIZE    (1u << 1)
#define GNW_LAYOUT_FLAG_RESERVED_OFFSET  (1u << 2)
#define GNW_LAYOUT_FLAG_LITTLEFS_LENGTH  (1u << 3)

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;            /* 0x00  GNW_LAYOUT_MAGIC */
    uint16_t version;          /* 0x04  GNW_LAYOUT_VERSION */
    uint16_t struct_size;      /* 0x06  sizeof(GnwLayoutSuperblock) == 36 */
    uint32_t frogfs_offset;    /* 0x08  FrogFS base = 0x90000000 + frogfs_offset */
    uint32_t frogfs_length;    /* 0x0C  packed image size (0 = unknown) */
    uint32_t extflash_size;    /* 0x10  total extflash bytes (override; 0 = OSPI_GetFlashSize) */
    uint32_t reserved_offset;  /* 0x14  bottom-reserved bytes (override; 0 = get_ofw_extflash_size) */
    uint32_t littlefs_length;  /* 0x18  LittleFS partition bytes (override; 0 = linker default) */
    uint32_t flags;            /* 0x1C  GNW_LAYOUT_FLAG_* */
    uint32_t crc32;            /* 0x20  crc32_le(0, this, 0x20) */
} GnwLayoutSuperblock;          /* 36 bytes */
#pragma pack(pop)

/* volatile: host-patched in flash AFTER build, so the compiler must NOT fold field
 * reads to the build-time initializer (that made gw_layout_valid() compare against a
 * baked crc32==0 and always fail). Every accessor reads through volatile. */
extern const volatile GnwLayoutSuperblock g_layout_superblock;

/* True iff magic + version + struct_size + crc32 all validate. */
bool gw_layout_valid(void);

/* FrogFS XiP base: 0x90000000 + frogfs_offset when valid + override set;
 * otherwise the linker default (uint32_t)&__EXTFLASH_START__. */
uint32_t gw_layout_frogfs_addr(void);

/* Bytes reserved at the bottom of extflash: superblock override when valid +
 * set, otherwise get_ofw_extflash_size() (bank-1 OFW metadata). */
uint32_t gw_layout_reserved_size(void);

/* LittleFS partition (writable saves region; grows DOWN from the top of extflash).
 * top   = 0x90000000 + extflash_size  (superblock/runtime), else linker __FILESYSTEM_END__.
 * bytes = littlefs_length             (superblock override),  else linker
 *         (__FILESYSTEM_END__ - __FILESYSTEM_START__). The host derives both from
 *         the probe-detected chip size + the FrogFS image it built. */
uint32_t gw_layout_littlefs_top(void);
uint32_t gw_layout_littlefs_size(void);
