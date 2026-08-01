#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#define FCEUMM_MAPPER_PACK_FILE "/cores/mappers/mappers.pak"

/* Load the mapper blob for `mapper_number` from the mappers pack into `dest`.
 * Returns the number of bytes loaded, or 0 when the mapper is not present in the
 * pack (shared board code lives in the main core, or the ROM's mapper was pruned
 * out of this build). */
size_t fceumm_load_mapper(uint16_t mapper_number, uint8_t *dest, size_t dest_capacity);
