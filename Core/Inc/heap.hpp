#pragma once

#ifdef __cplusplus
extern "C" {
#endif
void *heap_alloc_mem(size_t s);
size_t heap_free_mem();
/* Largest single heap_alloc_mem()/operator new that can currently succeed.
 * Max of AHB / RAM_EMU / DTCM (and ITC iff heap_itc_alloc(true)) — one
 * allocation lands in one pool, so this is not the sum. SDK-only
 * (cores/homebrews); not an ABI slot. */
size_t heap_get_largest_free_size(void);
void heap_itc_alloc(bool itc);
void cpp_heap_init(size_t bss_end);
#ifdef __cplusplus
}
#endif