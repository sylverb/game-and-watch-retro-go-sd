#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "gw_linker.h"
#include "gw_malloc.h"

static char *heap_end = 0;

/* newlib-nano free-list node (libc/stdlib/nano-mallocr.c). size is the
 * whole chunk including the header; usable bytes are size minus the
 * 8-byte aligned header. */
struct nano_malloc_chunk {
    size_t size;
    struct nano_malloc_chunk *next;
};
extern struct nano_malloc_chunk *__malloc_free_list;

#define AHB_MALLOC_OVERHEAD 8u

static size_t ahb_usable(size_t raw)
{
    return (raw > AHB_MALLOC_OVERHEAD) ? (raw - AHB_MALLOC_OVERHEAD) : 0;
}

size_t ahb_get_free_size(void)
{
    char *cur = heap_end ? heap_end : (char *)&_heap_start;
    char *lim = (char *)&_heap_end;
    size_t largest = (cur < lim) ? ahb_usable((size_t)(lim - cur)) : 0;

    for (struct nano_malloc_chunk *p = __malloc_free_list; p; p = p->next) {
        size_t usable = ahb_usable(p->size);
        if (usable > largest)
            largest = usable;
    }
    return largest;
}

void *
_sbrk (int incr)
{
    char *        prev_heap_end;

    if (heap_end == 0)
        heap_end = (char *) &_heap_start;

    if ((heap_end + incr) >= (char *)(&_heap_end)) {
        printf("HEAP OOM: need=%d used=%d/%d\n",
               incr, (int)(heap_end - (char *)&_heap_start),
               (int)((char *)&_heap_end - (char *)&_heap_start));
        return (void *)-1;
// Do not assert
//        assert(0);
    }

    prev_heap_end = heap_end;
    heap_end += incr;

    return (void *) prev_heap_end;
}

#ifdef DEBUG_RG_ALLOC

static struct {
    size_t   total_alloc_bytes;
    size_t   total_alloc_bytes_actual;
    uint32_t total_alloc_num;
} alloc_data;

void *rg_alloc(size_t size, uint32_t caps)
{
    uint32_t *p = malloc(size + sizeof(uint32_t));

    alloc_data.total_alloc_bytes += size;
    alloc_data.total_alloc_bytes_actual += size + sizeof(uint32_t);

#ifdef DEBUG_RG_ALLOC_PRINT
    printf("A %d %d %d %p\n", size, alloc_data.total_alloc_bytes, alloc_data.total_alloc_bytes_actual, p);
#endif

    p[0] = size;

    return &p[1];
}

void *rg_calloc(size_t nmemb, size_t size)
{
    uint8_t *p = rg_alloc(nmemb * size, 0);

    memset(p, '\x00', nmemb * size);

    return p;
}

void rg_free(void *ptr)
{
    assert(ptr != NULL);

    uint32_t *p = ((uint32_t *) ptr) - 1;

    alloc_data.total_alloc_bytes -= p[0];
    alloc_data.total_alloc_bytes_actual -= p[0] + sizeof(uint32_t);

#ifdef DEBUG_RG_ALLOC_PRINT
    printf("F %lu %d %d %p\n",
            p[0],
            alloc_data.total_alloc_bytes,
            alloc_data.total_alloc_bytes_actual,
            p);
#endif

    free(p);
}

void *rg_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) {
        return rg_alloc(size, 0);
    }

    uint32_t *p = ((uint32_t *) ptr) - 1;

    alloc_data.total_alloc_bytes -= p[0];
    alloc_data.total_alloc_bytes_actual -= p[0] + sizeof(uint32_t);

    alloc_data.total_alloc_bytes += size;
    alloc_data.total_alloc_bytes_actual += size + sizeof(uint32_t);

#ifdef DEBUG_RG_ALLOC_PRINT
    printf("R %u %d %d %p\n",
            size,
            alloc_data.total_alloc_bytes,
            alloc_data.total_alloc_bytes_actual,
            p);
#endif

    return realloc(p, size);
}

#endif
