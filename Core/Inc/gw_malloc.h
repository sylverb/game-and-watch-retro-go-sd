#ifndef _GW_MALLOC_H_
#define _GW_MALLOC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

extern uint32_t ram_start;

void ahb_init();
void *ahb_malloc(size_t size);
void *ahb_only_malloc(size_t size);
void *ahb_calloc(size_t count,size_t size);

void itc_init();
void *itc_malloc(size_t size);
void *itc_calloc(size_t count,size_t size);

size_t ram_get_free_size();
void *ram_malloc(size_t size);
void *ram_calloc(size_t count,size_t size);

/* DTCM stdlib heap (_heap_start.._heap_end). Use for emulator overlays
 * (PICO-8 p8ram, PCE work RAM, etc.) that need free/realloc. */
void *dtcm_malloc(size_t size);
void *dtcm_calloc(size_t count, size_t size);
void dtcm_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
