#include "main.h"

#if SD_CARD == 0

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "ff.h"
#include "gw_linker.h"
#include "gw_layout_superblock.h"
#include "rg_frogfs.h"
#include "frogfs_format.h"

static frogfs_fs_t *s_frogfs;

/* Allocation-free replacements for frogfs_get_entry() / frogfs_get_name().
 *
 * Both upstream calls allocate, and both fail badly once an emulator core is
 * resident and the heap is nearly exhausted:
 *
 *   frogfs_get_entry() calls frogfs_get_path(), which calloc's PATH_MAX
 *   (1024) bytes purely to rebuild a path string for comparison. On failure
 *   it returns NULL and the caller strcmp()s against it unchecked, so the
 *   lookup reports "not found" instead of "out of memory" -- a font open
 *   then returns ENOENT and the glyph renders as the replacement character.
 *
 *   frogfs_get_name() malloc's seg_sz + 1 and memcpy's into it with no NULL
 *   check. Address 0 is live ITCM holding executable code on this part, so a
 *   failed malloc corrupts hot code rather than failing cleanly.
 *
 * These walk the memory-mapped image directly using the on-disk layout in
 * frogfs_format.h. Nothing below allocates. */

#define RG_FROGFS_MAGIC 0x474F5246u /* 'FROG', little-endian */

typedef struct {
    const frogfs_head_t *head;
    const frogfs_hash_t *hash;
    const void *root;
    uint32_t num_entries;
} rg_frogfs_image_t;

static bool rg_frogfs_image(rg_frogfs_image_t *img)
{
    const frogfs_head_t *head =
        (const frogfs_head_t *)(uintptr_t)gw_layout_frogfs_addr();

    if (!head || head->magic != RG_FROGFS_MAGIC)
        return false;

    img->head = head;
    img->num_entries = head->num_entries;
    img->hash = (const frogfs_hash_t *)((const uint8_t *)head + sizeof(frogfs_head_t));
    img->root = (const uint8_t *)img->hash +
                (size_t)img->num_entries * sizeof(frogfs_hash_t);
    return true;
}

static uint32_t rg_frogfs_djb2(const char *s)
{
    uint32_t hash = 5381;

    while (*s)
        hash = ((hash << 5) + hash) ^ (uint8_t)*s++;

    return hash;
}

/* Path segment bytes for an entry. Not NUL-terminated in the image; the name
 * follows the object header, whose size depends on the entry type. Mirrors
 * get_name() in frogfs.c. */
static const char *rg_frogfs_seg(const frogfs_entry_t *entry)
{
    if (FROGFS_IS_DIR(entry))
        return (const char *)entry + 8 + (entry->child_count * 4);
    if (FROGFS_IS_COMP(entry))
        return (const char *)entry + 20;
    return (const char *)entry + 16;
}

/* Compare an entry's full path against `path` by walking the parent chain
 * backwards, consuming `path` from the end. `path` has no leading slash. */
static bool rg_frogfs_path_matches(const rg_frogfs_image_t *img,
                                   const frogfs_entry_t *entry,
                                   const char *path)
{
    size_t remaining = strlen(path);

    /* The root entry is the one whose parent offset is 0; it owns "". */
    if (entry->parent == 0)
        return remaining == 0;

    for (;;) {
        size_t seg_sz = entry->seg_sz;

        if (remaining < seg_sz)
            return false;
        if (memcmp(path + remaining - seg_sz, rg_frogfs_seg(entry), seg_sz) != 0)
            return false;
        remaining -= seg_sz;

        const frogfs_entry_t *parent =
            (const frogfs_entry_t *)((const uint8_t *)img->head + entry->parent);

        /* A child of root carries no leading separator, and ends the walk. */
        if ((const void *)parent == img->root)
            return remaining == 0;

        if (remaining == 0 || path[remaining - 1] != '/')
            return false;
        remaining--;

        entry = parent;
        if (entry->parent == 0)
            return false; /* malformed: reached root off the root chain */
    }
}

const frogfs_entry_t *rg_frogfs_lookup(const char *path)
{
    rg_frogfs_image_t img;

    if (!path || !rg_frogfs_image(&img))
        return NULL;

    while (*path == '/')
        path++;

    const uint32_t want = rg_frogfs_djb2(path);

    /* mkfrogfs.py keys its entry table by hash, so a hash occurs at most once
     * and one candidate is enough -- there is no collision chain to walk. */
    int first = 0;
    int last = (int)img.num_entries - 1;

    while (first <= last) {
        const int middle = first + (last - first) / 2;
        const uint32_t got = img.hash[middle].hash;

        if (got == want) {
            const frogfs_entry_t *entry = (const frogfs_entry_t *)
                ((const uint8_t *)img.head + img.hash[middle].offs);
            return rg_frogfs_path_matches(&img, entry, path) ? entry : NULL;
        }

        if (got < want)
            first = middle + 1;
        else
            last = middle - 1;
    }

    return NULL;
}

bool rg_frogfs_entry_name(const frogfs_entry_t *entry, char *buf, size_t buflen)
{
    if (!entry || !buf || buflen == 0)
        return false;

    const size_t seg_sz = entry->seg_sz;
    if (seg_sz >= buflen)
        return false;

    memcpy(buf, rg_frogfs_seg(entry), seg_sz);
    buf[seg_sz] = '\0';
    return true;
}

frogfs_fs_t *rg_frogfs_get(void)
{
    if (s_frogfs)
        return s_frogfs;

    frogfs_config_t config = {
        /* FrogFS XiP base. Default == &__EXTFLASH_START__ (0x90000000 +
         * __EXTFLASH_OFFSET__); a patched layout superblock can override it so
         * one prebuilt binary serves any extflash offset. See gw_layout_superblock.h. */
        .addr = (const void *)(uintptr_t)gw_layout_frogfs_addr(),
    };

    s_frogfs = frogfs_init(&config);
    if (!s_frogfs)
        printf("FrogFS: init failed\n");

    return s_frogfs;
}

bool rg_frogfs_get_file_data(const char *path, const uint8_t **data, uint32_t *size)
{
    if (!path || !data || !size)
        return false;

    frogfs_fs_t *fs = rg_frogfs_get();
    if (!fs)
        return false;

    const frogfs_entry_t *entry = rg_frogfs_lookup(path);
    if (!entry || !frogfs_is_file(entry))
        return false;

    frogfs_stat_t stat;
    frogfs_stat(fs, entry, &stat);
    if (stat.compression != FROGFS_COMP_ALGO_NONE)
    {
        printf("FrogFS: '%s' is compressed, direct access rejected\n", path);
        return false;
    }

    frogfs_fh_t *file = frogfs_open(fs, entry, 0);
    if (!file)
        return false;

    const void *raw_data = NULL;
    size_t raw_size = frogfs_access(file, &raw_data);
    frogfs_close(file);

    if (!raw_data || raw_size > UINT32_MAX)
        return false;

    *data = (const uint8_t *)raw_data;
    *size = (uint32_t)raw_size;
    return true;
}

static FRESULT normalize_fatfs_path(const TCHAR *path, char *normalized, size_t normalized_size)
{
    const char *src = path;

    if (!src || !normalized || normalized_size == 0)
        return FR_INVALID_PARAMETER;

    if (!src[0])
        return FR_INVALID_NAME;

    if (src[1] == ':')
        src += 2;

    if (!src[0])
        src = "/";

    int written;
    if (src[0] == '/')
        written = snprintf(normalized, normalized_size, "%s", src);
    else
        written = snprintf(normalized, normalized_size, "/%s", src);

    if (written < 0 || (size_t)written >= normalized_size)
        return FR_INVALID_NAME;

    size_t len = strlen(normalized);
    while (len > 1 && normalized[len - 1] == '/')
        normalized[--len] = '\0';

    return FR_OK;
}

FRESULT f_opendir(DIR *dp, const TCHAR *path)
{
    if (!dp)
        return FR_INVALID_OBJECT;

    char normalized[RG_PATH_MAX + 1];
    FRESULT res = normalize_fatfs_path(path, normalized, sizeof(normalized));
    if (res != FR_OK)
        return res;

    frogfs_fs_t *fs = rg_frogfs_get();
    if (!fs)
        return FR_NOT_READY;

    const frogfs_entry_t *entry = rg_frogfs_lookup(normalized);
    if (!entry)
        return FR_NO_PATH;

    if (!frogfs_is_dir(entry))
        return FR_NO_PATH;

    frogfs_dh_t *dh = frogfs_opendir(fs, entry);
    if (!dh)
        return FR_INVALID_OBJECT;

    memset(dp, 0, sizeof(*dp));
    dp->dir = (BYTE *)dh;

    return FR_OK;
}

FRESULT f_closedir(DIR *dp)
{
    if (!dp || !dp->dir)
        return FR_INVALID_OBJECT;

    frogfs_closedir((frogfs_dh_t *)dp->dir);
    dp->dir = NULL;

    return FR_OK;
}

FRESULT f_readdir(DIR *dp, FILINFO *fno)
{
    if (!dp || !dp->dir)
        return FR_INVALID_OBJECT;

    frogfs_dh_t *dh = (frogfs_dh_t *)dp->dir;

    if (!fno) {
        frogfs_seekdir(dh, 0);
        return FR_OK;
    }

    memset(fno, 0, sizeof(*fno));

    const frogfs_entry_t *entry = frogfs_readdir(dh);
    if (!entry)
        return FR_OK;

    if (!rg_frogfs_entry_name(entry, fno->fname, sizeof(fno->fname)))
        return FR_INT_ERR;

    frogfs_stat_t st;
    frogfs_stat((const frogfs_fs_t *)dh->fs, entry, &st);

    fno->fsize = st.size;
    fno->fattrib = frogfs_is_dir(entry) ? AM_DIR : 0;

    return FR_OK;
}

#endif
