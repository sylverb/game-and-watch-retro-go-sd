#ifndef _GW_MALLOC_H_
#define _GW_MALLOC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

extern uint32_t ram_start;

/*
 * Memory pools
 * ------------
 * AHB  (ahb_malloc/ahb_calloc → malloc/calloc): newlib heap in AHB SRAM.
 *      Freeable with free(). No pool-wide reset — live allocations are kept.
 * DTCM (dtc_init/dtc_malloc/dtc_calloc): bump from DTCM ORIGIN to the
 *      stack redzone. No free; forgotten by dtc_init().
 * ITC  (itc_init/itc_malloc/itc_calloc): bump; forgotten by itc_init().
 * RAM_EMU (ram_init/ram_malloc/ram_calloc): bump from ram_start; forgotten
 *      by ram_init() (current_ram_pointer rewind).
 */

void *ahb_malloc(size_t size);
void *ahb_calloc(size_t count, size_t size);

void itc_init(void);
void *itc_malloc(size_t size);
void *itc_calloc(size_t count, size_t size);

void ram_init(void);
size_t ram_get_free_size(void);
void *ram_malloc(size_t size);
void *ram_calloc(size_t count, size_t size);

void dtc_init(void);
void *dtc_malloc(size_t size);
void *dtc_calloc(size_t count, size_t size);

#ifdef __cplusplus
}
#endif

#endif
