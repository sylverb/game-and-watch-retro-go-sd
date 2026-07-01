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
#include "gw_layout_superblock.h"

#define METADATA_FILE ODROID_BASE_PATH_SAVES "/flashcachedata.bin"
#define METADATA_VERSION 1
#define MAX_FILES 50

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
    // Include file modification time or content in CRC32 calculation
    struct stat file_stat;
    if (stat(file_path, &file_stat) == 0) {
        uint32_t crc = crc32_le(0, (const uint8_t *)file_path, strlen(file_path));
        crc = crc32_le(crc, (const uint8_t *)&file_stat.st_mtime, sizeof(file_stat.st_mtime));
        return crc;
    } else {
        return crc32_le(0, (const uint8_t *)file_path, strlen(file_path));
    }
    return 0;
}

static uint32_t align_to_next_block(uint32_t pointer)
{
    uint32_t block_size = OSPI_GetSmallestEraseSize(); // Typically 4KB
    return (pointer + block_size - 1) & ~(block_size - 1);
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
    uint32_t ofw = gw_layout_reserved_size();
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
                                 file_progress_cb_t progress_cb)
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

    uint32_t flash_write_base = get_extflash_base();
    uint32_t block_size = OSPI_GetSmallestEraseSize();
    /* The erase (and thus the flash we consume) is block-aligned. */
    uint32_t erase_size_total = (*data_size + block_size - 1) & ~(block_size - 1);

    /* If there is not enough space available, wrap to the start of the cache. */
    if (flash_write_pointer - flash_write_base + erase_size_total >
        gw_layout_extflash_size() - get_reserved_extflash_size())
    {
        flash_write_pointer = flash_write_base;
    }

    /* Data larger than the usable flash cache — abort. */
    if (flash_write_pointer - flash_write_base + erase_size_total >
        gw_layout_extflash_size() - get_reserved_extflash_size())
    {
        fclose(file);
        return false;
    }

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
    initialize_metadata();
    initialize_flash_pointer();
    // TODO : append file modification time to filepath for crc32
    // to handle case where rom file in sd card has been modified
    uint32_t file_crc32 = compute_file_crc32(file_path);
    uint32_t flash_address;

    if (is_file_in_flash(file_crc32, &flash_address, file_size_p))
    {
        free(metadata);
        metadata = NULL;
        return (uint8_t *)flash_address;
    }

    if (!circular_flash_write(file_path, file_size_p, &flash_address, byte_swap, progress_cb))
    {
        free(metadata);
        metadata = NULL;
        return NULL;
    }

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
