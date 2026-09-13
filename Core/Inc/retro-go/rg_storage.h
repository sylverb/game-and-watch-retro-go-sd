#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "config.h"

#define RG_BASE_PATH        RG_STORAGE_ROOT "/retro-go"
#define RG_BASE_PATH_BIOS   RG_BASE_PATH "/bios"
#define RG_BASE_PATH_CACHE  RG_BASE_PATH "/cache"
#define RG_BASE_PATH_CONFIG RG_BASE_PATH "/config"
#define RG_BASE_PATH_COVERS RG_STORAGE_ROOT "/romart"
#define RG_BASE_PATH_MUSIC  RG_STORAGE_ROOT "/music"
#define RG_BASE_PATH_ROMS      RG_STORAGE_ROOT "/roms"
#define RG_BASE_PATH_HOMEBREWS RG_STORAGE_ROOT "/homebrews"
#define RG_BASE_PATH_SAVES     RG_BASE_PATH "/saves"
#define RG_BASE_PATH_THEMES RG_BASE_PATH "/themes"
#define RG_BASE_PATH_BORDERS RG_BASE_PATH "/borders"

typedef struct
{
    char path[RG_PATH_MAX + 1];
    const char *basename;
    const char *dirname;
    size_t size;
    time_t mtime;
    bool is_file;
    bool is_dir;
} rg_scandir_t;

typedef int (rg_scandir_cb_t)(const rg_scandir_t *file, void *arg);

enum
{
    RG_SCANDIR_FILES = (1 << 0),
    RG_SCANDIR_DIRS  = (1 << 1),
    RG_SCANDIR_STAT  = (1 << 8),
    RG_SCANDIR_SORT  = (1 << 9),
    RG_SCANDIR_RECURSIVE = (1 << 10),

    RG_SCANDIR_CONTINUE = 1,
    RG_SCANDIR_SKIP = 2,
    RG_SCANDIR_STOP = 0,
};

typedef struct
{
    const char *basename;
    const char *extension;
    size_t size;
    time_t mtime;
    bool is_dir;
    bool is_file;
    bool is_link;
    bool exists;
} rg_stat_t;

void rg_storage_init(void);
void rg_storage_deinit(void);
bool rg_storage_format(void);
bool rg_storage_ready(void);
void rg_storage_commit(void);
void rg_storage_set_activity_led(bool enable);
bool rg_storage_get_activity_led(void);
bool rg_storage_read_file(const char *path, void **data_ptr, size_t *data_len);
bool rg_storage_write_file(const char *path, const void *data_ptr, const size_t data_len);
bool rg_storage_delete(const char *path);
bool rg_storage_exists(const char *path);
bool rg_storage_mkdir(const char *dir);
bool rg_storage_scandir(const char *path, rg_scandir_cb_t *callback, void *arg, uint32_t flags);
rg_stat_t rg_storage_stat(const char *path);

typedef void (*file_progress_cb_t)(uint32_t total_size, uint32_t total_processed, uint8_t progress);

size_t rg_storage_copy_file_to_ram(char *file_path, uint8_t *ram_dest, file_progress_cb_t file_progress_cb);
size_t rg_storage_copy_file_to_ram_with_offset(char *file_path, uint8_t *ram_dest, uint32_t offset, file_progress_cb_t file_progress_cb);
size_t rg_storage_copy_file_range_to_ram(char *file_path, uint8_t *ram_dest, uint32_t offset, uint32_t length, file_progress_cb_t file_progress_cb);

/* Same as rg_storage_copy_file_to_ram_with_offset, but refuses (returns 0,
 * copies nothing) if the file's size-after-offset exceeds max_len. Used by
 * loaders that write into a fixed-size RAM region (e.g. RAM_EMU) fed by a
 * runtime-supplied file, where the destination buffer's capacity is not
 * implied by any compile-time overlay symbol. */
size_t rg_storage_copy_file_to_ram_bounded(char *file_path, uint8_t *ram_dest, uint32_t offset, uint32_t max_len, file_progress_cb_t file_progress_cb);

bool rg_storage_get_adjacent_files(const char *path, char *prev_path, char *next_path);