#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>
#include "rg_rtc.h"
#include "config.h"
#if SD_CARD == 1
#include "ff.h"
#else
#include "gw_littlefs.h"
#include "rg_frogfs.h"
#endif

#if SD_CARD == 1

extern uint32_t log_idx;
extern char logbuf[1024 * 4];

typedef struct {
    FIL file;
    int is_open;
} FatFSFile;

#define MAX_OPEN_FILES 10
FatFSFile file_table[MAX_OPEN_FILES];

void init_file_table() {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        file_table[i].is_open = 0;
    }
}

#define FATFS_FD_OFFSET 3 // Prevent collision with STDOUT_FILENO, ...
int find_free_slot() {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_table[i].is_open) {
            return i;
        }
    }
    return -1; // No free slot
}

int _open(const char *name, int flags, int mode)
{
    int slot = find_free_slot();
    if (slot == -1) {
        errno = ENFILE;
        return -1;
    }

    BYTE mode_flags = 0;
    switch (flags&0xFFFF) {
        case 0x0:       // "r"
            mode_flags = FA_READ;
            break;
        case 0x601:     // "w"
            mode_flags = FA_CREATE_ALWAYS | FA_WRITE;
            break;
        case 0x209:     // "a"
            mode_flags = FA_OPEN_APPEND | FA_WRITE;
            break;
        case 0x2:       // "r+"
            mode_flags = FA_READ | FA_WRITE;
            break;
        case 0x602:     // "w+"
            mode_flags = FA_CREATE_ALWAYS | FA_READ | FA_WRITE;
            break;
        case 0x20a:     // "a+"
            mode_flags = FA_OPEN_APPEND | FA_READ | FA_WRITE;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    FRESULT res = f_open(&file_table[slot].file, name, mode_flags);
    if (res != FR_OK) {
        errno = EIO;
        return -1;
    }

    file_table[slot].is_open = 1;
    return slot + FATFS_FD_OFFSET;
;
}

int _close(int file)
{
    file = file - FATFS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
        errno = EBADF;
        return -1;
    }

    FRESULT res = f_close(&file_table[file].file);
    if (res != FR_OK) {
        errno = EIO;
        return -1;
    }

    file_table[file].is_open = 0;
    return 0;
}

int _write(int file, char *ptr, int len)
{
    if (file == STDOUT_FILENO || file == STDERR_FILENO)
    {
        if (len <= 0) return len < 0 ? 0 : 0;
        /* A single write larger than the buffer would memcpy past logbuf into
         * .data/.bss after the wrap branch — clamp it to fit. */
        if ((size_t)len >= sizeof(logbuf))
            len = (int)(sizeof(logbuf) - 1);

        uint32_t idx = log_idx;
        if ((size_t)idx + (size_t)len + 1 > sizeof(logbuf))
            idx = 0;

        memcpy(&logbuf[idx], ptr, len);
        idx += len;
        logbuf[idx] = '\0';

        log_idx = idx;

        return len;
    }
    else
    {
        file = file - FATFS_FD_OFFSET;
        if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
            errno = EBADF;
            return -1;
        }

        UINT bytes_written;
        FRESULT res = f_write(&file_table[file].file, ptr, len, &bytes_written);
        if (res != FR_OK) {
            errno = EIO;
            return -1;
        }

        return bytes_written;
    }
}

int _read(int file, char *ptr, int len)
{
    file = file - FATFS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
        errno = EBADF;
        return -1;
    }

    UINT bytes_read;
    FRESULT res = f_read(&file_table[file].file, ptr, len, &bytes_read);
    if (res != FR_OK) {
        errno = EIO;
        return -1;
    }

    return bytes_read;
}

off_t _lseek(int file, off_t offset, int whence) {
    file = file - FATFS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
        errno = EBADF;
        return -1;
    }

    DWORD new_offset;
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = f_tell(&file_table[file].file) + offset;
            break;
        case SEEK_END:
            new_offset = f_size(&file_table[file].file) + offset;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    FRESULT res = f_lseek(&file_table[file].file, new_offset);
    if (res != FR_OK) {
        errno = EIO;
        return -1;
    }

    return new_offset;
}

static time_t fatfs_to_unix_timestamp(WORD fdate, WORD ftime) {
    struct tm t;

    // Extract date
    t.tm_year  = ((fdate >> 9) & 0x7F) + 80; // Years since 1900
    t.tm_mon   = ((fdate >> 5) & 0x0F) - 1;  // Month (0-11)
    t.tm_mday  = (fdate & 0x1F);             // Day (1-31)

    // Extract time
    t.tm_hour  = (ftime >> 11) & 0x1F;       // Hour (0-23)
    t.tm_min   = (ftime >> 5) & 0x3F;        // Minute (0-59)
    t.tm_sec   = (ftime & 0x1F) * 2;         // Second (0-59)

    t.tm_isdst = -1; // Daylight Saving Time flag

    return mktime(&t);
}

int _fstat(int file, struct stat *st) {
    file = file - FATFS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
        errno = EBADF;
        return -1;
    }

    st->st_size = f_size(&file_table[file].file);
    st->st_mode = S_IFREG;
    // TODO : fill st->st_mtime if needed
    return 0;
}

int stat(const char *path, struct stat *st) {
    FILINFO fno;
    FRESULT res;

    // Use FatFs to get file information
    res = f_stat(path, &fno);
    if (res != FR_OK) {
        // Map FatFs error to errno
        switch (res) {
            case FR_NO_FILE:
            case FR_NO_PATH:
                errno = ENOENT;  // No such file or directory
                break;
            case FR_DENIED:
                errno = EACCES;  // Permission denied
                break;
            case FR_INVALID_NAME:
                errno = EINVAL;  // Invalid path
                break;
            default:
                errno = EIO;     // I/O error
                break;
        }
        return -1;
    }

    // Populate the stat structure
    st->st_size = fno.fsize;  // File size
    if (fno.fattrib & AM_DIR) {
        st->st_mode = S_IFDIR;  // Directory
    } else {
        st->st_mode = S_IFREG;  // Regular file
    }

    // Set modification time
    st->st_mtime = fatfs_to_unix_timestamp(fno.fdate, fno.ftime);

    return 0;
}

int _isatty(int file)
{
    return 0;
}

int _feof(int file) {
    file = file - FATFS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
        errno = EBADF;
        return -1;
    }

    if (f_eof(&file_table[file].file)) {
        return 1;
    }
    return 0;
}

int mkdir(const char *path, mode_t mode) {
    FRESULT res;

    res = f_mkdir(path);
    switch (res)
    {
        case FR_OK:
            return 0;
            break;
        case FR_EXIST:
            errno = EEXIST;
            break;
        case FR_NO_PATH:
            errno = ENOENT;
            break;
        case FR_INVALID_NAME:
            errno = EINVAL;
            break;
        case FR_DENIED:
            errno = EACCES;
            break;
        case FR_DISK_ERR:
            errno = EIO;
            break;
        default:
            errno = EIO;
            break;
    }
    return -1;
}

int rmdir(const char *path) {
    FRESULT res;
    DIR dir;
    FILINFO fno;

    // Open the directory
    res = f_opendir(&dir, path);
    if (res != FR_OK) {
        return -1;  // Directory could not be opened
    }

    // Check if the directory is empty before deleting
    res = f_readdir(&dir, &fno);
    if (res != FR_OK || fno.fname[0] != 0) {
        // Directory is not empty or an error occurred
        f_closedir(&dir);
        return -1;
    }

    // Remove the directory if empty
    f_closedir(&dir);
    res = f_unlink(path);
    
    if (res == FR_OK) {
        return 0;  // Success
    }
    
    return -1;  // Failure
}

int _unlink(const char *path) {
    FRESULT res;
    res = f_unlink(path);
    if (res == FR_OK) {
        return 0;
    } else {
        errno = ENOENT;
        return -1;
    }
}

int __wrap_fflush(int file) {
    file = file - FATFS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || !file_table[file].is_open) {
        errno = EBADF;
        return -1;
    }

    FRESULT res = f_sync(&file_table[file].file);
    if (res != FR_OK) {
        errno = EIO;
        return -1;
    }

    return 0;
}

#else

extern uint32_t log_idx;
extern char logbuf[1024 * 4];

#define MAX_OPEN_FILES 10
#define FS_FD_OFFSET 3

typedef enum {
    FILE_BACKEND_NONE,
    FILE_BACKEND_FROGFS,
    FILE_BACKEND_LITTLEFS,
} FileBackend;

typedef struct {
    FileBackend backend;
    union {
        frogfs_fh_t *frogfs;
        fs_file_t *littlefs;
    } file;
} FileTableEntry;

static FileTableEntry file_table[MAX_OPEN_FILES];

void init_file_table(void)
{
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        file_table[i].backend = FILE_BACKEND_NONE;
        file_table[i].file.frogfs = NULL;
    }
}

static int find_free_slot(void)
{
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (file_table[i].backend == FILE_BACKEND_NONE)
            return i;
    }
    return -1;
}

static bool path_has_prefix_dir(const char *path, const char *dir)
{
    if (!path)
        return false;

    if (path[0] == '/')
        path++;

    size_t len = strlen(dir);
    return strncmp(path, dir, len) == 0 && (path[len] == '\0' || path[len] == '/');
}

static bool is_cores_pico8_ro_frogfs_path(const char *path)
{
    if (!path)
        return false;
    if (path[0] == '/')
        path++;
    return strcmp(path, "cores/pico8.ro") == 0;
}

static bool is_frogfs_path(const char *path)
{
    return path_has_prefix_dir(path, "roms") ||
           path_has_prefix_dir(path, "covers") ||
           path_has_prefix_dir(path, "bios") ||
           path_has_prefix_dir(path, "fonts") ||
           path_has_prefix_dir(path, "font") ||
           is_cores_pico8_ro_frogfs_path(path);
}

static const char *normalize_frogfs_path(const char *name, char *buffer, size_t buffer_size)
{
    if (!name || !buffer || buffer_size < 2)
        return NULL;

    if (name[0] == '/')
        return name;

    int written = snprintf(buffer, buffer_size, "/%s", name);
    if (written < 0 || (size_t)written >= buffer_size)
        return NULL;

    return buffer;
}

int _open(const char *name, int flags, int mode)
{
    (void)mode;

    int slot = find_free_slot();
    if (slot == -1) {
        errno = ENFILE;
        return -1;
    }

    if (is_frogfs_path(name)) {
        if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC | O_APPEND))) {
            errno = EROFS;
            return -1;
        }

        frogfs_fs_t *fs = rg_frogfs_get();
        if (!fs) {
            errno = ENODEV;
            return -1;
        }

        char normalized_name[RG_PATH_MAX];
        const char *frogfs_name = normalize_frogfs_path(name, normalized_name, sizeof(normalized_name));
        if (!frogfs_name) {
            errno = ENAMETOOLONG;
            return -1;
        }

        const frogfs_entry_t *entry = frogfs_get_entry(fs, frogfs_name);
        if (!entry || !frogfs_is_file(entry)) {
            errno = ENOENT;
            return -1;
        }

        frogfs_fh_t *file = frogfs_open(fs, entry, 0);
        if (!file) {
            errno = EIO;
            return -1;
        }

        file_table[slot].file.frogfs = file;
        file_table[slot].backend = FILE_BACKEND_FROGFS;
        return slot + FS_FD_OFFSET;
    }

    int lfs_flags;
    switch (flags & O_ACCMODE) {
        case O_RDONLY:
            lfs_flags = LFS_O_RDONLY;
            break;
        case O_WRONLY:
            lfs_flags = LFS_O_WRONLY;
            break;
        case O_RDWR:
            lfs_flags = LFS_O_RDWR;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    if (flags & O_CREAT)
        lfs_flags |= LFS_O_CREAT;
    if (flags & O_EXCL)
        lfs_flags |= LFS_O_EXCL;
    if (flags & O_TRUNC)
        lfs_flags |= LFS_O_TRUNC;
    if (flags & O_APPEND)
        lfs_flags |= LFS_O_APPEND;

    fs_file_t *file = fs_open_flags(name, lfs_flags);
    if (!file) {
        errno = (flags & O_ACCMODE) == O_RDONLY ? ENOENT : EIO;
        return -1;
    }

    file_table[slot].file.littlefs = file;
    file_table[slot].backend = FILE_BACKEND_LITTLEFS;
    return slot + FS_FD_OFFSET;
}

int _close(int file)
{
    file -= FS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || file_table[file].backend == FILE_BACKEND_NONE) {
        errno = EBADF;
        return -1;
    }

    if (file_table[file].backend == FILE_BACKEND_FROGFS) {
        frogfs_close(file_table[file].file.frogfs);
    } else {
        fs_close(file_table[file].file.littlefs);
    }

    file_table[file].file.frogfs = NULL;
    file_table[file].backend = FILE_BACKEND_NONE;
    return 0;
}

int _write(int file, char *ptr, int len)
{
    if (file == STDOUT_FILENO || file == STDERR_FILENO)
    {
        if (len <= 0) return len < 0 ? 0 : 0;
        /* A single write larger than the buffer would memcpy past logbuf into
         * .data/.bss after the wrap branch — clamp it to fit. */
        if ((size_t)len >= sizeof(logbuf))
            len = (int)(sizeof(logbuf) - 1);

        uint32_t idx = log_idx;
        if ((size_t)idx + (size_t)len + 1 > sizeof(logbuf))
            idx = 0;

        memcpy(&logbuf[idx], ptr, len);
        idx += len;
        logbuf[idx] = '\0';

        log_idx = idx;
        return len;
    }

    file -= FS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || file_table[file].backend == FILE_BACKEND_NONE) {
        errno = EBADF;
        return -1;
    }

    if (file_table[file].backend == FILE_BACKEND_FROGFS) {
        errno = EROFS;
        return -1;
    }

    int written = fs_write(file_table[file].file.littlefs, (unsigned char *)ptr, len);
    if (written < 0) {
        errno = EIO;
        return -1;
    }

    return written;
}

int _read(int file, char *ptr, int len)
{
    file -= FS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || file_table[file].backend == FILE_BACKEND_NONE) {
        errno = EBADF;
        return -1;
    }

    if (file_table[file].backend == FILE_BACKEND_FROGFS)
        return frogfs_read(file_table[file].file.frogfs, ptr, len);

    int bytes_read = fs_read(file_table[file].file.littlefs, (unsigned char *)ptr, len);
    if (bytes_read < 0) {
        errno = EIO;
        return -1;
    }
    return bytes_read;
}

off_t _lseek(int file, off_t offset, int whence)
{
    file -= FS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || file_table[file].backend == FILE_BACKEND_NONE) {
        errno = EBADF;
        return -1;
    }

    ssize_t pos = file_table[file].backend == FILE_BACKEND_FROGFS
            ? frogfs_seek(file_table[file].file.frogfs, offset, whence)
            : fs_seek(file_table[file].file.littlefs, offset, whence);
    if (pos < 0) {
        errno = EINVAL;
        return -1;
    }

    return pos;
}

int _fstat(int file, struct stat *st)
{
    file -= FS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || file_table[file].backend == FILE_BACKEND_NONE) {
        errno = EBADF;
        return -1;
    }

    ssize_t pos = file_table[file].backend == FILE_BACKEND_FROGFS
            ? frogfs_tell(file_table[file].file.frogfs)
            : fs_seek(file_table[file].file.littlefs, 0, SEEK_CUR);
    ssize_t size = file_table[file].backend == FILE_BACKEND_FROGFS
            ? frogfs_seek(file_table[file].file.frogfs, 0, SEEK_END)
            : fs_seek(file_table[file].file.littlefs, 0, SEEK_END);
    if (pos < 0 || size < 0) {
        errno = EIO;
        return -1;
    }
    if (file_table[file].backend == FILE_BACKEND_FROGFS)
        frogfs_seek(file_table[file].file.frogfs, pos, SEEK_SET);
    else
        fs_seek(file_table[file].file.littlefs, pos, SEEK_SET);

    memset(st, 0, sizeof(*st));
    st->st_size = size;
    st->st_mode = S_IFREG;
    return 0;
}

int stat(const char *path, struct stat *st)
{
    memset(st, 0, sizeof(*st));

    if (is_frogfs_path(path)) {
        frogfs_fs_t *fs = rg_frogfs_get();
        if (!fs) {
            errno = ENODEV;
            return -1;
        }

        char normalized_path[RG_PATH_MAX];
        const char *frogfs_path = normalize_frogfs_path(path, normalized_path, sizeof(normalized_path));
        if (!frogfs_path) {
            errno = ENAMETOOLONG;
            return -1;
        }

        const frogfs_entry_t *entry = frogfs_get_entry(fs, frogfs_path);
        if (!entry) {
            errno = ENOENT;
            return -1;
        }

        frogfs_stat_t frog_stat;
        frogfs_stat(fs, entry, &frog_stat);

        st->st_size = frog_stat.size;
        st->st_mode = frogfs_is_dir(entry) ? S_IFDIR : S_IFREG;
        return 0;
    }

    struct lfs_info info;
    int res = fs_stat(path, &info);
    if (res < 0) {
        errno = res == LFS_ERR_NOENT ? ENOENT : EIO;
        return -1;
    }

    st->st_size = info.size;
    st->st_mode = info.type == LFS_TYPE_DIR ? S_IFDIR : S_IFREG;
    uint32_t timestamp;
    if (fs_get_file_time(path, &timestamp) == sizeof(timestamp))
        st->st_mtime = timestamp;
    return 0;
}

int _feof(int file)
{
    file -= FS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || file_table[file].backend == FILE_BACKEND_NONE) {
        errno = EBADF;
        return -1;
    }

    ssize_t pos = file_table[file].backend == FILE_BACKEND_FROGFS
            ? frogfs_tell(file_table[file].file.frogfs)
            : fs_seek(file_table[file].file.littlefs, 0, SEEK_CUR);
    ssize_t size = file_table[file].backend == FILE_BACKEND_FROGFS
            ? frogfs_seek(file_table[file].file.frogfs, 0, SEEK_END)
            : fs_seek(file_table[file].file.littlefs, 0, SEEK_END);
    if (pos < 0 || size < 0) {
        errno = EIO;
        return -1;
    }
    if (file_table[file].backend == FILE_BACKEND_FROGFS)
        frogfs_seek(file_table[file].file.frogfs, pos, SEEK_SET);
    else
        fs_seek(file_table[file].file.littlefs, pos, SEEK_SET);
    return pos >= size;
}

int mkdir(const char *path, mode_t mode)
{
    (void)mode;

    if (is_frogfs_path(path)) {
        errno = EROFS;
        return -1;
    }

    int res = fs_mkdir(path);
    if (res < 0) {
        errno = res == LFS_ERR_EXIST ? EEXIST : EIO;
        return -1;
    }

    return 0;
}

int rmdir(const char *path)
{
    if (is_frogfs_path(path)) {
        errno = EROFS;
        return -1;
    }

    int res = fs_delete(path);
    if (res < 0) {
        errno = res == LFS_ERR_NOENT ? ENOENT : EIO;
        return -1;
    }

    return 0;
}

int _unlink(const char *path)
{
    if (is_frogfs_path(path)) {
        errno = EROFS;
        return -1;
    }

    int res = fs_delete(path);
    if (res < 0) {
        errno = res == LFS_ERR_NOENT ? ENOENT : EIO;
        return -1;
    }

    return 0;
}

int __wrap_fflush(int file)
{
    if (file == STDOUT_FILENO || file == STDERR_FILENO)
        return 0;

    file -= FS_FD_OFFSET;
    if (file < 0 || file >= MAX_OPEN_FILES || file_table[file].backend == FILE_BACKEND_NONE) {
        errno = EBADF;
        return -1;
    }

    if (file_table[file].backend == FILE_BACKEND_LITTLEFS && fs_sync(file_table[file].file.littlefs) < 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

#endif

int _gettimeofday(struct timeval *tv, void *tzvp)
{
    if (tv)
    {
        // get epoch UNIX time from RTC
        time_t unixTime = GW_GetUnixTime();
        tv->tv_sec = unixTime;

        // get millisecondes from rtc and convert them to microsecondes
        uint64_t millis = GW_GetCurrentMillis();
        tv->tv_usec = (millis % 1000) * 1000;
        return 0;
    }

    errno = EINVAL;
    return -1;
}
