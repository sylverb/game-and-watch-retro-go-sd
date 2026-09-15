#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <assert.h>
#include "stm32h7xx.h"
#include "main.h"
#include "crc32.h"
#include "gw_flash.h"
#include "gw_linker.h"
#include "config.h"
#include "gw_malloc.h"
#include "gw_flash_alloc.h"
#include "gw_ofw.h"

/* V2 INDEX, DELIBERATELY UNDER A NEW NAME.
 *
 * The index is a fixed table of MAX_FILES slots, 16 bytes each, keyed by a
 * CRC of the file path (or of a blob key).  It says WHERE in external flash
 * each cached item landed; the flash holds only the data.  A slot costs the
 * same whether it describes a 279-byte PLD or a 4MB ROM, so the table size
 * is a limit on the NUMBER of cached items, not on their bytes -- and 50 is
 * too few for a big arcade set: 1944 (CPS2) with pre-decoded gfx holds 97
 * entries (12 ROMs, 2 decrypt blobs, Z80 tables, 80 tile blobs, 2 cold-code
 * images), so with 100 slots any OTHER game launched afterwards evicted
 * 1944 entries and the two ping-ponged 256K blob rebuilds.  256 slots
 * (4K of index) hold a CPS2 set beside a dozen smaller games.
 *
 * KEEP ONE FILENAME, shared with stock firmware.  load_metadata() already
 * rejects an index whose size or version does not match, and both guards
 * are upstream, so changing MAX_FILES invalidates it in BOTH directions:
 * boot stock after us and it sees 1628 bytes where it wants 828, resets and
 * rebuilds; boot us after stock and the same happens in reverse.
 *
 * Giving our version its own filename looks tidier and is WRONG.  Each
 * firmware would keep its own index, so ours would survive untouched while
 * stock ran -- and stock writes cache data all over external flash.  On the
 * way back our index would still pass every check while describing
 * addresses stock had since overwritten.  The single shared name is
 * self-correcting precisely because whoever boots second finds a file that
 * fails validation. */
#define METADATA_FILE ODROID_BASE_PATH_SAVES "/flashcachedata.bin"
#define METADATA_VERSION 2
#define MAX_FILES 256

typedef struct {
    uint32_t uid[3];
} CpuUniqueId;

// Metadata for each file
typedef struct
{
    uint32_t file_crc32;
    uint32_t flash_address;
    uint32_t file_size;
    bool valid;
} FileMetadata;

// Global Metadata
typedef struct
{
    uint32_t version;
    CpuUniqueId cpu_unique_id;
    FileMetadata files[MAX_FILES];
    uint32_t flash_write_pointer;  // A value like 0x9YYYYYYY; the current location we should write to.
    uint32_t flash_write_base;     // A value like 0x9YYYYYYY; the starting point we are allowed to write to.
    uint16_t last_written_slot_index;
} Metadata;

static Metadata *metadata = NULL;
static uint32_t flash_write_pointer = 0;

static CpuUniqueId get_cpu_unique_id() {
    CpuUniqueId uid;
    uid.uid[0] = HAL_GetUIDw0();
    uid.uid[1] = HAL_GetUIDw1();
    uid.uid[2] = HAL_GetUIDw2();
    return uid;
}

static uint32_t compute_file_crc32(const char *file_path)
{
    // Cache key: path + modification time + size. Size matters: FAT
    // mtime has 2s granularity and copies can preserve timestamps, so a
    // same-mtime edit would otherwise keep serving the stale flash copy.
    struct stat file_stat;
    if (stat(file_path, &file_stat) == 0) {
        uint32_t crc = crc32_le(0, (const uint8_t *)file_path, strlen(file_path));
        crc = crc32_le(crc, (const uint8_t *)&file_stat.st_mtime, sizeof(file_stat.st_mtime));
        crc = crc32_le(crc, (const uint8_t *)&file_stat.st_size, sizeof(file_stat.st_size));
        return crc;
    } else {
        return crc32_le(0, (const uint8_t *)file_path, strlen(file_path));
    }
}

static uint32_t align_to_next_block(uint32_t pointer)
{
    uint32_t block_size = OSPI_GetSmallestEraseSize(); // Typically 4KB
    return (pointer + block_size - 1) & ~(block_size - 1);
}

/* ---------------------------------------------------------------- live set ---
 *
 * Every address handed out this boot is live — a cache hit exactly as much as a
 * fresh write, since the caller walks away holding it either way — and a write
 * steps over live ranges instead of through them. GBA (and similar) caches a ROM
 * then a code blob; without this the second write can erase the ROM underneath
 * the core. Nothing to release: leaving a game reboots.
 */
/* 96, because a file that does NOT fit here is a file find_write_slot may
 * erase while it is still mapped and in use -- and on a G&W that means
 * erasing flash the CPU is executing from: the machine resets instantly
 * with RAM corrupted (it looks like a brownout, boot_magic garbage, the
 * config magic zeroed).  Budget per CPS1 game: the zip, its program ROMs
 * and woven blobs, the sound ROM, every RAW tile ROM opened to build the
 * pre-decoded blobs, and one entry per blob.  Ghouls'n Ghosts peaks near
 * 52 on a cold card (20 tile ROMs + 12 blobs + 11 others); 40 left it
 * unprotected mid-build and reset the device twice.  Each entry is 8
 * bytes, so headroom is nearly free -- keep it generous. */
/* Must stay ABOVE MAX_FILES: every cached item a game maps is live for the
 * session, and a live entry is what stops a later write erasing something
 * still in use.  Running out only prints a warning, so an undersized array
 * is a silent corruption risk rather than a visible failure. */
#define MAX_LIVE_FILES 288

static uint32_t get_extflash_base(void);

typedef struct {
    uint32_t address;
    uint32_t size;
} LiveRange;

static LiveRange live_files[MAX_LIVE_FILES];
static uint8_t live_file_count = 0;

static void live_add(uint32_t address, uint32_t size)
{
    if (size == 0)
        return;

    for (uint8_t i = 0; i < live_file_count; i++) {
        if (live_files[i].address == address) {
            if (size > live_files[i].size)
                live_files[i].size = size;
            return;
        }
    }
    if (live_file_count >= MAX_LIVE_FILES) {
        printf("flash_alloc: live set full (%d) - a write may overwrite a file in use\n",
               MAX_LIVE_FILES);
        return;
    }
    live_files[live_file_count].address = address;
    live_files[live_file_count].size = size;
    live_file_count++;
}

void flash_alloc_forget_live_files(void)
{
    live_file_count = 0;
}

static bool live_overlaps(uint32_t start, uint32_t end, uint32_t *live_end_out)
{
    for (uint8_t i = 0; i < live_file_count; i++) {
        uint32_t file_start = live_files[i].address;
        uint32_t file_end = file_start + live_files[i].size;
        if (start < file_end && file_start < end) {
            *live_end_out = file_end;
            return true;
        }
    }
    return false;
}

static bool find_write_slot(uint32_t start_pointer, uint32_t erase_size_total,
                            uint32_t *out_pointer)
{
    const uint32_t base = get_extflash_base();
    const uint32_t limit = (uint32_t)&__EXTFLASH_BASE__ + OSPI_GetFlashSize();
    uint32_t p = start_pointer;

    if (erase_size_total > limit - base)
        return false;

    for (int attempt = 0; attempt < MAX_LIVE_FILES * 2 + 2; attempt++) {
        if (p < base || p + erase_size_total > limit)
            p = base;

        uint32_t live_end;
        if (!live_overlaps(p, p + erase_size_total, &live_end)) {
            *out_pointer = p;
            return true;
        }
        p = align_to_next_block(live_end);
    }
    return false;
}

/* Bytes to keep reserved at the bottom of external flash before the ROM cache may
 * write. We honor the LARGER of two reservations:
 *   1. get_ofw_extflash_size() - the active OFW's own external-flash footprint, read
 *      from its vector-table metadata (the stock retro-go behavior); and
 *   2. __EXTFLASH_OFFSET__ - the chainloader's reserved bottom region (its build-time
 *      EXTFLASH_OFFSET, passed in via --defsym).
 * The chainloader packs BOTH games' asset blocks, BOTH OFW backups, and the FAT module
 * store into the bottom __EXTFLASH_OFFSET__ bytes; get_ofw_extflash_size() only describes
 * the single booted game, so on its own it lets the ROM cache erase straight over the OFW
 * backups and FAT store. Using the max keeps the cache clear of all of it, and degrades to
 * the stock behavior when EXTFLASH_OFFSET is 0. */
static uint32_t get_reserved_extflash_size()
{
    uint32_t ofw = get_ofw_extflash_size();
    uint32_t reserved = (uint32_t)&__EXTFLASH_OFFSET__;
    return ofw > reserved ? ofw : reserved;
}

static uint32_t get_extflash_base(void)
{
    return align_to_next_block(((uint32_t)&__EXTFLASH_BASE__) + get_reserved_extflash_size());
}

static void reset_metadata(uint32_t flash_write_base) {
    assert(metadata != NULL);

    memset(metadata, 0, sizeof(Metadata));
    metadata->version = METADATA_VERSION;
    metadata->cpu_unique_id = get_cpu_unique_id();
    metadata->flash_write_base = flash_write_base;
    metadata->flash_write_pointer = flash_write_base;
}

static void initialize_metadata() {
    if (metadata != NULL) {
        return;
    }

    metadata = calloc(1, sizeof(Metadata));
    reset_metadata(0);
}

static void load_metadata()
{
    initialize_metadata();

    uint32_t base = get_extflash_base();

    FILE *file = fopen(METADATA_FILE, "rb");
    if (!file)
    {
        // File does not exist; invalidate cache
        reset_metadata(base);
        return;
    }
    fseek(file, 0, SEEK_END);
    if(ftell(file) != sizeof(Metadata)){
        // Stored metadata doesn't match our current structure; invalidate cache.
        reset_metadata(base);
        goto cleanup;
    }
    fseek(file, 0, SEEK_SET);
    fread(metadata, sizeof(Metadata), 1, file);

    CpuUniqueId cpu_unique_id = get_cpu_unique_id();
    bool metadata_valid = 
        metadata->flash_write_base == base &&
        metadata->version == METADATA_VERSION &&
        memcmp(&metadata->cpu_unique_id, &cpu_unique_id, sizeof(CpuUniqueId)) == 0;
    if(!metadata_valid) {
        // The stored base address does not match whats currently in bank 1; 
        // or metadata version mismatch; 
        // or the cache is from a different device;
        // invalidate cache.
        reset_metadata(base);
        goto cleanup;
    }

    cleanup:
    fclose(file);
}

static void save_metadata()
{
    FILE *file = fopen(METADATA_FILE, "wb");
    if (!file)
        return;
    fwrite(metadata, sizeof(Metadata), 1, file);
    fclose(file);
}

static void initialize_flash_pointer()
{
    load_metadata();
    flash_write_pointer = metadata->flash_write_pointer;
}

static void update_flash_pointer(uint32_t new_pointer)
{
    initialize_metadata();
    metadata->flash_write_pointer = new_pointer;
    save_metadata();
}

static bool is_file_in_flash(uint32_t file_crc32, uint32_t *flash_address, uint32_t *file_size_p)
{
    for (int i = 0; i < MAX_FILES; i++)
    {
        if (metadata->files[i].valid && metadata->files[i].file_crc32 == file_crc32)
        {
            *flash_address = metadata->files[i].flash_address;
            if (*file_size_p == 0)
                *file_size_p = metadata->files[i].file_size;
            return true;
        }
    }
    return false;
}

static void invalidate_overwritten_files(uint32_t flash_address, uint32_t data_size)
{
    for (int i = 0; i < MAX_FILES; i++)
    {
        uint32_t file_start = metadata->files[i].flash_address;
        uint32_t file_end = file_start + metadata->files[i].file_size;
        uint32_t flash_end = flash_address + data_size;

        if (metadata->files[i].valid && (flash_address < file_end && file_start < flash_end))
        {
            metadata->files[i].valid = false;
        }
    }
}

static bool circular_flash_write(const char *file_path,
                                 uint32_t *data_size,
                                 uint32_t *flash_address_out,
                                 bool byte_swap,
                                 file_progress_cb_t progress_cb,
                                 flash_relocate_cb_t relocate_cb)
{
    uint8_t buffer[16 * 1024];
    uint32_t total_bytes_processed = 0;
    uint8_t progress = 0;

    FILE *file = fopen(file_path, "rb");
    if (!file)
        return false;

    if (*data_size == 0) {
        fseek(file, 0, SEEK_END);
        *data_size = ftell(file);
        fseek(file, 0, SEEK_SET);
    }

    if (progress_cb) {
        progress_cb(*data_size, 0, 0);
    }

    uint32_t block_size = OSPI_GetSmallestEraseSize();
    /* The erase (and thus the flash we consume) is block-aligned. */
    uint32_t erase_size_total = (*data_size + block_size - 1) & ~(block_size - 1);

    /* Wrap if it does not fit, step over whatever a caller is reading right now,
     * and say no if there is nowhere left — rather than erase a live file. */
    uint32_t slot;
    if (!find_write_slot(flash_write_pointer, erase_size_total, &slot))
    {
        printf("flash_alloc: no room for %s (%lu bytes) clear of the files in use\n",
               file_path, (unsigned long)*data_size);
        fclose(file);
        return false;
    }
    flash_write_pointer = slot;

    uint32_t old_flash_write_pointer = flash_write_pointer;
    // Translates the address to an offset into external flash.
    uint32_t address_in_flash = flash_write_pointer - (uint32_t)&__EXTFLASH_BASE__;

    OSPI_DisableMemoryMappedMode();

    *flash_address_out = flash_write_pointer;

    /* Erase cursor runs AHEAD of the program cursor, one command at a time.
     * OSPI_Erase() picks the LARGEST erase the chip offers for the current
     * alignment (64KB blocks on the fitted chips) — the old loop forced one
     * 4KB-sector erase per 4KB of file, i.e. 2048 erase commands for an 8MB
     * ROM, which was the bulk of the "Caching game" wait. Interleaving the
     * erase with the SD reads keeps the progress bar moving.
     * (This also fixes a latent overflow: the old loop fread() block_size
     * bytes into the 16KB buffer, which overflows on chips whose smallest
     * erase exceeds 16KB, e.g. the 256KB-sector Spansion config.) */
    uint32_t erase_addr = address_in_flash;
    uint32_t erase_left = erase_size_total;

    while (total_bytes_processed < *data_size) {
        size_t want = sizeof(buffer);
        if (want > *data_size - total_bytes_processed)
            want = *data_size - total_bytes_processed;

        while (erase_addr < address_in_flash + want && erase_left > 0) {
            OSPI_Erase(&erase_addr, &erase_left, true);
            wdog_refresh();
        }

        size_t bytes_read = fread(buffer, 1, want, file);
        if (bytes_read == 0)
            break;

        if (byte_swap) {
            size_t swap_limit = bytes_read & ~(size_t)1; // last odd byte (if any) is left as-is
            for (size_t i = 0; i < swap_limit; i += 2) {
                uint8_t temp = buffer[i];
                buffer[i] = buffer[i + 1];
                buffer[i + 1] = temp;
            }
        }

        /* Last look at the data while it is still in RAM. The chunk size is a
         * multiple of 4 except at end-of-file, and the file starts on an erase
         * block, so a 32-bit field never straddles two chunks. */
        if (relocate_cb) {
            relocate_cb(buffer, bytes_read, total_bytes_processed,
                        (uint8_t *)*flash_address_out, *data_size);
        }

        OSPI_Program(address_in_flash, buffer, bytes_read);

        address_in_flash += bytes_read;
        flash_write_pointer += bytes_read;
        total_bytes_processed += bytes_read;

        if (progress_cb) {
            progress = (uint8_t)((total_bytes_processed * 100) / (*data_size));
            progress_cb(*data_size, total_bytes_processed, progress);
        }

        if (bytes_read < want) {
            break;
        }
    }

    OSPI_EnableMemoryMappedMode();
    fclose(file);

    /* The next file must start on an erase-block boundary (the old loop
     * advanced in whole blocks; we advance by real bytes now). */
    flash_write_pointer = (flash_write_pointer + block_size - 1) & ~(block_size - 1);

    /* Invalidate everything the ERASE touched, not just the programmed bytes —
     * the aligned tail past the file end is wiped too, and a cached file whose
     * data began there would otherwise stay marked valid over blank flash. */
    invalidate_overwritten_files(old_flash_write_pointer, erase_size_total);
    update_flash_pointer(flash_write_pointer);

    return true;
}

/* ---- derived-data blobs (not files) ----------------------------------
 * Same cache, RAM source: store a buffer under a caller-chosen key
 * string. Used by the arcade core to keep DECOMPRESSED zip entries in
 * memory-mapped flash (decompress once, then serve gfx/data ROMs
 * straight from QSPI with zero RAM cost — the megadrive principle).
 * The key should be content-addressed (e.g. include the zip entry's
 * own CRC32) so a changed zip naturally misses the cache. */

static uint32_t compute_key_crc32(const char *key)
{
    return crc32_le(0, (const uint8_t *)key, strlen(key));
}

const uint8_t *lookup_data_in_flash(const char *key, uint32_t *size_out)
{
    initialize_metadata();
    initialize_flash_pointer();

    uint32_t key_crc = compute_key_crc32(key);
    uint32_t flash_address;
    uint32_t size = 0;

    if (is_file_in_flash(key_crc, &flash_address, &size))
    {
        live_add(flash_address, size);
        free(metadata);
        metadata = NULL;
        if (size_out)
            *size_out = size;
        return (const uint8_t *)flash_address;
    }
    free(metadata);
    metadata = NULL;
    return NULL;
}

/* ---- streaming blob store (see gw_flash_alloc.h) ------------------- */

bool store_data_begin(flash_stream_t *st, const char *key, uint32_t total_size)
{
    memset(st, 0, sizeof(*st));
    initialize_metadata();
    initialize_flash_pointer();

    st->key_crc = compute_key_crc32(key);
    st->total = total_size;

    uint32_t block_size = OSPI_GetSmallestEraseSize();
    st->erase_size_total = (total_size + block_size - 1) & ~(block_size - 1);

    uint32_t slot;
    if (!find_write_slot(flash_write_pointer, st->erase_size_total, &slot)) {
        printf("flash_alloc: no room for streamed blob '%s' (%lu bytes)\n",
               key, (unsigned long)total_size);
        free(metadata);
        metadata = NULL;
        return false;
    }
    flash_write_pointer = slot;
    st->flash_address = flash_write_pointer;
    st->prog_addr  = flash_write_pointer - (uint32_t)&__EXTFLASH_BASE__;
    st->erase_addr = st->prog_addr;
    st->erase_left = st->erase_size_total;
    st->active = true;
    return true;
}

bool store_data_append(flash_stream_t *st, const uint8_t *buf, uint32_t len)
{
    if (!st->active || st->done + len > st->total)
        return false;

    /* Memory-mapped mode is off only for the program itself: the producer
     * (the inflate) reads its compressed input from the mapped flash
     * between calls, so it must be back on when we return. */
    OSPI_DisableMemoryMappedMode();
    while (st->erase_addr < st->prog_addr + len && st->erase_left > 0) {
        OSPI_Erase(&st->erase_addr, &st->erase_left, true);
        wdog_refresh();
    }
    OSPI_Program(st->prog_addr, (uint8_t *)buf, len);
    OSPI_EnableMemoryMappedMode();

    st->prog_addr += len;
    st->done += len;
    wdog_refresh();
    return true;
}

void store_data_abort(flash_stream_t *st)
{
    if (!st->active)
        return;
    st->active = false;
    free(metadata);
    metadata = NULL;
}

const uint8_t *store_data_finish(flash_stream_t *st)
{
    if (!st->active || st->done != st->total) {
        store_data_abort(st);
        return NULL;
    }
    st->active = false;

    uint32_t block_size = OSPI_GetSmallestEraseSize();
    flash_write_pointer = (st->flash_address + st->total + block_size - 1)
                          & ~(block_size - 1);
    invalidate_overwritten_files(st->flash_address, st->erase_size_total);
    update_flash_pointer(flash_write_pointer);

    live_add(st->flash_address, st->total);

    bool updated = false;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!metadata->files[i].valid) {
            metadata->files[i].file_crc32 = st->key_crc;
            metadata->files[i].flash_address = st->flash_address;
            metadata->files[i].file_size = st->total;
            metadata->files[i].valid = true;
            metadata->last_written_slot_index = i;
            updated = true;
            break;
        }
    }
    if (!updated) {
        metadata->last_written_slot_index =
            (metadata->last_written_slot_index + 1) % MAX_FILES;
        FileMetadata *f = &metadata->files[metadata->last_written_slot_index];
        f->file_crc32 = st->key_crc;
        f->flash_address = st->flash_address;
        f->file_size = st->total;
        f->valid = true;
    }
    save_metadata();
    wdog_refresh();
    free(metadata);
    metadata = NULL;
    return (const uint8_t *)st->flash_address;
}

static void (*store_progress_cb)(uint32_t done, uint32_t total);

void store_data_set_progress_cb(void (*cb)(uint32_t done, uint32_t total))
{
    store_progress_cb = cb;
}

const uint8_t *store_data_in_flash(const char *key, const uint8_t *data,
                                   uint32_t data_size)
{
    initialize_metadata();
    initialize_flash_pointer();

    uint32_t key_crc = compute_key_crc32(key);
    uint32_t flash_address;
    uint32_t cached_size = 0;

    if (is_file_in_flash(key_crc, &flash_address, &cached_size) &&
        cached_size == data_size)
    {
        live_add(flash_address, cached_size);
        free(metadata);
        metadata = NULL;
        return (const uint8_t *)flash_address;
    }

    uint32_t block_size = OSPI_GetSmallestEraseSize();
    uint32_t erase_size_total = (data_size + block_size - 1) & ~(block_size - 1);
    uint32_t slot;
    if (!find_write_slot(flash_write_pointer, erase_size_total, &slot))
    {
        printf("flash_alloc: no room for blob '%s' (%lu bytes)\n",
               key, (unsigned long)data_size);
        free(metadata);
        metadata = NULL;
        return NULL;
    }
    flash_write_pointer = slot;
    flash_address = flash_write_pointer;
    uint32_t address_in_flash = flash_write_pointer - (uint32_t)&__EXTFLASH_BASE__;

    OSPI_DisableMemoryMappedMode();
    /* erase cursor runs ahead of the program cursor, as in
     * circular_flash_write */
    uint32_t erase_addr = address_in_flash;
    uint32_t erase_left = erase_size_total;
    uint32_t done = 0;
    while (done < data_size) {
        uint32_t want = 16 * 1024;
        if (want > data_size - done)
            want = data_size - done;
        while (erase_addr < address_in_flash + want && erase_left > 0) {
            OSPI_Erase(&erase_addr, &erase_left, true);
            wdog_refresh();
        }
        OSPI_Program(address_in_flash, (uint8_t *)data + done, want);
        if (store_progress_cb)
            store_progress_cb(done + want, data_size);
        address_in_flash += want;
        done += want;
        wdog_refresh();
    }
    OSPI_EnableMemoryMappedMode();

    flash_write_pointer = (flash_write_pointer + data_size + block_size - 1)
                          & ~(block_size - 1);
    invalidate_overwritten_files(flash_address, erase_size_total);
    update_flash_pointer(flash_write_pointer);

    live_add(flash_address, data_size);
    bool metadata_updated = false;
    for (int i = 0; i < MAX_FILES; i++)
    {
        if (!metadata->files[i].valid)
        {
            metadata->files[i].file_crc32 = key_crc;
            metadata->files[i].flash_address = flash_address;
            metadata->files[i].file_size = data_size;
            metadata->files[i].valid = true;
            metadata->last_written_slot_index = i;
            metadata_updated = true;
            break;
        }
    }
    if (!metadata_updated)
    {
        metadata->last_written_slot_index =
            (metadata->last_written_slot_index + 1) % MAX_FILES;
        FileMetadata *f = &metadata->files[metadata->last_written_slot_index];
        f->file_crc32 = key_crc;
        f->flash_address = flash_address;
        f->file_size = data_size;
        f->valid = true;
    }
    save_metadata();
    wdog_refresh();
    free(metadata);
    metadata = NULL;
    return (const uint8_t *)flash_address;
}

// Clear all metadata and delete the metadata file
void flash_alloc_reset()
{
    if (metadata)
    {
        free(metadata);
        metadata = NULL;
    }
    remove(METADATA_FILE);
}

uint8_t *store_file_in_flash(const char *file_path, uint32_t *file_size_p, bool byte_swap, file_progress_cb_t progress_cb)
{
    return store_file_in_flash_relocate(file_path, file_size_p, byte_swap, progress_cb, NULL);
}

uint8_t *store_file_in_flash_relocate(const char *file_path, uint32_t *file_size_p, bool byte_swap,
                                      file_progress_cb_t progress_cb, flash_relocate_cb_t relocate_cb)
{
    initialize_metadata();
    initialize_flash_pointer();
    // TODO : append file modification time to filepath for crc32
    // to handle case where rom file in sd card has been modified
    uint32_t file_crc32 = compute_file_crc32(file_path);
    uint32_t flash_address;

    if (is_file_in_flash(file_crc32, &flash_address, file_size_p))
    {
        /* A hit is as live as a write: the caller walks away holding this address. */
        live_add(flash_address, *file_size_p);
        free(metadata);
        metadata = NULL;
        return (uint8_t *)flash_address;
    }

    if (!circular_flash_write(file_path, file_size_p, &flash_address, byte_swap, progress_cb, relocate_cb))
    {
        free(metadata);
        metadata = NULL;
        return NULL;
    }

    live_add(flash_address, *file_size_p);

    bool metadata_updated = false;

    for (int i = 0; i < MAX_FILES; i++)
    {
        if (!metadata->files[i].valid)
        {
            metadata->files[i].file_crc32 = file_crc32;
            metadata->files[i].flash_address = flash_address;
            metadata->files[i].file_size = *file_size_p;
            metadata->files[i].valid = true;
            metadata->last_written_slot_index = i;
            metadata_updated = true;
            break;
        }
    }

    if (!metadata_updated)
    {
        metadata->last_written_slot_index = (metadata->last_written_slot_index + 1) % MAX_FILES;
        metadata->files[metadata->last_written_slot_index].file_crc32 = file_crc32;
        metadata->files[metadata->last_written_slot_index].flash_address = flash_address;
        metadata->files[metadata->last_written_slot_index].file_size = *file_size_p;
        metadata->files[metadata->last_written_slot_index].valid = true;
    }

    save_metadata();
    wdog_refresh();
    free(metadata);
    metadata = NULL;
    return (uint8_t *)flash_address;
}
