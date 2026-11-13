#ifndef GW_LITTLEFS_H__
#define GW_LITTLEFS_H__

/* LittleFS volume on external flash (writable /data, etc.). ROM/covers use FrogFS separately. */

#include <stdint.h>
#include <stddef.h>
#include "lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FS_MAX_PATH_SIZE (192)  // including null-terminator

#define FS_WRITE true
#define FS_READ false

#define FS_COMPRESS true
#define FS_RAW false

typedef lfs_file_t fs_file_t;
void fs_init(void);

typedef struct {
    char        name[100];
    char        size[11];
    bool        is_folder;
    bool        has_savestate;
    bool        has_sram;
} fs_folder_entry;

fs_file_t *fs_open(const char *path, bool write_mode, bool use_compression);
fs_file_t *fs_open_flags(const char *path, int flags);
int fs_write(fs_file_t *file, unsigned char *data, size_t size);
int fs_delete(const char *path);
int fs_rename(const char *old_path, const char *new_path);
int fs_read(fs_file_t *file, unsigned char *buffer, size_t size);
int fs_seek(fs_file_t *file, lfs_soff_t off, int whence);
int fs_sync(fs_file_t *file);
int fs_stat(const char *path, struct lfs_info *info);
int fs_get_file_time(const char *path, uint32_t *timestamp);
int fs_mkdir(const char *path);
void fs_close(fs_file_t *file);
bool fs_exists(const char *path);
uint32_t fs_free_blocks();

int fs_dir_open(uint8_t dir_index, const char *path);
int fs_dir_read(uint8_t dir_index, fs_folder_entry *entry);
int fs_dir_close(uint8_t dir_index);

extern bool fs_mounted;

#ifdef __cplusplus
}
#endif

#endif
