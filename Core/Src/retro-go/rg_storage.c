#include <odroid_system.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#if SD_CARD == 1
#include "ff.h"
#else
#include "rg_frogfs.h"
#include "gw_littlefs.h"
#endif
#include "rg_storage.h"
#include <unistd.h>

static bool disk_mounted = false;

#define CHECK_PATH(path)          \
    if (!(path && path[0]))       \
    {                             \
        printf("No path given"); \
        return false;             \
    }

void rg_storage_init(void)
{
    disk_mounted = true;
}

void rg_storage_deinit(void)
{
    if (!disk_mounted)
        return;

    rg_storage_commit();

    printf("Storage unmounted.");

    disk_mounted = false;
}

bool rg_storage_ready(void)
{
#if SD_CARD == 0
    return disk_mounted && rg_frogfs_get() != NULL;
#else
    return disk_mounted;
#endif
}

void rg_storage_commit(void)
{
    if (!disk_mounted)
        return;
    // flush buffers();
}

bool rg_storage_mkdir(const char *dir)
{
    CHECK_PATH(dir);

    if (mkdir(dir, 0777) == 0)
        return true;

    // FIXME: Might want to stat to see if it's a dir
    if (errno == EEXIST)
        return true;

    // Possibly missing some parents, try creating them (stack buffer, no heap)
    char temp[RG_PATH_MAX];
    strncpy(temp, dir, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    for (char *p = temp + strlen(RG_STORAGE_ROOT) + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = 0;
            if (strlen(temp) > 0)
            {
                mkdir(temp, 0777);
            }
            *p = '/';
            while (*(p + 1) == '/')
                p++;
        }
    }

    // Finally try again
    if (mkdir(dir, 0777) == 0)
        return true;

    return false;
}

static int delete_cb(const rg_scandir_t *file, void *arg)
{
    rg_storage_delete(file->path);
    return RG_SCANDIR_CONTINUE;
}

/* newlib's rename() is _link()+_unlink(), and _link() is an always-fail stub in
 * this toolchain, so it can never work here — go straight at the backend. */
bool rg_storage_rename(const char *old_path, const char *new_path)
{
    CHECK_PATH(old_path);
    CHECK_PATH(new_path);

    /* Neither backend replaces an existing destination, so clear it first. */
    remove(new_path);

#if SD_CARD == 1
    return f_rename(old_path, new_path) == FR_OK;
#else
    return fs_rename(old_path, new_path) == 0;
#endif
}

bool rg_storage_delete(const char *path)
{
    CHECK_PATH(path);

    // Try the fast way first
    if (remove(path) == 0 || rmdir(path) == 0)
        return true;

    // If that fails, it's likely a non-empty directory and we go recursive
    // (errno could confirm but it has proven unreliable across platforms...)
    if (rg_storage_scandir(path, delete_cb, NULL, 0))
        return rmdir(path) == 0;

    return false;
}

rg_stat_t rg_storage_stat(const char *path)
{
    rg_stat_t ret = {0};
    struct stat statbuf;
    /* Use stat(path), not fstat on an open fd: _fstat() does not fill st_mtime (FatFs time is set in stat()). */
    if (path && stat(path, &statbuf) == 0)
    {
        ret.basename = rg_basename(path);
        ret.extension = rg_extension(path);
        ret.size = statbuf.st_size;
        ret.mtime = statbuf.st_mtime;
        ret.is_file = S_ISREG(statbuf.st_mode);
        ret.is_dir = S_ISDIR(statbuf.st_mode);
        ret.exists = true;
    }
    return ret;
}

bool rg_storage_exists(const char *path)
{
    CHECK_PATH(path);
    return access(path, F_OK) == 0;
}

bool rg_storage_scandir(const char *path, rg_scandir_cb_t *callback, void *arg, uint32_t flags)
{
    CHECK_PATH(path);
    uint32_t types = flags & (RG_SCANDIR_FILES | RG_SCANDIR_DIRS);
#if SD_CARD == 1
    size_t path_len = strlen(path) + 1;
    FILINFO fno;
    DIR dir;
    FRESULT res;

    if (path_len > RG_PATH_MAX - 5)
    {
        printf("Folder path too long '%s'", path);
        return false;
    }

    res = f_opendir(&dir, path);
    if (res != FR_OK) {
        return false;
    }

    // We allocate on heap in case we go recursive through rg_storage_delete
    rg_scandir_t *result = calloc(1, sizeof(rg_scandir_t));
    if (!result)
    {
        f_closedir(&dir);
        return false;
    }

    strcat(strcpy(result->path, path), "/");
    result->basename = result->path + path_len;
    result->dirname = path;

    while (true)
    {
        wdog_refresh();
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0)
            break;

        if (fno.fname[0] == '.' && (!fno.fname[1] || fno.fname[1] == '.'))
        {
            // Skip self and parent
            continue;
        }

        if (path_len + strlen(fno.fname) >= RG_PATH_MAX)
        {
            printf("File path too long '%s/%s'", path, fno.fname);
            continue;
        }

        strcpy((char *)result->basename, fno.fname);

        result->is_file = !(fno.fattrib & AM_DIR);
        result->is_dir = fno.fattrib & AM_DIR;
        result->size = fno.fsize;
        result->mtime = fno.ftime;

        if ((result->is_dir && types != RG_SCANDIR_FILES) || (result->is_file && types != RG_SCANDIR_DIRS))
        {
            int ret = (callback)(result, arg);

            if (ret == RG_SCANDIR_STOP)
                break;

            if (ret == RG_SCANDIR_SKIP)
                continue;
        }

        if ((flags & RG_SCANDIR_RECURSIVE) && result->is_dir)
        {
            rg_storage_scandir(result->path, callback, arg, flags);
        }
    }
    f_closedir(&dir);
    free(result);

    return true;
#else
    frogfs_fs_t *fs = rg_frogfs_get();
    if (!fs)
        return false;

    const frogfs_entry_t *dir_entry = frogfs_get_entry(fs, path);
    if (!dir_entry || !frogfs_is_dir(dir_entry))
        return false;

    frogfs_dh_t *dir = frogfs_opendir(fs, dir_entry);
    if (!dir)
        return false;

    rg_scandir_t *result = calloc(1, sizeof(rg_scandir_t));
    if (!result)
    {
        frogfs_closedir(dir);
        return false;
    }

    result->dirname = path;

    const frogfs_entry_t *entry;
    while ((entry = frogfs_readdir(dir)) != NULL)
    {
        wdog_refresh();

        char *name = frogfs_get_name(entry);
        if (!name)
            continue;

        if (name[0] == '.' && (!name[1] || name[1] == '.'))
        {
            free(name);
            continue;
        }

        int written;
        if (strcmp(path, "/") == 0)
            written = snprintf(result->path, sizeof(result->path), "/%s", name);
        else
            written = snprintf(result->path, sizeof(result->path), "%s/%s", path, name);

        free(name);

        if (written < 0 || (size_t)written >= sizeof(result->path))
        {
            printf("File path too long '%s'", path);
            continue;
        }

        frogfs_stat_t st;
        frogfs_stat(fs, entry, &st);

        result->basename = strrchr(result->path, '/');
        result->basename = result->basename ? result->basename + 1 : result->path;
        result->is_file = frogfs_is_file(entry);
        result->is_dir = frogfs_is_dir(entry);
        result->size = st.size;
        result->mtime = 0;

        if ((result->is_dir && types != RG_SCANDIR_FILES) || (result->is_file && types != RG_SCANDIR_DIRS))
        {
            int ret = (callback)(result, arg);

            if (ret == RG_SCANDIR_STOP)
                break;

            if (ret == RG_SCANDIR_SKIP)
                continue;
        }

        if ((flags & RG_SCANDIR_RECURSIVE) && result->is_dir)
        {
            rg_storage_scandir(result->path, callback, arg, flags);
        }
    }

    frogfs_closedir(dir);
    free(result);

    return true;
#endif
}

/* max_len == 0 means unbounded (preserves the historical, unchecked
 * behavior relied upon by every existing caller of the two public
 * wrappers below). */
static size_t rg_storage_copy_file_to_ram_impl(char *file_path, uint8_t *ram_dest,
                                                uint32_t offset, uint32_t max_len,
                                                file_progress_cb_t file_progress_cb) {
    FILE *file;
    size_t bytes_read;
    uint32_t total_written;

    file = fopen(file_path,"rb");
    if (file == NULL) {
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    long file_size_l = ftell(file);
    if (file_size_l < 0 || (uint32_t)file_size_l < offset) {
        fclose(file);
        return 0;
    }
    uint32_t total_size = (uint32_t)file_size_l - offset;
    if (max_len != 0 && total_size > max_len) {
        // Refuse rather than overrun the caller's destination buffer/region.
        fclose(file);
        return 0;
    }
    if (fseek(file, (long)offset, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    total_written = 0;
    if (file_progress_cb) {
        file_progress_cb(total_size, 0, 0);
    }

    while ((bytes_read = fread(ram_dest+total_written, 1, 32*1024, file))) {
        wdog_refresh();
        total_written += bytes_read;
        if (file_progress_cb) {
            file_progress_cb(total_size, total_written, (uint8_t)((total_written * 100) / (total_size)));
        }
    }

    fclose(file);

    return total_written;
}

size_t rg_storage_copy_file_to_ram_with_offset(char *file_path, uint8_t *ram_dest, uint32_t offset, file_progress_cb_t file_progress_cb) {
    return rg_storage_copy_file_to_ram_impl(file_path, ram_dest, offset, 0, file_progress_cb);
}

size_t rg_storage_copy_file_to_ram_bounded(char *file_path, uint8_t *ram_dest, uint32_t offset, uint32_t max_len, file_progress_cb_t file_progress_cb) {
    return rg_storage_copy_file_to_ram_impl(file_path, ram_dest, offset, max_len, file_progress_cb);
}

/* copy file content into ram */
size_t rg_storage_copy_file_to_ram(char *file_path, uint8_t *ram_dest, file_progress_cb_t file_progress_cb) {
    return rg_storage_copy_file_to_ram_with_offset(file_path, ram_dest, 0, file_progress_cb);
}

/* copy at most `length` bytes starting at `offset` from a file into ram */
size_t rg_storage_copy_file_range_to_ram(char *file_path, uint8_t *ram_dest, uint32_t offset, uint32_t length, file_progress_cb_t file_progress_cb) {
    FILE *file;
    size_t bytes_read;
    uint32_t total_written;

    if (length == 0) {
        return 0;
    }

    file = fopen(file_path, "rb");
    if (file == NULL) {
        return 0;
    }

    if (fseek(file, (long)offset, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    total_written = 0;
    if (file_progress_cb) {
        file_progress_cb(length, 0, 0);
    }

    while (total_written < length) {
        uint32_t chunk = length - total_written;
        if (chunk > 32 * 1024) {
            chunk = 32 * 1024;
        }
        bytes_read = fread(ram_dest + total_written, 1, chunk, file);
        if (bytes_read == 0) {
            break;
        }
        wdog_refresh();
        total_written += bytes_read;
        if (file_progress_cb) {
            file_progress_cb(length, total_written, (uint8_t)((total_written * 100) / length));
        }
    }

    fclose(file);

    return total_written;
}

bool rg_storage_get_adjacent_files(const char *path, char *prev_path, char *next_path) {
    CHECK_PATH(path);
    
    // Get directory and extension (stack buffer, no heap)
    char dir[RG_PATH_MAX];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *last_slash = strrchr(dir, '/');
    if (!last_slash) return false;
    *last_slash = '\0';

    const char *ext = rg_extension(path);
    if (!ext) return false;

    const char *current_basename = rg_basename(path);
    char best_prev[RG_PATH_MAX] = {0};
    char best_next[RG_PATH_MAX] = {0};
    bool need_prev = prev_path != NULL;
    bool need_next = next_path != NULL;

#if SD_CARD == 1
    DIR dir_obj;
    FILINFO fno;
    FRESULT res = f_opendir(&dir_obj, dir);
    if (res != FR_OK) return false;
    
    // Single pass to find previous and next files
    while (true) {
        wdog_refresh();
        res = f_readdir(&dir_obj, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        
        // Skip directories and hidden files
        if (fno.fattrib & AM_DIR || fno.fname[0] == '.') continue;
        
        const char *file_ext = rg_extension(fno.fname);
        if (file_ext && strcasecmp(file_ext, ext) == 0) {
            // Compare with current file to determine if it's a better match
            int cmp = strcasecmp(fno.fname, current_basename);
            
            // For previous file: we want the highest file that's still lower than current
            if (need_prev && cmp < 0) {
                // If we don't have a previous file yet, or this one is higher than our current best
                if (!best_prev[0] || strcasecmp(fno.fname, best_prev + strlen(dir) + 1) > 0) {
                    sprintf(best_prev, "%s/%s", dir, fno.fname);
                }
            }
            
            // For next file: we want the lowest file that's still higher than current
            if (need_next && cmp > 0) {
                // If we don't have a next file yet, or this one is lower than our current best
                if (!best_next[0] || strcasecmp(fno.fname, best_next + strlen(dir) + 1) < 0) {
                    sprintf(best_next, "%s/%s", dir, fno.fname);
                }
            }
        }
    }
    
    f_closedir(&dir_obj);
    
    // Copy results to output buffers, using current path if no match found
    if (need_prev) {
        strcpy(prev_path, best_prev[0] ? best_prev : path);
    }
    if (need_next) {
        strcpy(next_path, best_next[0] ? best_next : path);
    }
    
    return true;
#else
    frogfs_fs_t *fs = rg_frogfs_get();
    if (!fs)
        return false;

    const frogfs_entry_t *dir_entry = frogfs_get_entry(fs, dir);
    if (!dir_entry || !frogfs_is_dir(dir_entry))
        return false;

    frogfs_dh_t *dir_obj = frogfs_opendir(fs, dir_entry);
    if (!dir_obj)
        return false;

    const frogfs_entry_t *entry;
    while ((entry = frogfs_readdir(dir_obj)) != NULL) {
        wdog_refresh();

        if (!frogfs_is_file(entry))
            continue;

        char *name = frogfs_get_name(entry);
        if (!name)
            continue;

        if (name[0] == '.') {
            free(name);
            continue;
        }

        const char *file_ext = rg_extension(name);
        if (file_ext && strcasecmp(file_ext, ext) == 0) {
            int cmp = strcasecmp(name, current_basename);

            if (need_prev && cmp < 0) {
                if (!best_prev[0] || strcasecmp(name, best_prev + strlen(dir) + 1) > 0) {
                    snprintf(best_prev, sizeof(best_prev), "%s/%s", dir, name);
                }
            }

            if (need_next && cmp > 0) {
                if (!best_next[0] || strcasecmp(name, best_next + strlen(dir) + 1) < 0) {
                    snprintf(best_next, sizeof(best_next), "%s/%s", dir, name);
                }
            }
        }

        free(name);
    }

    frogfs_closedir(dir_obj);

    if (need_prev) {
        strcpy(prev_path, best_prev[0] ? best_prev : path);
    }
    if (need_next) {
        strcpy(next_path, best_next[0] ? best_next : path);
    }

    return true;
#endif
}
