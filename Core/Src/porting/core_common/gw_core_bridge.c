/*
 * core_common bridge trampolines.
 *
 * One `core_<name>` function per entry of gw_firmware_abi_t that a classic
 * core is expected to call. Each simply forwards to the firmware through
 * gw_firmware_abi() — see gw_core_bridge.h for the overall design and
 * gw_core_bridge_redefine_syms.txt for the objcopy renaming that makes the
 * core's own code (which still calls "fopen", "lcd_swap", ...) resolve to
 * these instead of a real local implementation. The exception is
 * memcpy/memset/memmove/__aeabi_mem* (see their own comment below): real
 * local implementations, not ABI trampolines — too hot a path for the
 * extra indirection.
 *
 * NOT implemented here (add if/when a future core needs them):
 *   - __aeabi_ldivmod / __aeabi_uldivmod: return a {quot,rem} pair in
 *     r0-r3 per AAPCS, which a plain C function pointer can't express.
 *     ldivmod_quot/ldivmod_rem (and the u* variants) ARE in the ABI for
 *     when this is needed — see docs/PICO8_EXTERNAL_MODULE.md.
 * If a core's link fails with "undefined reference to __aeabi_*", that
 * core is the first to need the above.
 *
 * setjmp/longjmp ARE implemented (see core_setjmp/core_longjmp below), but
 * NOT as plain wrappers like everything else in this file: a normal C
 * function calling gw_firmware_abi()->setjmp(env) would have setjmp save
 * *its own* (the trampoline's) stack frame, which is gone by the time the
 * m68k core (the first caller here — Musashi's read/write bus-error path)
 * later calls longjmp, since core_setjmp already returned 0 to ITS caller
 * on the direct-call path. They're naked asm tail calls instead (`bx`, no
 * `bl`, no prologue/epilogue) so the real setjmp/longjmp execute with
 * EXACTLY the original caller's r0-r3/LR/SP — indistinguishable from that
 * caller having called the firmware's real setjmp/longjmp directly.
 */

#include "gw_core_bridge.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>
#include <setjmp.h>
#include <time.h>

/* newlib defines these as function-like macros (isalnum(c) -> ctype-table
 * lookup, feof(f)/ferror(f) -> flag-bit check on the FILE struct); left
 * alone they'd macro-expand `gw_firmware_abi()->isalnum(c)` into nonsense
 * instead of a struct member call. Undef so the plain trampoline names
 * below resolve to newlib's real (non-macro) function symbols instead —
 * which we never call anyway, we only need the identifier to not expand. */
#undef isalnum
#undef isalpha
#undef isspace
#undef isupper
#undef islower
#undef isxdigit
#undef tolower
#undef toupper
#undef feof
#undef ferror

void gw_core_bridge_init(void)
{
    /* Nothing to snapshot yet — see gw_core_bridge.h. */
}

/* Caprice (and other plain-C cores) call fputs(stderr, …) which expands to
 * _impure_ptr->_stderr. Alias the firmware's reent so stderr/stdout work.
 * Runs from .init_array before CORE_ENTRY (see gw_core_entry.S). */
struct _reent;
struct _reent *_impure_ptr;
static void __attribute__((constructor)) gw_core_impure_ptr_init(void)
{
    _impure_ptr = *(struct _reent **)(gw_firmware_abi()->impure_ptr_ptr);
}

/* libm (linked directly via CORE_LDLIBS=-lm, see cores/md/Makefile) expects
 * newlib's non-reentrant `errno` macro, `#define errno (*__errno())`. Its
 * .a member (math_err.o) is prebuilt and never passes through this build's
 * --redefine-syms pass (that only touches OUR object files, see
 * gw_core_bridge_redefine_syms.txt's header comment), so unlike everything
 * else in this file the real `__errno` symbol name must exist as-is — no
 * `core_` trampoline/rename pair for this one. Purely local per-core state
 * (single core running at a time, no threads), no need to round-trip
 * through the firmware ABI either. */
static int core_errno_storage;
int *__errno(void) { return &core_errno_storage; }

/* Baked-in record of the ABI surface this core was actually compiled
 * against — read by tools/pack_core.py (via `nm` + a raw byte read at this
 * symbol's file offset, since the payload isn't executed on the packaging
 * host) to fill gnw_core_meta_t.required_abi_version/required_abi_min_size
 * without duplicating gw_firmware_abi_t's layout logic in Python. */
/* Explicit named section + KEEP() in core_ram_emu.ld: with -ffunction-
 * sections/-fdata-sections + --gc-sections, an otherwise-unreferenced
 * const global (nothing in this core ever reads these, they exist only
 * for the packaging tool to read post-link) gets garbage-collected despite
 * __attribute__((used)) — that attribute only stops the *compiler* from
 * dropping it, --gc-sections is a *linker* decision that needs KEEP(). */
__attribute__((used, section(".gw_core_bridge_probe")))
const uint32_t GW_CORE_BUILT_ABI_VERSION = GW_FIRMWARE_ABI_VERSION;
__attribute__((used, section(".gw_core_bridge_probe")))
const uint32_t GW_CORE_BUILT_ABI_SIZE = sizeof(gw_firmware_abi_t);

/* ====================================================================
 * libc: string.h
 * ==================================================================== */
void  *core_memchr(const void *s, int c, size_t n) { return gw_firmware_abi()->memchr(s, c, n); }
int    core_memcmp(const void *a, const void *b, size_t n) { return gw_firmware_abi()->memcmp(a, b, n); }
char  *core_strchr(const char *s, int c) { return gw_firmware_abi()->strchr(s, c); }
int    core_strcmp(const char *a, const char *b) { return gw_firmware_abi()->strcmp(a, b); }
size_t core_strlen(const char *s) { return gw_firmware_abi()->strlen(s); }
int    core_strncmp(const char *a, const char *b, size_t n) { return gw_firmware_abi()->strncmp(a, b, n); }
char  *core_strncpy(char *d, const char *s, size_t n) { return gw_firmware_abi()->strncpy(d, s, n); }
char  *core_strrchr(const char *s, int c) { return gw_firmware_abi()->strrchr(s, c); }
char  *core_strstr(const char *h, const char *n) { return gw_firmware_abi()->strstr(h, n); }
char  *core_strcpy(char *d, const char *s) { return gw_firmware_abi()->strcpy(d, s); }
/* strcat is not on the ABI; compose from strlen+strcpy (FCEUmm ines.c). */
char  *core_strcat(char *dest, const char *src)
{
    core_strcpy(dest + core_strlen(dest), src);
    return dest;
}
long   core_strtol(const char *nptr, char **endptr, int base) { return gw_firmware_abi()->strtol(nptr, endptr, base); }
double core_strtod(const char *nptr, char **endptr) { return gw_firmware_abi()->strtod(nptr, endptr); }

/* ====================================================================
 * memcpy/memset/memmove + the compiler-generated __aeabi_mem* family:
 * LOCAL implementations, NOT routed through gw_firmware_abi() like
 * everything else in this file.
 *
 * These are by far the hottest calls a classic emulator core makes —
 * every scanline blit, DMA-style buffer fill, CD sector read (2048B),
 * ADPCM/CD-DA sample buffer copy, etc. Going through the ABI indirection
 * (redefine-syms rename -> real function call -> load abi->memcpy from
 * the struct -> indirect branch -> firmware's memcpy) on every single one
 * of those, even 4-byte ones the compiler would normally inline away,
 * was measured to cause visible frameskip/audio glitches on PCE-CD (heavy
 * memcpy use: SCSI sectors, ADPCM, CD-DA mixing) — hence local
 * implementations that the linker resolves directly, no indirection, no
 * ABI round-trip. This file is exempt from gw_core_bridge_redefine_syms.txt
 * (see cores/_template/Makefile's `if "$@" != "$(BRIDGE_OBJECTS)"`), so
 * these real-named definitions are what every other object in the core
 * link's plain "memcpy"/"memset"/... calls resolve to.
 *
 * -mno-unaligned-access (must match the firmware's MCU flags, see
 * cores/_template/Makefile) means Cortex-M7 unaligned word loads/stores
 * are NOT assumed safe here — memcpy/memmove/memset fall back to a byte
 * loop unless dst (and, for memcpy/memmove, dst-vs-src) is/are provably
 * word-aligned. __aeabi_memcpy4/8 and __aeabi_memset4/8/__aeabi_memclr4/8
 * are compiler-guaranteed 4/8-byte aligned by construction (the compiler
 * only emits them when it has proven the alignment itself), so those skip
 * the runtime check and go straight to the word-copy loop. */
void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (n >= 4 && (((uintptr_t)d ^ (uintptr_t)s) & 3u) == 0) {
        while (((uintptr_t)d & 3u) && n) { *d++ = *s++; n--; }
        while (n >= 16) {
            uint32_t *dw = (uint32_t *)d;
            const uint32_t *sw = (const uint32_t *)s;
            dw[0] = sw[0]; dw[1] = sw[1]; dw[2] = sw[2]; dw[3] = sw[3];
            d += 16; s += 16; n -= 16;
        }
        while (n >= 4) {
            *(uint32_t *)d = *(const uint32_t *)s;
            d += 4; s += 4; n -= 4;
        }
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || n == 0)
        return dst;
    if (d < s || d >= s + n)
        return memcpy(dst, src, n); /* non-overlapping (or dst before src): forward copy is safe */

    d += n; s += n;
    while (n--) *--d = *--s;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint8_t b = (uint8_t)c;

    if (n >= 4) {
        while (((uintptr_t)d & 3u) && n) { *d++ = b; n--; }
        uint32_t w = 0x01010101u * (uint32_t)b;
        while (n >= 16) {
            uint32_t *dw = (uint32_t *)d;
            dw[0] = w; dw[1] = w; dw[2] = w; dw[3] = w;
            d += 16; n -= 16;
        }
        while (n >= 4) { *(uint32_t *)d = w; d += 4; n -= 4; }
    }
    while (n--) *d++ = b;
    return dst;
}

/* ARM EABI memory helpers the compiler emits instead of plain memcpy/
 * memset/memmove for struct copies, local-array init, etc. (AAPCS
 * __aeabi_mem* family, gcc/config/arm/aeabi-*). NOT simple aliases:
 * __aeabi_memset/memclr take (dest, n, c) — n and c SWAPPED versus libc's
 * memset(dest, c, n). Getting this wrong silently corrupts memory instead
 * of failing to link, so they're spelled out explicitly below. */
void __aeabi_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memcpy4(void *d, const void *s, size_t n)
{
    uint32_t *dw = (uint32_t *)d;
    const uint32_t *sw = (const uint32_t *)s;
    while (n >= 4) { *dw++ = *sw++; n -= 4; }
    uint8_t *db = (uint8_t *)dw;
    const uint8_t *sb = (const uint8_t *)sw;
    while (n--) *db++ = *sb++;
}
void __aeabi_memcpy8(void *d, const void *s, size_t n) { __aeabi_memcpy4(d, s, n); }
void __aeabi_memmove(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memmove4(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memmove8(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memset(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memset4(void *d, size_t n, int c)
{
    uint32_t *dw = (uint32_t *)d;
    uint32_t w = 0x01010101u * (uint32_t)(uint8_t)c;
    while (n >= 4) { *dw++ = w; n -= 4; }
    uint8_t *db = (uint8_t *)dw;
    while (n--) *db++ = (uint8_t)c;
}
void __aeabi_memset8(void *d, size_t n, int c) { __aeabi_memset4(d, n, c); }
void __aeabi_memclr(void *d, size_t n) { memset(d, 0, n); }
void __aeabi_memclr4(void *d, size_t n) { __aeabi_memset4(d, n, 0); }
void __aeabi_memclr8(void *d, size_t n) { __aeabi_memset4(d, n, 0); }

/* ====================================================================
 * libc: ctype.h
 * ==================================================================== */
int core_isalnum(int c)  { return gw_firmware_abi()->isalnum(c); }
int core_isalpha(int c)  { return gw_firmware_abi()->isalpha(c); }
int core_isspace(int c)  { return gw_firmware_abi()->isspace(c); }
int core_isupper(int c)  { return gw_firmware_abi()->isupper(c); }
int core_islower(int c)  { return gw_firmware_abi()->islower(c); }
int core_isxdigit(int c) { return gw_firmware_abi()->isxdigit(c); }
int core_tolower(int c)  { return gw_firmware_abi()->tolower(c); }
int core_toupper(int c)  { return gw_firmware_abi()->toupper(c); }

/* ====================================================================
 * libc: stdlib.h
 * ==================================================================== */
void  core_abort(void) { gw_firmware_abi()->abort(); while (1) {} /* noreturn */ }
void  core_qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
    gw_firmware_abi()->qsort(base, nmemb, size, compar);
}
double core_pow(double x, double y) { return gw_firmware_abi()->pow(x, y); }
void  *core_malloc(size_t size) { return gw_firmware_abi()->malloc(size); }
void   core_free(void *ptr) { gw_firmware_abi()->free(ptr); }
void  *core_realloc(void *ptr, size_t size) { return gw_firmware_abi()->realloc(ptr, size); }

/* ====================================================================
 * libc: stdio.h
 * ==================================================================== */
FILE  *core_fopen(const char *path, const char *mode) { return gw_firmware_abi()->fopen(path, mode); }
int    core_fclose(FILE *stream) { return gw_firmware_abi()->fclose(stream); }
size_t core_fread(void *ptr, size_t size, size_t nmemb, FILE *stream) { return gw_firmware_abi()->fread(ptr, size, nmemb, stream); }
size_t core_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) { return gw_firmware_abi()->fwrite(ptr, size, nmemb, stream); }
int    core_fseek(FILE *stream, long offset, int whence) { return gw_firmware_abi()->fseek(stream, offset, whence); }
long   core_ftell(FILE *stream) { return gw_firmware_abi()->ftell(stream); }
int    core_feof(FILE *stream) { return gw_firmware_abi()->feof(stream); }
int    core_ferror(FILE *stream) { return gw_firmware_abi()->ferror(stream); }
int    core_fgetc(FILE *stream) { return gw_firmware_abi()->fgetc(stream); }
char  *core_fgets(char *s, int size, FILE *stream) { return gw_firmware_abi()->fgets(s, size, stream); }
int    core_remove(const char *path) { return gw_firmware_abi()->remove(path); }
int    core_puts(const char *s) { return gw_firmware_abi()->puts(s); }

int core_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = gw_firmware_abi()->vprintf(fmt, ap);
    va_end(ap);
    return r;
}

int core_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = gw_firmware_abi()->vfprintf(stream, fmt, ap);
    va_end(ap);
    return r;
}

/* Passthrough (not variadic): a caller building its own va_list (e.g. a
 * printf-style wrapper like PCE's osd_log()) needs the real vprintf, not
 * another variadic layer on top of it. */
int core_vprintf(const char *fmt, va_list ap) { return gw_firmware_abi()->vprintf(fmt, ap); }
int core_vfprintf(FILE *stream, const char *fmt, va_list ap) { return gw_firmware_abi()->vfprintf(stream, fmt, ap); }

int core_sprintf(char *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = gw_firmware_abi()->vsprintf(s, fmt, ap);
    va_end(ap);
    return r;
}

int core_vsprintf(char *s, const char *fmt, va_list ap)
{
    return gw_firmware_abi()->vsprintf(s, fmt, ap);
}

int core_snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = gw_firmware_abi()->vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

/* Minimal LCG — FCEU_MemoryRand / NSF visuals only need non-crypto entropy. */
static unsigned long core_rand_state = 1;
int core_rand(void)
{
    core_rand_state = core_rand_state * 1103515245UL + 12345UL;
    return (int)((core_rand_state >> 16) & 0x7fff);
}

/*
 * newlib ctype.h macros (isdigit, etc.) index this table. Provide a minimal
 * ASCII-oriented table so FCEUmm cheat/GG parsers link without pulling libc.
 * Layout matches newlib: _ctype_[c+1], bit flags.
 */
#define _C_U  0x01
#define _C_L  0x02
#define _C_N  0x04
#define _C_S  0x08
#define _C_P  0x10
#define _C_C  0x20
#define _C_X  0x40
#define _C_B  0x80
const char _ctype_[1 + 256] = {
    0,
    _C_C, _C_C, _C_C, _C_C, _C_C, _C_C, _C_C, _C_C,
    _C_C, _C_C|_C_S, _C_C|_C_S, _C_C|_C_S, _C_C|_C_S, _C_C|_C_S, _C_C, _C_C,
    _C_C, _C_C, _C_C, _C_C, _C_C, _C_C, _C_C, _C_C,
    _C_C, _C_C, _C_C, _C_C, _C_C, _C_C, _C_C, _C_C,
    _C_S|_C_B, _C_P, _C_P, _C_P, _C_P, _C_P, _C_P, _C_P,
    _C_P, _C_P, _C_P, _C_P, _C_P, _C_P, _C_P, _C_P,
    _C_N, _C_N, _C_N, _C_N, _C_N, _C_N, _C_N, _C_N,
    _C_N, _C_N, _C_P, _C_P, _C_P, _C_P, _C_P, _C_P,
    _C_P, _C_U|_C_X, _C_U|_C_X, _C_U|_C_X, _C_U|_C_X, _C_U|_C_X, _C_U|_C_X, _C_U,
    _C_U, _C_U, _C_U, _C_U, _C_U, _C_U, _C_U, _C_U,
    _C_U, _C_U, _C_U, _C_U, _C_U, _C_U, _C_U, _C_U,
    _C_U, _C_U, _C_U, _C_P, _C_P, _C_P, _C_P, _C_P,
    _C_P, _C_L|_C_X, _C_L|_C_X, _C_L|_C_X, _C_L|_C_X, _C_L|_C_X, _C_L|_C_X, _C_L,
    _C_L, _C_L, _C_L, _C_L, _C_L, _C_L, _C_L, _C_L,
    _C_L, _C_L, _C_L, _C_L, _C_L, _C_L, _C_L, _C_L,
    _C_L, _C_L, _C_L, _C_P, _C_P, _C_P, _C_P, _C_C,
};

/* ====================================================================
 * libc: assert.h
 * ==================================================================== */
void core_assert_func(const char *file, int line, const char *func, const char *expr)
{
    gw_firmware_abi()->__assert_func(file, line, func, expr);
    while (1) {} /* noreturn */
}

/* ====================================================================
 * libc: setjmp.h — naked tail-call trampolines, see the file header
 * comment for why these can't be plain wrapper functions.
 *
 * gw_firmware_abi() (gw_firmware_abi.h) is itself just
 * `*(uint32_t *)GW_VTOR_ADDRESS + GW_FIRMWARE_ABI_OFFSET`; movw/movt build
 * that same constant inline instead of calling the helper, since a naked
 * function's body may contain nothing but asm. r0 (env) / r1 (val, for
 * longjmp) are never touched, so they reach the real function exactly as
 * the original caller set them up; r2/r3 are free per AAPCS (caller-saved,
 * not yet used for an argument here).
 * ==================================================================== */
__attribute__((naked))
int core_setjmp(jmp_buf env)
{
    (void)env;
    __asm volatile(
        "movw r2, #%[vtor_lo]\n"
        "movt r2, #%[vtor_hi]\n"
        "ldr  r2, [r2]\n"
        "ldr  r1, [r2, %[off]]\n"
        "bx   r1\n"
        :
        : [vtor_lo] "i" (GW_VTOR_ADDRESS & 0xFFFFu),
          [vtor_hi] "i" (GW_VTOR_ADDRESS >> 16),
          [off] "i" (GW_FIRMWARE_ABI_OFFSET + offsetof(gw_firmware_abi_t, setjmp))
    );
}

__attribute__((naked, noreturn))
void core_longjmp(jmp_buf env, int val)
{
    (void)env; (void)val;
    __asm volatile(
        "movw r2, #%[vtor_lo]\n"
        "movt r2, #%[vtor_hi]\n"
        "ldr  r2, [r2]\n"
        "ldr  r3, [r2, %[off]]\n"
        "bx   r3\n"
        :
        : [vtor_lo] "i" (GW_VTOR_ADDRESS & 0xFFFFu),
          [vtor_hi] "i" (GW_VTOR_ADDRESS >> 16),
          [off] "i" (GW_FIRMWARE_ABI_OFFSET + offsetof(gw_firmware_abi_t, longjmp))
    );
}

/* ====================================================================
 * FatFs (ff.h) — via fatfs_dir_ctl()
 * ==================================================================== */
FRESULT core_f_opendir(DIR *dp, const TCHAR *path)
{
    return gw_firmware_abi()->fatfs_dir_ctl(GW_FATFS_OPENDIR, dp, (void *)path);
}
FRESULT core_f_closedir(DIR *dp)
{
    return gw_firmware_abi()->fatfs_dir_ctl(GW_FATFS_CLOSEDIR, dp, NULL);
}
FRESULT core_f_readdir(DIR *dp, FILINFO *fno)
{
    return gw_firmware_abi()->fatfs_dir_ctl(GW_FATFS_READDIR, dp, fno);
}

/* ====================================================================
 * G&W hardware: LCD — all via lcd_ctl(); historical names kept for
 * --redefine-syms. lcd_sleep_while_swap_pending composed from
 * IS_SWAP_PENDING + WFI.
 * ==================================================================== */
void core_lcd_swap(void)
{
    (void)gw_firmware_abi()->lcd_ctl(GW_LCD_SWAP, 0, 0, 0);
}
void *core_lcd_get_active_buffer(void)
{
    return (void *)gw_firmware_abi()->lcd_ctl(GW_LCD_BUFFER, GW_LCD_BUF_ACTIVE, 0, 0);
}
void *core_lcd_get_inactive_buffer(void)
{
    return (void *)gw_firmware_abi()->lcd_ctl(GW_LCD_BUFFER, GW_LCD_BUF_INACTIVE, 0, 0);
}
void *core_lcd_clear_active_buffer(void)
{
    return (void *)gw_firmware_abi()->lcd_ctl(GW_LCD_BUFFER, GW_LCD_BUF_ACTIVE, GW_LCD_CLEAR, 0);
}
void *core_lcd_clear_inactive_buffer(void)
{
    return (void *)gw_firmware_abi()->lcd_ctl(GW_LCD_BUFFER, GW_LCD_BUF_INACTIVE, GW_LCD_CLEAR, 0);
}
void core_lcd_clear_buffers(void)
{
    (void)gw_firmware_abi()->lcd_ctl(GW_LCD_BUFFER, GW_LCD_BUF_BOTH, GW_LCD_CLEAR, 0);
}
void core_lcd_wait_for_vblank(void)
{
    (void)gw_firmware_abi()->lcd_ctl(GW_LCD_WAIT_VBLANK, 0, 0, 0);
}
void core_lcd_set_refresh_rate(uint32_t frequency)
{
    (void)gw_firmware_abi()->lcd_ctl(GW_LCD_SET_REFRESH, frequency, 0, 0);
}
void core_lcd_sync(void)
{
    (void)gw_firmware_abi()->lcd_ctl(GW_LCD_COPY_FB, GW_LCD_COPY_ACTIVE_TO_INACTIVE, 0, 0);
}
void core_lcd_clone(void)
{
    (void)gw_firmware_abi()->lcd_ctl(GW_LCD_COPY_FB, GW_LCD_COPY_INACTIVE_TO_ACTIVE, 0, 0);
}
bool core_lcd_sleep_while_swap_pending(void)
{
    bool pending = false;
    while (gw_firmware_abi()->lcd_ctl(GW_LCD_IS_SWAP_PENDING, 0, 0, 0)) {
        pending = true;
        __asm volatile ("wfi");
    }
    return pending;
}
uint32_t core_lcd_get_pixel_position(void)
{
    return (uint32_t)gw_firmware_abi()->lcd_ctl(GW_LCD_GET_PIXEL_POS, 0, 0, 0);
}
uint32_t core_lcd_is_swap_pending(void)
{
    return (uint32_t)gw_firmware_abi()->lcd_ctl(GW_LCD_IS_SWAP_PENDING, 0, 0, 0);
}
void core_lcd_backlight_set(uint8_t brightness)
{
    (void)gw_firmware_abi()->lcd_ctl(GW_LCD_BACKLIGHT_SET, brightness, 0, 0);
}
void core_lcd_setup_framebuffers(int lcd_mode)
{
    (void)gw_firmware_abi()->lcd_ctl(GW_LCD_SETUP_FB, (uint32_t)lcd_mode, 0, 0);
}
void core_lcd_get_bonus_pool(uint8_t **out_ptr, size_t *out_size)
{
    (void)gw_firmware_abi()->lcd_ctl(GW_LCD_GET_BONUS_POOL,
                                     (uint32_t)(uintptr_t)out_ptr,
                                     (uint32_t)(uintptr_t)out_size, 0);
}
void core_lcd_set_clut(const uint32_t *clut, uint16_t count)
{
    (void)gw_firmware_abi()->lcd_ctl(GW_LCD_SET_CLUT,
                                     (uint32_t)(uintptr_t)clut, count, 0);
}

/* ====================================================================
 * G&W hardware: audio
 * ==================================================================== */
/* ====================================================================
 * G&W hardware: audio — all via audio_ctl(); historical names kept for
 * --redefine-syms. audio_get_buffer_size composed as length * sizeof(int16_t);
 * audio_clear_buffers uses CLEAR_BOTH (full DMA memset).
 * ==================================================================== */
void core_audio_start_playing(uint16_t length)
{
    (void)gw_firmware_abi()->audio_ctl(GW_AUDIO_START, length);
}
int16_t *core_audio_get_active_buffer(void)
{
    return (int16_t *)gw_firmware_abi()->audio_ctl(GW_AUDIO_GET_ACTIVE, 0);
}
void core_audio_clear_active_buffer(void)
{
    (void)gw_firmware_abi()->audio_ctl(GW_AUDIO_CLEAR_ACTIVE, 0);
}
void core_audio_clear_inactive_buffer(void)
{
    (void)gw_firmware_abi()->audio_ctl(GW_AUDIO_CLEAR_INACTIVE, 0);
}
uint16_t core_audio_get_buffer_length(void)
{
    return (uint16_t)gw_firmware_abi()->audio_ctl(GW_AUDIO_GET_LENGTH, 0);
}
void core_audio_clear_buffers(void)
{
    (void)gw_firmware_abi()->audio_ctl(GW_AUDIO_CLEAR_BOTH, 0);
}
uint16_t core_audio_get_buffer_size(void)
{
    return (uint16_t)(gw_firmware_abi()->audio_ctl(GW_AUDIO_GET_LENGTH, 0) * sizeof(int16_t));
}
void core_audio_start_playing_full_length(uint16_t length)
{
    (void)gw_firmware_abi()->audio_ctl(GW_AUDIO_START_FULL, length);
}
uint16_t core_audio_get_buffer_full_length(void)
{
    return (uint16_t)gw_firmware_abi()->audio_ctl(GW_AUDIO_GET_FULL_LENGTH, 0);
}
void core_audio_stop_playing(void)
{
    (void)gw_firmware_abi()->audio_ctl(GW_AUDIO_STOP, 0);
}
void core_odroid_audio_mute(bool mute)
{
    (void)gw_firmware_abi()->audio_ctl(GW_AUDIO_MUTE, mute ? 1u : 0u);
}
void core_odroid_audio_init(int sample_rate)
{
    (void)gw_firmware_abi()->audio_ctl(GW_AUDIO_INIT, (uint32_t)sample_rate);
}
int core_odroid_audio_sample_rate_get(void)
{
    return (int)gw_firmware_abi()->audio_ctl(GW_AUDIO_SAMPLE_RATE_GET, 0);
}
int core_odroid_audio_volume_get(void)
{
    return (int)gw_firmware_abi()->audio_ctl(GW_AUDIO_VOLUME_GET, 0);
}

/* ====================================================================
 * G&W hardware: allocators
 *
 * All of these route through mem_ctl() (see gw_firmware_abi.h) — kept as
 * separate trampolines/names so core source keeps calling the familiar
 * itc_malloc()/ahb_calloc()/etc. via objcopy --redefine-syms.
 * ==================================================================== */
void *core_itc_malloc(size_t size)
{
    return (void *)gw_firmware_abi()->mem_ctl(GW_MEM_OP_ALLOC, GW_MEM_ITC, 1, size);
}
void *core_itc_calloc(size_t count, size_t size)
{
    return (void *)gw_firmware_abi()->mem_ctl(GW_MEM_OP_ALLOC, GW_MEM_ITC, count, size);
}
void core_itc_init(void)
{
    (void)gw_firmware_abi()->mem_ctl(GW_MEM_OP_INIT, GW_MEM_ITC, 0, 0);
}
void *core_ram_malloc(size_t size)
{
    return (void *)gw_firmware_abi()->mem_ctl(GW_MEM_OP_ALLOC, GW_MEM_RAM, 1, size);
}
void *core_ram_calloc(size_t count, size_t size)
{
    return (void *)gw_firmware_abi()->mem_ctl(GW_MEM_OP_ALLOC, GW_MEM_RAM, count, size);
}
size_t core_ram_get_free_size(void)
{
    return (size_t)gw_firmware_abi()->mem_ctl(GW_MEM_OP_FREE_SIZE, GW_MEM_RAM, 0, 0);
}
void *core_dtc_malloc(size_t size)
{
    return (void *)gw_firmware_abi()->mem_ctl(GW_MEM_OP_ALLOC, GW_MEM_DTC, 1, size);
}
void *core_dtc_calloc(size_t count, size_t size)
{
    return (void *)gw_firmware_abi()->mem_ctl(GW_MEM_OP_ALLOC, GW_MEM_DTC, count, size);
}

/* ====================================================================
 * G&W hardware: RTC. Per-field getters and GW_GetUnixTM/mktime were
 * dropped from the firmware table during external-core development
 * (still ABI v2). Every core reads "now" through core_time() +
 * core_localtime() instead. GW_SetUnixTM (below) stays — writing the
 * RTC has no portable libc equivalent wired into the ABI.
 * Millis/SubSeconds composed from gettimeofday — firmware _gettimeofday
 * is itself backed by GW_GetCurrentMillis(); kept as their own
 * trampolines since struct tm/time_t have no sub-second field.
 * ==================================================================== */
time_t core_time(time_t *t) { return gw_firmware_abi()->time(t); }
uint64_t core_GW_GetCurrentMillis(void)
{
    struct timeval tv;
    if (gw_firmware_abi()->gettimeofday(&tv, NULL) != 0)
        return 0;
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}
uint8_t core_GW_GetCurrentSubSeconds(void)
{
    struct timeval tv;
    if (gw_firmware_abi()->gettimeofday(&tv, NULL) != 0)
        return 0;
    return (uint8_t)((tv.tv_usec * 256L) / 1000000L);
}

/* ====================================================================
 * G&W hardware: watchdog + HAL
 * ==================================================================== */
void     core_wdog_refresh(void) { gw_firmware_abi()->wdog_refresh(); }
void     core_HAL_Delay(uint32_t ms) { gw_firmware_abi()->HAL_Delay(ms); }
uint32_t core_HAL_GetTick(void) { return gw_firmware_abi()->HAL_GetTick(); }

/* ====================================================================
 * retro-go: system
 * ==================================================================== */
void core_odroid_system_init(int app_id, int sample_rate) { gw_firmware_abi()->odroid_system_init(app_id, sample_rate); }

void core_odroid_system_emu_init(state_handler_t load_cb,
                                 state_handler_t save_cb,
                                 screenshot_handler_t screenshot_cb,
                                 shutdown_handler_t shutdown_cb,
                                 sleep_post_wakeup_handler_t sleep_post_wakeup_cb,
                                 sram_save_handler_t sram_save_cb,
                                 cheat_update_handler_t cheat_update_cb)
{
    gw_firmware_abi()->odroid_system_emu_init(load_cb, save_cb, screenshot_cb,
                                              shutdown_cb, sleep_post_wakeup_cb,
                                              sram_save_cb, cheat_update_cb);
}

bool core_odroid_system_emu_load_state(int slot) { return gw_firmware_abi()->odroid_system_emu_load_state(slot); }

rg_app_desc_t *core_odroid_system_get_app(void)
{
    return gw_firmware_abi()->odroid_system_get_app();
}

uint32_t core_dma2d_ctl(gw_dma2d_op_t op, uint32_t a, uint32_t b, uint32_t c)
{
    return gw_firmware_abi()->dma2d_ctl(op, a, b, c);
}

/* ====================================================================
 * retro-go: input / display
 * ==================================================================== */
void core_odroid_input_read_gamepad(odroid_gamepad_state_t *out_state)
{
    (void)gw_firmware_abi()->input_ctl(GW_INPUT_READ_GAMEPAD, out_state);
}
odroid_battery_state_t core_odroid_input_read_battery(void)
{
    odroid_battery_state_t bat;
    (void)gw_firmware_abi()->input_ctl(GW_INPUT_READ_BATTERY, &bat);
    return bat;
}
odroid_display_scaling_t core_odroid_display_get_scaling_mode(void)
{
    return (odroid_display_scaling_t)gw_firmware_abi()->display_ctl(GW_DISP_GET_SCALING, 0);
}
void core_odroid_display_set_scaling_mode(odroid_display_scaling_t mode)
{
    (void)gw_firmware_abi()->display_ctl(GW_DISP_SET_SCALING, (uint32_t)mode);
}
/* Real return type is odroid_display_filter_t; ABI forwards it as plain int
 * so this header doesn't have to pull odroid_display.h's enum in — the enum
 * values themselves are ABI-stable (see gw_firmware_abi.h). */
int core_odroid_display_get_filter_mode(void)
{
    return (int)gw_firmware_abi()->display_ctl(GW_DISP_GET_FILTER, 0);
}

/* ====================================================================
 * retro-go: overlay / SD / settings
 * ==================================================================== */
int core_odroid_overlay_draw_text(uint16_t x, uint16_t y, uint16_t width,
                                  const char *text, uint16_t color, uint16_t color_bg)
{
    return gw_firmware_abi()->odroid_overlay_draw_text(x, y, width, text, color, color_bg);
}
uint8_t *core_odroid_overlay_cache_file_in_flash(const char *file_path, uint32_t *file_size_p, bool byte_swap)
{
    return gw_firmware_abi()->odroid_overlay_cache_file_in_flash_relocate(
        file_path, file_size_p, byte_swap, NULL);
}
size_t core_odroid_overlay_cache_file_in_ram(const char *file_path, uint8_t *dest_address)
{
    return gw_firmware_abi()->odroid_overlay_cache_file_in_ram(file_path, dest_address);
}
int core_odroid_sdcard_mkdir(const char *path) { return gw_firmware_abi()->odroid_sdcard_mkdir(path); }
int32_t core_odroid_settings_app_int32_get(const char *key, int32_t default_value)
{
    return gw_firmware_abi()->odroid_settings_app_int32_get(key, default_value);
}
void core_odroid_settings_app_int32_set(const char *key, int32_t value)
{
    gw_firmware_abi()->odroid_settings_app_int32_set(key, value);
}

/* ====================================================================
 * retro-go: common emulator loop
 * ==================================================================== */
bool core_common_emu_frame_loop(void) { return gw_firmware_abi()->common_emu_frame_loop(); }
void core_common_emu_input_loop(odroid_gamepad_state_t *joystick, odroid_dialog_choice_t *game_options, void_callback_t repaint)
{
    gw_firmware_abi()->common_emu_input_loop(joystick, game_options, repaint);
}
void core_common_emu_input_loop_handle_turbo(odroid_gamepad_state_t *joystick)
{
    gw_firmware_abi()->common_emu_input_loop_handle_turbo(joystick);
}
uint8_t core_common_emu_sound_get_volume(void) { return gw_firmware_abi()->common_emu_sound_get_volume(); }
bool    core_common_emu_sound_loop_is_muted(void) { return gw_firmware_abi()->common_emu_sound_loop_is_muted(); }
void    core_common_emu_sound_sync(bool use_nops) { gw_firmware_abi()->common_emu_sound_sync(use_nops); }
void    core_common_ingame_overlay(void) { gw_firmware_abi()->common_ingame_overlay(); }
/* DWT cycle counter — fixed CMSIS MMIO, no ABI slot (hot path; same
 * addresses as firmware common.c). */
void core_common_emu_enable_dwt_cycles(void)
{
    volatile unsigned int *DEMCR = (volatile unsigned int *)0xE000EDFCu;
    volatile unsigned int *LAR = (volatile unsigned int *)0xE0001FB0u;
    volatile unsigned int *DWT_CYCCNT = (volatile unsigned int *)0xE0001004u;
    volatile unsigned int *DWT_CONTROL = (volatile unsigned int *)0xE0001000u;

    *DEMCR = *DEMCR | 0x01000000u;
    *LAR = 0xC5ACCE55u;
    *DWT_CYCCNT = 0;
    *DWT_CONTROL = *DWT_CONTROL | 1u;
}
unsigned int core_common_emu_get_dwt_cycles(void)
{
    return *(volatile unsigned int *)0xE0001004u;
}
void core_common_emu_clear_dwt_cycles(void)
{
    *(volatile unsigned int *)0xE0001004u = 0;
}

/* ====================================================================
 * v1 append: Mega Drive / gwenesis porting surface
 * ==================================================================== */
void *core_ahb_malloc(size_t size)
{
    return (void *)gw_firmware_abi()->mem_ctl(GW_MEM_OP_ALLOC, GW_MEM_AHB, 1, size);
}
void *core_ahb_calloc(size_t count, size_t size)
{
    return (void *)gw_firmware_abi()->mem_ctl(GW_MEM_OP_ALLOC, GW_MEM_AHB, count, size);
}

uint8_t core_odroid_settings_cpu_oc_level_get(void) { return gw_firmware_abi()->odroid_settings_cpu_oc_level_get(); }
void    core_SystemClock_Config(uint8_t new_oc_level) { gw_firmware_abi()->SystemClock_Config(new_oc_level); }

bool core_get_ofw_is_mario(void) { return gw_firmware_abi()->get_ofw_is_mario(); }

char *core_odroid_system_get_path(int type, const char *romPath)
{
    return gw_firmware_abi()->odroid_system_get_path(type, romPath);
}

/* ====================================================================
 * v2 append: PC Engine / PC Engine CD porting surface
 * ==================================================================== */
unsigned int core_crc32_le(unsigned int crc, const unsigned char *buf, unsigned int len) { return gw_firmware_abi()->crc32_le(crc, buf, len); }
void     core_cpumon_sleep(void) { gw_firmware_abi()->cpumon_sleep(); }
char    *core_strncat(char *dest, const char *src, size_t n) { return gw_firmware_abi()->strncat(dest, src, n); }
bool     core_odroid_settings_ActiveGameGenieCodes_is_enabled(char *game_path, int code_index)
{
    return gw_firmware_abi()->odroid_settings_ActiveGameGenieCodes_is_enabled(game_path, code_index);
}

int core_sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = gw_firmware_abi()->vsscanf(str, fmt, ap);
    va_end(ap);
    return r;
}

/* ====================================================================
 * v2 append: TGB Dual (Game Boy / Game Boy Color, C++) porting surface
 * ==================================================================== */
int32_t core_odroid_settings_Palette_get(void) { return gw_firmware_abi()->odroid_settings_Palette_get(); }
void    core_odroid_settings_Palette_set(int32_t value) { gw_firmware_abi()->odroid_settings_Palette_set(value); }

/* strtok keeps a static "where was I" pointer between calls — like memcpy/
 * memset above, this is a real LOCAL implementation, not an ABI trampoline:
 * a single core runs at a time (no threads), so per-core static state is
 * safe, and routing a stateful libc function through the ABI would mean
 * the *firmware's* static buffer gets used, which is shared with whatever
 * the firmware itself last tokenized — silently wrong the moment both
 * sides call strtok in the same frame. Not in
 * gw_core_bridge_redefine_syms.txt for the same reason memcpy isn't. */
static char *saved_strtok;
char *strtok(char *str, const char *delim)
{
    /* This bridge object is exempt from gw_core_bridge_redefine_syms.txt
     * (see cores/_template/Makefile), so a plain strchr(...) call here
     * would emit a real "strchr" symbol reference that nothing in a
     * -nostdlib link resolves — go through the ABI struct field
     * directly instead, exactly like every other trampoline in this
     * file does. */
    const gw_firmware_abi_t *abi = gw_firmware_abi();
    char *s = str ? str : saved_strtok;
    if (!s)
        return NULL;

    while (*s && abi->strchr(delim, *s))
        s++;
    if (!*s) {
        saved_strtok = NULL;
        return NULL;
    }

    char *tok = s;
    while (*s && !abi->strchr(delim, *s))
        s++;
    if (*s) {
        *s = '\0';
        saved_strtok = s + 1;
    } else {
        saved_strtok = NULL;
    }
    return tok;
}

/* ====================================================================
 * Lynx (handy-go) helpers composed from existing ABI entries — no ABI
 * append needed. handy-go's LSS savestate path uses
 * `#define lss_printf(fp, str) (fputs(str, fp) >= 0)` (system.h), and
 * lynxdec.cpp's public-key decrypt temps use calloc()/free(). free() is
 * already trampolined; these two fill the remaining holes. calloc routes
 * through mem_ctl(GW_MEM_OP_ALLOC, GW_MEM_DTC, ...) (DTCM bump — same
 * pool as dtc_malloc; no per-block free).
 * ==================================================================== */
int core_fputs(const char *s, FILE *stream)
{
    const gw_firmware_abi_t *abi = gw_firmware_abi();
    size_t len = abi->strlen(s);
    return (abi->fwrite(s, 1, len, stream) == len) ? 0 : EOF;
}

void *core_calloc(size_t nmemb, size_t size)
{
    return (void *)gw_firmware_abi()->mem_ctl(GW_MEM_OP_ALLOC, GW_MEM_DTC, nmemb, size);
}

/* ====================================================================
 * Atari 2600 (Stella) helpers composed from existing ABI entries —
 * atoi via strtol; strcasecmp/strncasecmp via tolower. No ABI append.
 * ==================================================================== */
int core_atoi(const char *nptr)
{
    return (int)gw_firmware_abi()->strtol(nptr, NULL, 10);
}

int core_strcasecmp(const char *s1, const char *s2)
{
    const gw_firmware_abi_t *abi = gw_firmware_abi();
    while (*s1 && *s2) {
        int c1 = abi->tolower((unsigned char)*s1++);
        int c2 = abi->tolower((unsigned char)*s2++);
        if (c1 != c2)
            return c1 - c2;
    }
    return abi->tolower((unsigned char)*s1) - abi->tolower((unsigned char)*s2);
}

int core_strncasecmp(const char *s1, const char *s2, size_t n)
{
    const gw_firmware_abi_t *abi = gw_firmware_abi();
    while (n-- > 0) {
        int c1 = abi->tolower((unsigned char)*s1++);
        int c2 = abi->tolower((unsigned char)*s2++);
        if (c1 != c2)
            return c1 - c2;
        if (c1 == 0)
            return 0;
    }
    return 0;
}

int core_fputc(int c, FILE *stream)
{
    unsigned char ch = (unsigned char)c;
    return (gw_firmware_abi()->fwrite(&ch, 1, 1, stream) == 1) ? (int)ch : EOF;
}

void core_rewind(FILE *stream)
{
    (void)gw_firmware_abi()->fseek(stream, 0, SEEK_SET);
}

char *core_getenv(const char *name)
{
    (void)name;
    return NULL;
}

unsigned long core_strtoul(const char *nptr, char **endptr, int base)
{
    return (unsigned long)gw_firmware_abi()->strtol(nptr, endptr, base);
}

/* ====================================================================
 * FCEUmm (NES): ranged SD→RAM copy for /cores/nes_fceumm_mappers/mappers.pak blobs.
 * ==================================================================== */
size_t core_rg_storage_copy_file_range_to_ram(char *file_path, uint8_t *ram_dest,
                                              uint32_t offset, uint32_t length,
                                              gw_file_progress_cb_t file_progress_cb)
{
    return gw_firmware_abi()->rg_storage_copy_file_range_to_ram(
        file_path, ram_dest, offset, length, file_progress_cb);
}

/* ====================================================================
 * blueMSX (MSX): SHA1 + RAM_EMU bump reset.
 * ==================================================================== */
void core_ram_init(void)
{
    (void)gw_firmware_abi()->mem_ctl(GW_MEM_OP_INIT, GW_MEM_RAM, 0, 0);
}
int8_t core_calculate_sha1_file(const char *file_path, uint8_t *output)
{
    return gw_firmware_abi()->sha1_ctl(GW_SHA1_FILE_LIMIT,
                                       (uintptr_t)file_path,
                                       (uintptr_t)(ssize_t)-1,
                                       (uintptr_t)output);
}
int8_t core_calculate_sha1_file_limit(const char *file_path, ssize_t max_bytes, uint8_t *output)
{
    return gw_firmware_abi()->sha1_ctl(GW_SHA1_FILE_LIMIT,
                                       (uintptr_t)file_path,
                                       (uintptr_t)max_bytes,
                                       (uintptr_t)output);
}
int8_t core_calculate_sha1_hw(const uint8_t *data, size_t len, uint8_t *output)
{
    return gw_firmware_abi()->sha1_ctl(GW_SHA1_HW,
                                       (uintptr_t)data,
                                       (uintptr_t)len,
                                       (uintptr_t)output);
}

/* libc localtime/gettimeofday — core_time (above) pairs with this one for
 * every "get now as calendar fields" need (time()+localtime(), see the RTC
 * block above). gettimeofday is real RTC access, kept for
 * archGetSystemUpTime (external/blueMSX-go/Src/Libretro/Timer.c) and the
 * Millis/SubSeconds composition above. mktime is not exported: convert
 * "now" with time(); convert an arbitrary time_t with localtime only. */
struct tm *core_localtime(const time_t *timer) { return gw_firmware_abi()->localtime(timer); }
int core_gettimeofday(struct timeval *tv, void *tz)
{
    return gw_firmware_abi()->gettimeofday(tv, tz);
}

rg_stat_t core_rg_storage_stat(const char *path)
{
    return gw_firmware_abi()->rg_storage_stat(path);
}
/* PokeMini (TARGET_GNW) calls rg_storage_exists for optional BIOS load.
 * Compose from rg_storage_stat — no ABI append. */
bool core_rg_storage_exists(const char *path)
{
    return gw_firmware_abi()->rg_storage_stat(path).exists;
}
bool core_rg_storage_get_adjacent_files(const char *path, char *prev_path, char *next_path)
{
    return gw_firmware_abi()->rg_storage_get_adjacent_files(path, prev_path, next_path);
}
const char *core_rg_basename(const char *path)
{
    return gw_firmware_abi()->rg_basename(path);
}

/* ====================================================================
 * LCD-Game-Emulator (Game & Watch handhelds): RTC write-back, LCD swap
 * poll, hardware JPEG (background images), LZ4/LZMA ROM unpack.
 * odroid_system_switch_app was already on the ABI but missing a
 * trampoline — first consumer is main_gw.c on ROM-load failure.
 *
 * JPEG: ABI exposes a single jpeg_ctl(op,…); the historical
 * JPEG_DecodeToFrameInit/ToFrame/GetSize/DeInit names stay as thin
 * wrappers so external/LCD-Game-Emulator/src/gw_sys/gw_romloader.c is
 * unchanged (redefine-syms still maps those names → core_*).
 * ==================================================================== */
void core_GW_SetUnixTM(struct tm *tm) { gw_firmware_abi()->GW_SetUnixTM(tm); }
uint32_t core_JPEG_DecodeToFrameInit(uint32_t JPEG_Buffer, uint32_t JPEG_Buffer_Size)
{
    return gw_firmware_abi()->jpeg_ctl(GW_JPEG_INIT, JPEG_Buffer, JPEG_Buffer_Size, 0, 0);
}
uint32_t core_JPEG_DecodeToFrame(uint32_t SrcAddress, uint32_t DestAddress,
                                 uint16_t x, uint16_t y, uint8_t luma_alpha)
{
    return gw_firmware_abi()->jpeg_ctl(GW_JPEG_DECODE, SrcAddress, DestAddress,
                                       ((uint32_t)x << 16) | (uint32_t)y, luma_alpha);
}
uint32_t core_JPEG_DecodeGetSize(uint32_t SrcAddress, uint32_t *width, uint32_t *height)
{
    return gw_firmware_abi()->jpeg_ctl(GW_JPEG_GET_SIZE, SrcAddress,
                                       (uint32_t)(uintptr_t)width,
                                       (uint32_t)(uintptr_t)height, 0);
}
uint32_t core_JPEG_DecodeDeInit(void)
{
    return gw_firmware_abi()->jpeg_ctl(GW_JPEG_DEINIT, 0, 0, 0, 0);
}
size_t core_lzma_inflate(uint8_t *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
    return gw_firmware_abi()->lzma_inflate(dst, dst_size, src, src_size);
}
unsigned int core_lz4_uncompress(const void *src, void *dst)
{
    return gw_firmware_abi()->lz4_ctl(GW_LZ4_UNCOMPRESS, src, dst);
}
unsigned int core_lz4_get_file_size(const void *src)
{
    return gw_firmware_abi()->lz4_ctl(GW_LZ4_GET_SIZE, src, NULL);
}
void core_odroid_system_switch_app(int app)
{
    gw_firmware_abi()->odroid_system_switch_app(app);
    while (1) {} /* noreturn */
}

void core_exit(int status)
{
    (void)status;
    gw_firmware_abi()->odroid_system_switch_app(0);
    while (1) {} /* noreturn */
}

void core_common_emu_frame_loop_reset(void)
{
    gw_firmware_abi()->common_emu_frame_loop_reset();
}

uint32_t core_get_SystemCoreClock(void)
{
    return gw_firmware_abi()->get_SystemCoreClock();
}

uint8_t *core_odroid_overlay_cache_file_in_flash_relocate(
    const char *file_path, uint32_t *file_size_p, bool byte_swap,
    gw_flash_relocate_cb_t relocate_cb)
{
    return gw_firmware_abi()->odroid_overlay_cache_file_in_flash_relocate(
        file_path, file_size_p, byte_swap, relocate_cb);
}

void core_draw_error_screen(const char *main_line, const char *line_1, const char *line_2)
{
    gw_firmware_abi()->draw_error_screen(main_line, line_1, line_2);
}

/* ====================================================================
 * Un-renamed libc exports for archives that still call malloc/strlen/...
 * by their real names (notably toolchain libstdc++.a when a core sets
 * CORE_LDLIBS=-lstdc++). Core .o files go through --redefine-syms so they
 * call core_*; this bridge object does NOT, so these wrappers stay as
 * malloc/free/... and satisfy libstdc++ without dragging in newlib.
 * ==================================================================== */
void  *malloc(size_t size) { return core_malloc(size); }
void   free(void *ptr) { core_free(ptr); }
void  *realloc(void *ptr, size_t size) { return core_realloc(ptr, size); }
void   abort(void) { core_abort(); while (1) {} }
void   exit(int status) { core_exit(status); while (1) {} }
int    memcmp(const void *a, const void *b, size_t n) { return core_memcmp(a, b, n); }
char  *strchr(const char *s, int c) { return core_strchr(s, c); }
int    strcmp(const char *a, const char *b) { return core_strcmp(a, b); }
size_t strlen(const char *s) { return core_strlen(s); }
int    strncmp(const char *a, const char *b, size_t n) { return core_strncmp(a, b, n); }
int    sprintf(char *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = gw_firmware_abi()->vsprintf(s, fmt, ap);
    va_end(ap);
    return r;
}
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    return core_fwrite(ptr, size, nmemb, stream);
}
int    fputs(const char *s, FILE *stream) { return core_fputs(s, stream); }
int    fputc(int c, FILE *stream) { return core_fputc(c, stream); }
void   rewind(FILE *stream) { core_rewind(stream); }
char  *getenv(const char *name) { return core_getenv(name); }
unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    return core_strtoul(nptr, endptr, base);
}
