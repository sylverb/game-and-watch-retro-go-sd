#include "main.h"

/* The superblock is only meaningful for the FrogFS (flash) build; SD builds
 * read assets from FatFs and never mount FrogFS. */
#if SD_CARD == 0

#include "gw_layout_superblock.h"
#include "gw_linker.h"
#include "gw_ofw.h"
#include "crc32.h"

/* Lands in .rodata (included in gw_retro_go_intflash.bin) so a host can locate
 * it by scanning for GNW_LAYOUT_MAGIC. `used` keeps it even if LTO/GC is on.
 * crc32 = 0 in a plain build → CRC check fails → fall back to linker defaults,
 * which equal the patched values for the default offset (so an unpatched build
 * behaves exactly as before). */
__attribute__((used, aligned(4)))
const GnwLayoutSuperblock g_layout_superblock = {
    .magic           = GNW_LAYOUT_MAGIC,
    .version         = GNW_LAYOUT_VERSION,
    .struct_size     = (uint16_t)sizeof(GnwLayoutSuperblock),
    .frogfs_offset   = (uint32_t)&__EXTFLASH_OFFSET__,
    .frogfs_length   = 0u,
    .extflash_size   = 0u,
    .reserved_offset = 0u,
    .flags           = GNW_LAYOUT_FLAG_FROGFS_OFFSET,
    .crc32           = 0u,
};

bool gw_layout_valid(void)
{
    static int cached = -1;
    if (cached < 0) {
        const GnwLayoutSuperblock *sb = &g_layout_superblock;
        bool ok = (sb->magic == GNW_LAYOUT_MAGIC)
               && (sb->version <= GNW_LAYOUT_VERSION)
               && (sb->struct_size >= 32u)
               && (crc32_le(0u, (const unsigned char *)sb, 0x1Cu) == sb->crc32);
        cached = ok ? 1 : 0;
    }
    return cached != 0;
}

uint32_t gw_layout_frogfs_addr(void)
{
    const GnwLayoutSuperblock *sb = &g_layout_superblock;
    if (gw_layout_valid() && (sb->flags & GNW_LAYOUT_FLAG_FROGFS_OFFSET))
        return 0x90000000u + sb->frogfs_offset;
    return (uint32_t)&__EXTFLASH_START__;
}

uint32_t gw_layout_reserved_size(void)
{
    const GnwLayoutSuperblock *sb = &g_layout_superblock;
    if (gw_layout_valid() && (sb->flags & GNW_LAYOUT_FLAG_RESERVED_OFFSET))
        return sb->reserved_offset;
    return get_ofw_extflash_size();
}

#else
typedef int gw_layout_superblock_sd_stub; /* avoid empty translation unit */
#endif /* SD_CARD == 0 */
