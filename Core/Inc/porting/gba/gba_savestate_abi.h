#pragma once

/* The slim-savestate surface of gpsp, declared here rather than by including the
 * core's savestate.h: that header drags in libretro types and register-name
 * macros that collide with CMSIS in the porting layer's translation unit.
 *
 * Keep in step with external/gpsp/savestate.h. */

#include <stdint.h>
#include <stdbool.h>

/* The bson document with the six bulk buffers left out. The real one is ~4.3KB;
 * this is the ceiling the core guarantees it stays under. */
#define GBA_STATE_SLIM_SIZE (32 * 1024)

typedef struct {
    void     *ptr;
    unsigned  len;
} gba_bulk_region_t;

/* iwram / ewram / vram / oam / palette / ioregs — ~390KB, streamed straight to
 * storage instead of being staged in a buffer this device does not have. */
const gba_bulk_region_t *gba_bulk_regions(unsigned *count);

void gba_save_state_slim(void *dst);
bool gba_load_state_slim(const void *src);
