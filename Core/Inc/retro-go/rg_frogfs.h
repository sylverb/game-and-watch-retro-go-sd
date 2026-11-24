#pragma once

#if SD_CARD == 0

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "frogfs/frogfs.h"

frogfs_fs_t *rg_frogfs_get(void);
bool rg_frogfs_get_file_data(const char *path, const uint8_t **data, uint32_t *size);

/* A path segment length is a uint8_t in the image, so this always suffices. */
#define RG_FROGFS_NAME_MAX 256

/* Allocation-free stand-ins for frogfs_get_entry() / frogfs_get_name(), which
 * both allocate and fail unsafely when a resident core has exhausted the heap.
 * Use these instead. */
const frogfs_entry_t *rg_frogfs_lookup(const char *path);
bool rg_frogfs_entry_name(const frogfs_entry_t *entry, char *buf, size_t buflen);

#endif
