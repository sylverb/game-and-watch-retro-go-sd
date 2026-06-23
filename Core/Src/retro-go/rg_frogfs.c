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

static frogfs_fs_t *s_frogfs;

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

    const frogfs_entry_t *entry = frogfs_get_entry(fs, path);
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

    const frogfs_entry_t *entry = frogfs_get_entry(fs, normalized);
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

    char *name = frogfs_get_name(entry);
    if (!name)
        return FR_INT_ERR;

    strncpy(fno->fname, name, sizeof(fno->fname) - 1);
    fno->fname[sizeof(fno->fname) - 1] = '\0';
    free(name);

    frogfs_stat_t st;
    frogfs_stat((const frogfs_fs_t *)dh->fs, entry, &st);

    fno->fsize = st.size;
    fno->fattrib = frogfs_is_dir(entry) ? AM_DIR : 0;

    return FR_OK;
}

#endif
