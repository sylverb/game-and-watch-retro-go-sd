#include <string.h>
#include "msx_database.h"
#include "hw_sha1.h"
#include "odroid_overlay.h"
#include "gw_linker.h"
#include "main.h"
#include "gw_malloc.h"
#ifndef GNW_DISABLE_COMPRESSION
#include "main_msx.h"
#include "lzma.h"
#endif

#define ENTRY_SIZE (SHA1_COMPACT_SIZE + 3) // 6 bytes of trunked sha1 + 3 bytes of configuration

/* IDE HDD images (.dsk > 720 KiB): hash only the boot/partition area used for
 * game identification (same cutoff as MSX_GAME_HDIDE detection in main_msx.c). */
#define MSX_HDD_SHA1_BYTES (720U * 1024U)
#define MSX_DISK_EXTENSION "dsk"
#define MSX_CDK_TRACK_SIZE (9U * 512U)
/* CDK index size above this threshold => compressed HDD (main_msx.c). */
#define MSX_CDK_INDEX_MAX_FLOPPY 0x288U
#define MSX_HDD_SHA1_CDK_TRACKS (MSX_HDD_SHA1_BYTES / MSX_CDK_TRACK_SIZE)

static int msx_file_is_hdd_image(const retro_emulator_file_t *active_file)
{
    return (active_file != NULL &&
            active_file->ext != NULL &&
            strcmp(active_file->ext, MSX_DISK_EXTENSION) == 0 &&
            active_file->size > MSX_HDD_SHA1_BYTES);
}

static int8_t msx_find_rom_info(const uint8_t *target_sha1, RomInfo *result) {
    long low = 0;
    FILE *file = fopen("/bios/msx/msxromdb.bin", "rb");
    if (!file) {
        return -1;
    }

    fseek(file, 0, SEEK_END);
    long high = ftell(file) / ENTRY_SIZE - 1;

    // sha1 are sorted in database file, use binary search
    // to find the target sha1
    while (low <= high) {
        long mid = (low + high) / 2;

        fseek(file, mid * ENTRY_SIZE, SEEK_SET);
        fread(result, ENTRY_SIZE, 1, file);

        int cmp = memcmp(target_sha1, result->sha1, SHA1_COMPACT_SIZE);
        
        if (cmp == 0) {
            fclose(file);
            return 1;  // SHA1 found
        } else if (cmp < 0) {
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }

    fclose(file);
    return 0; // SHA1 Not found
}

static uint32_t msx_read_u32_le(const uint8_t* data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

#if !defined(GNW_DISABLE_COMPRESSION) && SD_CARD == 0
static int8_t msx_calculate_sha1_cdk_stream(const uint8_t *src_data, uint32_t src_size, uint8_t *output_sha1) {
    uint32_t first_offset;
    uint32_t block_count;
    uint32_t blocks_to_hash;
    uint8_t *track_buffer = (uint8_t *)&_MSX_ROM_UNPACK_BUFFER;
    HASH_HandleTypeDef hhash;

    if (src_data == NULL || src_size < 8 || memcmp(src_data, "lzma", 4) != 0) {
        return 0;
    }

    first_offset = msx_read_u32_le(src_data + 4);
    if (first_offset < 8 || first_offset > src_size || ((first_offset - 4) % 4) != 0) {
        return 0;
    }

    block_count = (first_offset - 4) / 4;
    if (block_count == 0 || MSX_CDK_TRACK_SIZE > (uint32_t)&_MSX_ROM_UNPACK_BUFFER_SIZE) {
        return 0;
    }

    /* Floppy CDK: hash every decompressed track.  HDD CDK (large index): only
     * the first 720 KiB of decompressed data, i.e. MSX_HDD_SHA1_CDK_TRACKS
     * tracks of MSX_CDK_TRACK_SIZE bytes each. */
    if (first_offset > MSX_CDK_INDEX_MAX_FLOPPY) {
        blocks_to_hash = block_count;
        if (blocks_to_hash > MSX_HDD_SHA1_CDK_TRACKS) {
            blocks_to_hash = MSX_HDD_SHA1_CDK_TRACKS;
        }
    } else {
        blocks_to_hash = block_count;
    }

    __HAL_RCC_HASH_CLK_ENABLE();
    HAL_HASH_DeInit(&hhash);
    hhash.Init.DataType = HASH_DATATYPE_8B;
    if (HAL_HASH_Init(&hhash) != HAL_OK) {
        __HAL_RCC_HASH_CLK_DISABLE();
        return 0;
    }

    for (uint32_t i = 0; i < blocks_to_hash; i++) {
        uint32_t off = msx_read_u32_le(src_data + 4 + i * 4);
        uint32_t end = (i + 1 < block_count) ? msx_read_u32_le(src_data + 4 + (i + 1) * 4) : src_size;
        if (off >= end || end > src_size) {
            HAL_HASH_DeInit(&hhash);
            __HAL_RCC_HASH_CLK_DISABLE();
            return 0;
        }

        size_t out_block_size = lzma_inflate(track_buffer, MSX_CDK_TRACK_SIZE, src_data + off, end - off);
        if (out_block_size != MSX_CDK_TRACK_SIZE) {
            HAL_HASH_DeInit(&hhash);
            __HAL_RCC_HASH_CLK_DISABLE();
            return 0;
        }

        if (HAL_HASH_SHA1_Accumulate(&hhash, track_buffer, out_block_size) != HAL_OK) {
            HAL_HASH_DeInit(&hhash);
            __HAL_RCC_HASH_CLK_DISABLE();
            return 0;
        }
    }

    if (HAL_HASH_SHA1_Start(&hhash, track_buffer, 0, output_sha1, HAL_MAX_DELAY) != HAL_OK) {
        HAL_HASH_DeInit(&hhash);
        __HAL_RCC_HASH_CLK_DISABLE();
        return 0;
    }

    HAL_HASH_DeInit(&hhash);
    __HAL_RCC_HASH_CLK_DISABLE();
    return 1;
}
#endif

int8_t msx_get_game_info(const retro_emulator_file_t *active_file, RomInfo *result) {
    uint8_t sha1[SHA1_SIZE];
    if (active_file == NULL) {
        return 0;
    }
    ram_start = (uint32_t)&_MSX_ROM_UNPACK_BUFFER;
#ifndef GNW_DISABLE_COMPRESSION
#if SD_CARD == 0
    if (strcmp(active_file->ext, "lzma") == 0) {
        printf("Decompressing MSX ROM...\n");
        uint32_t src_size = 0;
        const uint8_t *src_data = odroid_overlay_cache_file_in_flash(active_file->path, &src_size, false);
        uint8_t *dest = (uint8_t *)&_MSX_ROM_UNPACK_BUFFER;
        uint32_t available_size = (uint32_t)&_MSX_ROM_UNPACK_BUFFER_SIZE;

        msx_rom_decompress_size = lzma_inflate(dest, available_size, src_data, src_size);
        ram_start += msx_rom_decompress_size;
        if (calculate_sha1_hw(dest, msx_rom_decompress_size, sha1)) {
            return msx_find_rom_info(sha1, result);
        }
        return 0;
    }

    if (strcmp(active_file->ext, "cdk") == 0) {
        uint32_t src_size = 0;
        const uint8_t *src_data = odroid_overlay_cache_file_in_flash(active_file->path, &src_size, false);
        if (msx_calculate_sha1_cdk_stream(src_data, src_size, sha1)) {
            return msx_find_rom_info(sha1, result);
        }
        return 0;
    }
#endif
#endif

    if (msx_file_is_hdd_image(active_file)) {
        if (calculate_sha1_file_limit(active_file->path, MSX_HDD_SHA1_BYTES, sha1)) {
            return msx_find_rom_info(sha1, result);
        }
        return 0;
    }

    if (calculate_sha1_file(active_file->path, sha1)) {
        return msx_find_rom_info(sha1, result);
    }

    return 0;
}
