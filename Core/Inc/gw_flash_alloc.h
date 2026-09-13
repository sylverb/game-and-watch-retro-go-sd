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

/* Derived-data blobs: same flash cache, RAM source, caller-chosen key
 * string (make it content-addressed). lookup probes the cache without
 * writing; store writes (or returns the cached copy when the key+size
 * already match). Both return a memory-mapped read-only pointer, NULL
 * on miss / no room. */
const uint8_t *lookup_data_in_flash(const char *key, uint32_t *size_out);
const uint8_t *store_data_in_flash(const char *key, const uint8_t *data, uint32_t data_size);
/* Optional progress hook for the NEXT store_data_in_flash() calls: invoked
 * once per programmed chunk with (bytes done, bytes total).  Building a
 * 256KB blob takes seconds and the caller may be the only thing on screen,
 * so it needs a way to say "still working".  NULL disables it. */
void store_data_set_progress_cb(void (*cb)(uint32_t done, uint32_t total));

/* ---- streaming blob store -------------------------------------------
 * Same cache, but written incrementally so the producer never needs a RAM
 * buffer the size of the blob. Written for rom_map()'s inflate: a 512KB
 * ROM entry used to need 512KB of transient machine pool, which simply
 * does not fit, and the failure looked like a missing ROM.
 *
 * begin() reserves and returns false if there is no room; append() may be
 * called any number of times with any chunk sizes; finish() commits the
 * metadata and returns the memory-mapped address (NULL on failure).
 * abort() throws the reservation away without committing.
 *
 * append() toggles memory-mapped mode around each program, so the CALLER
 * MAY read the external flash between appends -- which the inflate does,
 * since its compressed input is mapped there. */
typedef struct {
    uint32_t key_crc;
    uint32_t flash_address;      /* mapped base of the blob */
    uint32_t prog_addr;          /* program cursor, offset in ext flash */
    uint32_t erase_addr;
    uint32_t erase_left;
    uint32_t total;
    uint32_t done;
    uint32_t erase_size_total;
    bool     active;
} flash_stream_t;

bool store_data_begin(flash_stream_t *st, const char *key, uint32_t total_size);
bool store_data_append(flash_stream_t *st, const uint8_t *buf, uint32_t len);
const uint8_t *store_data_finish(flash_stream_t *st);
void store_data_abort(flash_stream_t *st);

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
