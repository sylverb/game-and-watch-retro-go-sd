#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef void (*file_progress_cb_t)(uint32_t total_size, uint32_t total_processed, uint8_t progress);

/* Called on each buffer of file data on its way to the flash, after the file's
 * final address is known but before anything is programmed. It lets a caller
 * relocate absolute addresses inside the payload — code linked at a sentinel
 * address, say — without a second erase/program pass over flash that is already
 * written.
 *
 * Doing it here rather than afterwards is what keeps that safe. A rewrite pass
 * has to erase before it can program, so an interrupted one (flat battery, say)
 * leaves a blank hole that the next boot has no way to tell from a finished job.
 * This runs before the metadata is committed, so an interrupted write is simply
 * a cache miss next time and is redone from scratch. It also stays clear of the
 * erase-granularity trap: chips in the wild go up to 256 KB sectors, so nothing
 * outside this file may assume it can erase a small piece of one.
 *
 *   buffer          the chunk, in RAM, free to modify in place
 *   length          its size (a multiple of 4 except possibly at end-of-file)
 *   offset_in_file  where the chunk starts within the file
 *   file_address    where the whole file will live (memory-mapped, i.e. XIP)
 *   file_size       the file's total size
 */
typedef void (*flash_relocate_cb_t)(uint8_t *buffer, uint32_t length, uint32_t offset_in_file,
                                    uint8_t *file_address, uint32_t file_size);

void flash_alloc_reset();

/* Forget which files are being read. The device does this by rebooting between
 * games; a host test has to ask. */
void flash_alloc_forget_live_files(void);
uint8_t *store_file_in_flash(const char *file_path, uint32_t *file_size_p, bool byte_swap, file_progress_cb_t progress_cb);

/* As store_file_in_flash(), but relocate_cb (if non-NULL) gets a crack at the
 * data before it is programmed. On a cache hit nothing is written and the
 * callback does not run — the copy in flash was already relocated, to the same
 * address, by whichever boot first stored it. */
uint8_t *store_file_in_flash_relocate(const char *file_path, uint32_t *file_size_p, bool byte_swap,
                                      file_progress_cb_t progress_cb, flash_relocate_cb_t relocate_cb);

/* odroid_overlay_cache_file_in_flash() with a relocation pass. Lives in
 * Core/Src/porting/odroid_overlay.c next to its sibling (it draws the "Caching
 * game" bar), but is declared here rather than in odroid_overlay.h so the
 * flash-allocator API stays next to the rest of the flash helpers. */
uint8_t *odroid_overlay_cache_file_in_flash_relocate(const char *file_path, uint32_t *file_size_p,
                                                     bool byte_swap, flash_relocate_cb_t relocate_cb);
