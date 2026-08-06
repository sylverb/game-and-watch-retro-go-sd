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

int core_snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = gw_firmware_abi()->vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

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
 * FatFs (ff.h)
 * ==================================================================== */
FRESULT core_f_opendir(DIR *dp, const TCHAR *path) { return gw_firmware_abi()->f_opendir(dp, path); }
FRESULT core_f_closedir(DIR *dp) { return gw_firmware_abi()->f_closedir(dp); }
FRESULT core_f_readdir(DIR *dp, FILINFO *fno) { return gw_firmware_abi()->f_readdir(dp, fno); }

/* ====================================================================
 * G&W hardware: LCD
 * ==================================================================== */
void  core_lcd_swap(void) { gw_firmware_abi()->lcd_swap(); }
void *core_lcd_get_active_buffer(void) { return gw_firmware_abi()->lcd_get_active_buffer(); }
void *core_lcd_get_inactive_buffer(void) { return gw_firmware_abi()->lcd_get_inactive_buffer(); }
void *core_lcd_clear_active_buffer(void) { return gw_firmware_abi()->lcd_clear_active_buffer(); }
void *core_lcd_clear_inactive_buffer(void) { return gw_firmware_abi()->lcd_clear_inactive_buffer(); }
void  core_lcd_wait_for_vblank(void) { gw_firmware_abi()->lcd_wait_for_vblank(); }
void  core_lcd_set_refresh_rate(uint32_t frequency) { gw_firmware_abi()->lcd_set_refresh_rate(frequency); }
void  core_lcd_clear_buffers(void) { gw_firmware_abi()->lcd_clear_buffers(); }

/* ====================================================================
 * G&W hardware: audio
 * ==================================================================== */
void     core_audio_start_playing(uint16_t length) { gw_firmware_abi()->audio_start_playing(length); }
int16_t *core_audio_get_active_buffer(void) { return gw_firmware_abi()->audio_get_active_buffer(); }
void     core_audio_clear_active_buffer(void) { gw_firmware_abi()->audio_clear_active_buffer(); }
void     core_audio_clear_inactive_buffer(void) { gw_firmware_abi()->audio_clear_inactive_buffer(); }
uint16_t core_audio_get_buffer_length(void) { return gw_firmware_abi()->audio_get_buffer_length(); }

/* ====================================================================
 * G&W hardware: allocators
 *
 * All of these route through the single ABI entry point mem_alloc() (see
 * gw_firmware_abi.h) — kept as separate trampolines/names here purely so
 * core source code (main_wsv.c, main_gwenesis.c, external submodules)
 * keeps calling the familiar itc_malloc()/ahb_calloc()/etc. names it
 * always has, via the usual objcopy --redefine-syms indirection.
 * ==================================================================== */
void  *core_itc_malloc(size_t size) { return gw_firmware_abi()->mem_alloc(GW_MEM_ITC, 1, size); }
void  *core_itc_calloc(size_t count, size_t size) { return gw_firmware_abi()->mem_alloc(GW_MEM_ITC, count, size); }
void   core_itc_init(void) { gw_firmware_abi()->itc_init(); }
void  *core_ram_malloc(size_t size) { return gw_firmware_abi()->mem_alloc(GW_MEM_RAM, 1, size); }
size_t core_ram_get_free_size(void) { return gw_firmware_abi()->ram_get_free_size(); }
void  *core_dtcm_malloc(size_t size) { return gw_firmware_abi()->mem_alloc(GW_MEM_DTCM, 1, size); }

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
void core_odroid_audio_mute(bool mute) { gw_firmware_abi()->odroid_audio_mute(mute); }

/* ====================================================================
 * retro-go: input / display
 * ==================================================================== */
void core_odroid_input_read_gamepad(odroid_gamepad_state_t *out_state) { gw_firmware_abi()->odroid_input_read_gamepad(out_state); }
odroid_display_scaling_t core_odroid_display_get_scaling_mode(void) { return gw_firmware_abi()->odroid_display_get_scaling_mode(); }
void core_odroid_display_set_scaling_mode(odroid_display_scaling_t mode) { gw_firmware_abi()->odroid_display_set_scaling_mode(mode); }
/* Real return type is odroid_display_filter_t; ABI forwards it as plain int
 * so this header doesn't have to pull odroid_display.h's enum in — the enum
 * values themselves are ABI-stable (see gw_firmware_abi.h). */
int core_odroid_display_get_filter_mode(void) { return gw_firmware_abi()->odroid_display_get_filter_mode(); }

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
    return gw_firmware_abi()->odroid_overlay_cache_file_in_flash(file_path, file_size_p, byte_swap);
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
void    core_common_emu_enable_dwt_cycles(void) { gw_firmware_abi()->common_emu_enable_dwt_cycles(); }
unsigned int core_common_emu_get_dwt_cycles(void) { return gw_firmware_abi()->common_emu_get_dwt_cycles(); }
void    core_common_emu_clear_dwt_cycles(void) { gw_firmware_abi()->common_emu_clear_dwt_cycles(); }

/* ====================================================================
 * v1 append: Mega Drive / gwenesis porting surface
 * ==================================================================== */
void  *core_ahb_malloc(size_t size) { return gw_firmware_abi()->mem_alloc(GW_MEM_AHB, 1, size); }
void  *core_ahb_calloc(size_t count, size_t size) { return gw_firmware_abi()->mem_alloc(GW_MEM_AHB, count, size); }

void core_odroid_audio_init(int sample_rate) { gw_firmware_abi()->odroid_audio_init(sample_rate); }
int  core_odroid_audio_sample_rate_get(void) { return gw_firmware_abi()->odroid_audio_sample_rate_get(); }
void core_audio_start_playing_full_length(uint16_t length) { gw_firmware_abi()->audio_start_playing_full_length(length); }
uint16_t core_audio_get_buffer_full_length(void) { return gw_firmware_abi()->audio_get_buffer_full_length(); }

uint8_t core_odroid_settings_cpu_oc_level_get(void) { return gw_firmware_abi()->odroid_settings_cpu_oc_level_get(); }
void    core_SystemClock_Config(uint8_t new_oc_level) { gw_firmware_abi()->SystemClock_Config(new_oc_level); }

bool core_get_ofw_is_mario(void) { return gw_firmware_abi()->get_ofw_is_mario(); }

char *core_odroid_system_get_path(int type, const char *romPath)
{
    return gw_firmware_abi()->odroid_system_get_path(type, romPath);
}

uint32_t core_lcd_get_pixel_position(void) { return gw_firmware_abi()->lcd_get_pixel_position(); }
bool     core_lcd_sleep_while_swap_pending(void) { return gw_firmware_abi()->lcd_sleep_while_swap_pending(); }

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
void   core_GW_GetUnixTM(struct tm *tm) { gw_firmware_abi()->GW_GetUnixTM(tm); }
time_t core_mktime(struct tm *tm) { return gw_firmware_abi()->mktime(tm); }
void   core_lcd_clone(void) { gw_firmware_abi()->lcd_clone(); }
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
 * through mem_alloc(GW_MEM_DTCM, ...) so the buffers are free()-able on
 * the DTCM newlib heap (same pool dtcm_malloc uses).
 * ==================================================================== */
int core_fputs(const char *s, FILE *stream)
{
    const gw_firmware_abi_t *abi = gw_firmware_abi();
    size_t len = abi->strlen(s);
    return (abi->fwrite(s, 1, len, stream) == len) ? 0 : EOF;
}

void *core_calloc(size_t nmemb, size_t size)
{
    return gw_firmware_abi()->mem_alloc(GW_MEM_DTCM, nmemb, size);
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
 * Un-renamed libc exports for archives that still call malloc/strlen/...
 * by their real names (notably toolchain libstdc++.a when a core sets
 * CORE_LDLIBS=-lstdc++). Core .o files go through --redefine-syms so they
 * call core_*; this bridge object does NOT, so these wrappers stay as
 * malloc/free/... and satisfy libstdc++ without dragging in newlib.
 * ==================================================================== */
void  *malloc(size_t size) { return core_malloc(size); }
void   free(void *ptr) { core_free(ptr); }
void  *realloc(void *ptr, size_t size) { return core_realloc(ptr, size); }
void   abort(void) { core_abort(); }
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
