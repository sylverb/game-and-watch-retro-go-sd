/*
 * Shared C++ runtime support for standalone "core" binaries.
 *
 * Pulled in automatically by cores/_template/Makefile whenever a core sets
 * CORE_CXX_SOURCES (see that variable's doc comment) — cores/gb_tgbdual is
 * the first (and, as of this writing, only) consumer.
 *
 * A C++ core builds -nostdlib, no libstdc++ (see cores/_template/Makefile's
 * CXXFLAGS comment on -fno-exceptions/-fno-rtti/-fno-threadsafe-statics/
 * -fno-use-cxa-atexit): none of those flags eliminate operator new/delete
 * or virtual-dispatch support, which the compiler always assumes exist as
 * plain linkable symbols regardless. This file provides exactly those,
 * nothing more:
 *
 *   - operator new/new[]/delete/delete[] (+ the sized-delete overloads
 *     current arm-none-eabi-g++'s default -std picks up) — routed through
 *     heap_alloc_mem() below, a small allocator on top of the existing
 *     ram_malloc()/itc_malloc()/ahb_calloc() core_common trampolines.
 *     Behaviorally the same allocator as the (now unused, monolithic-build-
 *     only) Core/Src/heap.cpp: cores/gb_tgbdual/main_gb_tgbdual.cpp and
 *     every .cpp file under external/tgbdual-go/gb_core call these exact names directly
 *     (declared in Core/Inc/heap.hpp) — heap_itc_alloc(true) temporarily
 *     routes allocations through the 64KB ITC pool (used for GB WRAM/VRAM
 *     banking when cart SRAM mapping doesn't need it), falling back to the
 *     shared RAM_EMU bump pool and then AHB SRAM when that's exhausted.
 *   - __cxa_pure_virtual: GCC always emits a reference to this in an
 *     abstract base class's vtable (for the pure-virtual slots), even
 *     though -fno-rtti plus every pure virtual actually being overridden
 *     means nothing should ever legitimately call through it — the linker
 *     still needs the symbol to exist.
 *   - __cxa_atexit/__dso_handle: -fno-use-cxa-atexit (see CXXFLAGS) means
 *     the compiler won't emit __cxa_atexit calls for global objects with
 *     non-trivial destructors, but some libgcc/libstdc++ header paths
 *     reference the symbol unconditionally — stubbed as a no-op
 *     registration (this image never "exits", see core_ram_emu.ld's
 *     .fini_array discard, so there's nothing to ever call these
 *     registered destructors anyway).
 */
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <sys/reent.h>

extern "C" {
#include "gw_malloc.h"
}

/* ====================================================================
 * heap_alloc_mem() — ITCM only if heap_itc_alloc(true); otherwise AHB
 * (freeable newlib heap) first, then RAM_EMU bump, then DTCM bump.
 * ==================================================================== */
static bool s_heap_itc_alloc = false;

extern "C" void heap_itc_alloc(bool itc)
{
    s_heap_itc_alloc = itc;
}

static const char *heap_pool_name(const void *ptr)
{
    uintptr_t p = (uintptr_t)ptr;
    if (p < 0x00010000u)
        return "ITCM";
    if (p >= 0x20000000u && p < 0x20020000u)
        return "DTCM";
    if (p >= 0x24000000u && p < 0x24100000u)
        return "RAM_EMU";
    if (p >= 0x30000000u && p < 0x30020000u)
        return "AHB";
    return "?";
}

extern "C" void *heap_alloc_mem(size_t s)
{
    void *ptr = NULL;
    const char *pool = NULL;

    if (s_heap_itc_alloc) {
        void *p = itc_malloc(s);
        /* ITC RAM starts at 0x00000000, so itc_malloc() can't use NULL as
         * its own "allocation failed" sentinel — see gw_malloc.c. */
        if (p != (void *)0xffffffff) {
            ptr = p;
            pool = "ITCM";
        }
    }
    if (!ptr) {
        ptr = ahb_malloc(s);
        if (ptr)
            pool = "AHB";
    }
    if (!ptr) {
        ptr = ram_malloc(s);
        if (ptr)
            pool = "RAM_EMU";
    }
    if (!ptr) {
        ptr = dtc_malloc(s);
        if (ptr)
            pool = "DTCM";
    }

    if (ptr) {
        memset(ptr, 0, s);
        printf("[heap] %u B -> %s @ %p (tag %s)\n",
               (unsigned)s, pool, ptr, heap_pool_name(ptr));
    } else {
        printf("[heap] %u B -> FAIL\n", (unsigned)s);
    }

    return ptr;
}

extern "C" size_t heap_get_largest_free_size(void)
{
    /* Mirrors heap_alloc_mem()'s pool walk: one allocation lands in one
     * pool, so this is the max, not the sum. ITC only after
     * heap_itc_alloc(true). */
    size_t n = ahb_get_free_size();
    size_t ram = ram_get_free_size();
    size_t dtc = dtc_get_free_size();
    if (ram > n)
        n = ram;
    if (dtc > n)
        n = dtc;
    if (s_heap_itc_alloc) {
        size_t itc = itc_get_free_size();
        if (itc > n)
            n = itc;
    }
    return n;
}

extern "C" size_t heap_free_mem(void)
{
    return heap_get_largest_free_size();
}

extern "C" void cpp_heap_init(size_t bss_end)
{
    /* No private bump heap here (unlike the old firmware-side heap.cpp) —
     * every allocation goes through the shared ram/itc/ahb/dtc pools.
     * Kept only for source compatibility with heap.hpp; bss_end is unused. */
    (void)bss_end;
    s_heap_itc_alloc = false;
}

/* ====================================================================
 * operator new/delete
 * ==================================================================== */
void *operator new(size_t s) { return heap_alloc_mem(s); }
void *operator new[](size_t s) { return heap_alloc_mem(s); }

/* No real free(): none of the bump pools behind heap_alloc_mem() support
 * releasing memory (see gw_malloc.c) — same "delete is a no-op" contract
 * the old heap.cpp had. A core's RAM_EMU/ITC/DTCM budget is reclaimed
 * wholesale the next time any core loads (itc_init()/ram_init()/dtc_init()
 * / ram_start rewind — see emulator_start()). AHB malloc allocations are
 * not pool-reset. Leaking within a single ROM session is the intended
 * tradeoff, not a bug. */
void operator delete(void *p) { (void)p; }
void operator delete[](void *p) { (void)p; }
void operator delete(void *p, size_t s) { (void)p; (void)s; }
void operator delete[](void *p, size_t s) { (void)p; (void)s; }

/* ====================================================================
 * Minimal C++ runtime symbols the compiler/linker require to exist even
 * though nothing here should ever actually reach them at runtime (see
 * file header comment).
 * ==================================================================== */
extern "C" void __cxa_pure_virtual()
{
    while (1) { }
}

extern "C" {
void *__dso_handle = (void *)&__dso_handle;
}

extern "C" int __cxa_atexit(void (*)(void *), void *, void *)
{
    return 0;
}

/* ====================================================================
 * Exception / unwind stubs for cores that link -lstdc++ (Stella).
 *
 * Toolchain libstdc++.a is built WITH exceptions; even with our own
 * -fno-exceptions, string/length_error paths still reference the EH
 * runtime. We never throw (new never fails into bad_alloc here — OOM
 * returns NULL and Stella doesn't check every allocation), so these
 * stubs just abort if somehow reached, and keep --gc-sections from
 * pulling the full personality/demangle objects when possible.
 * ==================================================================== */
extern "C" void abort(void);

extern "C" void *__cxa_allocate_exception(size_t) { abort(); return nullptr; }
extern "C" void  __cxa_free_exception(void *) { abort(); }
extern "C" void  __cxa_throw(void *, void *, void (*)(void *)) { abort(); }
extern "C" void  __cxa_rethrow(void) { abort(); }
extern "C" void *__cxa_begin_catch(void *) { abort(); return nullptr; }
extern "C" void  __cxa_end_catch(void) { abort(); }
extern "C" void *__cxa_get_exception_ptr(void *) { return nullptr; }
extern "C" void  __cxa_call_unexpected(void *) { abort(); }
extern "C" char *__cxa_demangle(const char *, char *, size_t *, int *status)
{
    if (status)
        *status = -2; /* invalid mangled name / unsupported */
    return nullptr;
}

/* libgcc provides the real _Unwind_* / __gnu_unwind_frame — do not stub
 * those here (multiple-definition with -lgcc). Empty __exidx_start/end
 * are PROVIDEd by cores/_template/core_ram_emu.ld (exidx is /DISCARD/). */

extern "C" int __gxx_personality_v0(int, int, unsigned long long, void *, void *)
{
    abort();
    return 0;
}

/* newlib's FILE* table pointer — provided by gw_core_bridge.c (aliases the
 * firmware's reent via impure_ptr_ptr). Do not define a second _impure_ptr
 * here: that collides at link time for every C++ core. */
namespace __gnu_cxx {
void __verbose_terminate_handler() { abort(); }
}
