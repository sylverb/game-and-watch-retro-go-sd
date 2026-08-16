#ifndef _GW_MALLOC_H_
#define _GW_MALLOC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

void *ahb_malloc(size_t size);
void *ahb_calloc(size_t count, size_t size);
static inline size_t ahb_get_free_size(void) { return 32u * 1024u * 1024u; }

void dtc_init(void);
void *dtc_malloc(size_t size);
void *dtc_calloc(size_t count, size_t size);
static inline size_t dtc_get_free_size(void) { return 32u * 1024u * 1024u; }

void itc_init(void);
void *itc_malloc(size_t size);
void *itc_calloc(size_t count, size_t size);
static inline size_t itc_get_free_size(void) { return 32u * 1024u * 1024u; }

void ram_init(void);
static inline size_t ram_get_free_size(void) { return 32u * 1024u * 1024u; }
void *ram_malloc(size_t size);
void *ram_calloc(size_t count, size_t size);

#ifdef __cplusplus
}
#endif

#endif
