#pragma GCC optimize("O0")

extern "C" {
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gw_linker.h"
}
#include "gw_malloc.h"
#include <cstddef>

static size_t gb_bss_end;

static size_t heapsize;
static uint8_t *badheap;
static size_t heap_offset;
static bool itc_alloc;
#define DBG(...) printf(__VA_ARGS__)

extern "C" void heap_itc_alloc(bool itc) {
    itc_alloc = itc;
}

extern "C" void *heap_alloc_mem(size_t s) {
    DBG("heap_alloc_mem %d\n", s);
    void *ptr = (void *)0xffffffff;
    if (itc_alloc) {
        DBG("-> itc_malloc %d\n", s);
        ptr = itc_malloc(s);
    }
    if (ptr == (void *)0xffffffff) {
        size_t aligned = (s + 0b11) & ~0b11; // 32-bit
        if (heap_offset + aligned <= heapsize) {
            ptr = &badheap[heap_offset];
            heap_offset += aligned;
        } else {
            /* RAM_EMU bump heap full — spill into AHB (asserts if that OOMs too). */
            DBG("-> ahb_only_malloc %d\n", (int)s);
            ptr = ahb_only_malloc(s);
        }
    }
    memset(ptr, 0, s);

    return ptr;
}

void *operator new[](std::size_t s)
{
    DBG("new[] %d\n", s);
    return heap_alloc_mem(s);
}

void *operator new(std::size_t s)
{
    DBG("new %d\n", s);
    return heap_alloc_mem(s);
}

void operator delete(void *p)
{
    // We do not have a real malloc/free mechanism
    // so delete will not free anything.
    DBG("d 0x%08x\n", (unsigned int)p);
}

void operator delete[](void *p)
{
    // We do not have a real malloc/free mechanism
    // so delete will not free anything.
    DBG("d[] 0x%08x\n", (unsigned int)p);
}

extern "C" size_t heap_free_mem()
{
    return (size_t)(heapsize - heap_offset);
}

extern "C" void cpp_heap_init(size_t bss_end)
{
    itc_alloc = false;
    gb_bss_end = bss_end;

    heap_offset = 0;
    heapsize = (size_t) &__RAM_END__ - gb_bss_end;
    badheap = (uint8_t *) ((gb_bss_end + 0b1111) & ~0b1111);
}
