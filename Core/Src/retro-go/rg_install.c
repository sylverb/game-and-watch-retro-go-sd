/* /data/INSTALL — see Core/Inc/retro-go/rg_install.h. */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "odroid_system.h"
#include "rg_install.h"
#include "rg_rtc.h"
#include "rg_storage.h"
#include "crc32.h"
#include "gittag.h"
#include "gw_firmware_abi.h"
#include "gnw_core_meta.h"

/* The flash ROM cache. Keyed to the device UID and the flash write base
 * (Core/Src/gw_flash_alloc.c), so it survives a reboot but not a reflash that
 * moves the base — at which point its entries point at addresses that hold
 * something else now. gw_flash_alloc's own load_metadata() catches the cases it
 * can see; it cannot see a firmware change that leaves the base where it was. */
#define RG_INSTALL_FLASH_CACHE ODROID_BASE_PATH_SAVES "/flashcachedata.bin"

static void install_fill(rg_install_file_t *out)
{
    memset(out, 0, sizeof(*out));

    out->magic             = RG_INSTALL_MAGIC;
    out->version           = RG_INSTALL_VERSION;
    out->bank              = (uint8_t)INTFLASH_BANK;
    out->storage           = (SD_CARD == 1) ? RG_INSTALL_STORAGE_SD
                                            : RG_INSTALL_STORAGE_FLASH;
    out->abi_version       = g_firmware_abi.version;
    out->abi_size          = g_firmware_abi.size;
    out->core_meta_version = GNW_CORE_META_VERSION;

    /* Verbatim, prefix and all — this is the string a tool compares against the
     * manifest to decide whether the installed firmware is the one it expects. */
    strncpy(out->git_tag, GIT_TAG, sizeof(out->git_tag) - 1);
    out->git_tag[sizeof(out->git_tag) - 1] = '\0';
}

static uint32_t install_crc(const rg_install_file_t *f)
{
    rg_install_file_t copy = *f;
    copy.crc32 = 0;
    return crc32_le(0, (unsigned char *)&copy, sizeof(copy));
}

/* True when the file on disk describes exactly this firmware. installed_at is
 * deliberately not compared — it records when the marker was written, not what
 * was written, so comparing it would rewrite the file on every boot. */
static bool install_matches(const rg_install_file_t *disk,
                            const rg_install_file_t *want)
{
    return disk->magic == want->magic &&
           disk->version == want->version &&
           disk->bank == want->bank &&
           disk->storage == want->storage &&
           disk->abi_version == want->abi_version &&
           disk->abi_size == want->abi_size &&
           disk->core_meta_version == want->core_meta_version &&
           strncmp(disk->git_tag, want->git_tag, sizeof(disk->git_tag)) == 0;
}

static bool install_load(rg_install_file_t *out)
{
    FILE *file = fopen(RG_INSTALL_FILE, "rb");
    if (!file)
        return false;

    size_t n = fread(out, 1, sizeof(*out), file);
    fclose(file);
    if (n != sizeof(*out))
        return false;

    return out->crc32 == install_crc(out);
}

static void install_write(rg_install_file_t *f)
{
    rg_storage_mkdir(ODROID_BASE_PATH_CONFIG);

    f->installed_at = (uint32_t)GW_GetUnixTime();
    f->crc32 = install_crc(f);

    FILE *file = fopen(RG_INSTALL_FILE, "wb");
    if (!file) {
        printf("install: cannot write %s\n", RG_INSTALL_FILE);
        return;
    }
    fwrite(f, 1, sizeof(*f), file);
    fclose(file);
}

bool rg_install_check(void)
{
    rg_install_file_t want;
    install_fill(&want);

    rg_install_file_t disk;
    if (install_load(&disk) && install_matches(&disk, &want))
        return false;

    /* Either this card has never seen this firmware or it has seen a different
     * one. The ROM cache in external flash was filled by whatever was here
     * before; its metadata may still pass gw_flash_alloc's own checks (same
     * device, same base) while pointing into flash the new firmware lays out
     * differently. Drop it and let it refill — it is a cache, so the only cost
     * is re-caching the next game.
     *
     * The firmware does this rather than the installer because an installer
     * cannot always reach the card: a browser without the File System Access
     * API cannot delete a file, and a device flashed by gnwmanager or `make
     * flash` was never touched by an installer at all. */
    printf("install: firmware changed, dropping %s\n", RG_INSTALL_FLASH_CACHE);
    remove(RG_INSTALL_FLASH_CACHE);

    install_write(&want);
    return true;
}
