#include <stdint.h>
#include <stddef.h>
#include "nes_fceu_mappers.h"
#include "rg_storage.h"

/* mappers.pak layout (little-endian), see scripts/gen_mappers_pack.py:
 *   Header (16 bytes): 'MPAK', uint32 version, uint32 num_entries, uint32 reserved
 *   Index (num_entries * 8 bytes) indexed by mapper number: uint32 offset, uint32 size
 *   Blobs concatenated after the index. offset == size == 0 => mapper absent.
 */
#define PACK_MAGIC 0x4B41504Du /* 'M','P','A','K' */
#define PACK_HEADER_SIZE 16
#define PACK_INDEX_ENTRY_SIZE 8

static uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t fceumm_load_mapper(uint16_t mapper_number, uint8_t *dest, size_t dest_capacity) {
    FILE *file = fopen(FCEUMM_MAPPER_PACK_FILE, "rb");
    if (!file) {
        return 0;
    }

    uint8_t header[PACK_HEADER_SIZE];
    if (fread(header, 1, PACK_HEADER_SIZE, file) != PACK_HEADER_SIZE ||
        rd_u32le(header) != PACK_MAGIC) {
        fclose(file);
        return 0;
    }

    uint32_t num_entries = rd_u32le(header + 8);
    if (mapper_number >= num_entries) {
        fclose(file);
        return 0;
    }

    long index_pos = (long)PACK_HEADER_SIZE + (long)mapper_number * PACK_INDEX_ENTRY_SIZE;
    uint8_t entry[PACK_INDEX_ENTRY_SIZE];
    if (fseek(file, index_pos, SEEK_SET) != 0 ||
        fread(entry, 1, PACK_INDEX_ENTRY_SIZE, file) != PACK_INDEX_ENTRY_SIZE) {
        fclose(file);
        return 0;
    }
    fclose(file);

    uint32_t offset = rd_u32le(entry);
    uint32_t size = rd_u32le(entry + 4);
    if (size == 0 || size > dest_capacity) {
        return 0;
    }

    return rg_storage_copy_file_range_to_ram(FCEUMM_MAPPER_PACK_FILE, dest, offset, size, NULL);
}
