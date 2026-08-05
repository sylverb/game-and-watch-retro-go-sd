/*
 * Game & Watch Retro-Go firmware ABI
 *
 * Stable, versioned contract between this firmware and runtime-loaded
 * plugin overlays (e.g. the PICO-8 engine binary distributed separately
 * from the GPL firmware). The firmware publishes g_firmware_abi at a
 * fixed intflash address; plugins read the struct and call through it
 * instead of linking against firmware symbols directly.
 *
 * This decouples plugin binaries from firmware code layout: the firmware
 * can be recompiled, refactored, or have unrelated emulators updated,
 * without breaking previously distributed plugin binaries, as long as
 * GW_FIRMWARE_ABI_VERSION is unchanged.
 *
 * Backwards-compat rules:
 *   - NEVER reorder or remove struct fields (that's a breaking change).
 *   - Only ADD new fields at the end, bumping GW_FIRMWARE_ABI_VERSION.
 *   - Plugins check version+size at init; they may ignore newer fields.
 *   - The struct is placed at GW_FIRMWARE_ABI_ADDRESS via the linker.
 */

#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <setjmp.h>
#include <locale.h>
#include <time.h>

#include "odroid_system.h"
#include "odroid_input.h"
#include "odroid_display.h"
#include "common.h"
#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bump on any removal, reorder, or signature change. Append-only is safe. */
#define GW_FIRMWARE_ABI_VERSION  1u

/* Offset within intflash where the .firmware_abi section is pinned by
 * the linker. Chosen to sit after the ISR vector table (684 bytes at
 * offset 0..0x2AC) with headroom for vector-table growth before the
 * ABI slot. Engine code resolves the absolute address at runtime by
 * reading the VTOR register — which matches whichever flash bank the
 * firmware is actually executing from (bank 1 = 0x08000000 or bank 2 =
 * 0x08100000 on STM32H7B0). */
#define GW_FIRMWARE_ABI_OFFSET    0x400u

/* ARMv7-M Vector Table Offset Register. VTOR holds the base address of
 * the currently-active vector table; for this firmware that's always
 * __flash_start__ (first byte of intflash). */
#define GW_VTOR_ADDRESS           0xE000ED08u

typedef struct {
    /* Header — every plugin checks these before using the rest. */
    uint32_t version;        /* == GW_FIRMWARE_ABI_VERSION for this build */
    uint32_t size;           /* == sizeof(gw_firmware_abi_t) for this build */

    /* ================================================================
     * libc: string.h
     * ================================================================ */
    void  *(*memchr)(const void *, int, size_t);
    int    (*memcmp)(const void *, const void *, size_t);
    void  *(*memcpy)(void *, const void *, size_t);
    void  *(*memmem)(const void *, size_t, const void *, size_t);
    void  *(*memmove)(void *, const void *, size_t);
    void  *(*memset)(void *, int, size_t);
    char  *(*strchr)(const char *, int);
    int    (*strcmp)(const char *, const char *);
    int    (*strcoll)(const char *, const char *);
    size_t (*strlen)(const char *);
    int    (*strncmp)(const char *, const char *, size_t);
    char  *(*strncpy)(char *, const char *, size_t);
    char  *(*strpbrk)(const char *, const char *);
    char  *(*strrchr)(const char *, int);
    size_t (*strspn)(const char *, const char *);
    char  *(*strstr)(const char *, const char *);
    char  *(*strerror)(int);

    /* ================================================================
     * libc: ctype.h
     * ================================================================ */
    int (*isalnum)(int);
    int (*isalpha)(int);
    int (*iscntrl)(int);
    int (*isgraph)(int);
    int (*islower)(int);
    int (*ispunct)(int);
    int (*isspace)(int);
    int (*isupper)(int);
    int (*isxdigit)(int);
    int (*tolower)(int);
    int (*toupper)(int);

    /* ================================================================
     * libc: stdlib.h
     * ================================================================ */
    void   (*abort)(void);    /* noreturn; attribute dropped on fn ptr */
    void   (*qsort)(void *base, size_t nmemb, size_t size,
                    int (*compar)(const void *, const void *));
    double (*strtod)(const char *nptr, char **endptr);
    long   (*strtol)(const char *nptr, char **endptr, int base);

    /* ================================================================
     * libc: stdio.h
     *
     * Varargs functions (printf / fprintf / fscanf / snprintf / sprintf)
     * are exposed as their v*-form; the engine wraps them back into
     * variadic trampolines.
     * ================================================================ */
    FILE  *(*fopen)(const char *path, const char *mode);
    int    (*fclose)(FILE *stream);
    size_t (*fread)(void *ptr, size_t size, size_t nmemb, FILE *stream);
    size_t (*fwrite)(const void *ptr, size_t size, size_t nmemb, FILE *stream);
    int    (*fseek)(FILE *stream, long offset, int whence);
    long   (*ftell)(FILE *stream);
    int    (*feof)(FILE *stream);
    int    (*ferror)(FILE *stream);
    int    (*fgetc)(FILE *stream);   /* engine's getc() trampolines here */
    int    (*fputc)(int c, FILE *stream);
    FILE  *(*freopen)(const char *path, const char *mode, FILE *stream);
    int    (*remove)(const char *path);
    int    (*putchar)(int c);
    int    (*puts)(const char *s);
    int    (*fflush)(FILE *stream);  /* firmware may wrap this */
    int   *(*__errno)(void);         /* returns &errno for current thread */
    int    (*vfprintf)(FILE *, const char *, va_list);
    int    (*vprintf)(const char *, va_list);
    int    (*vsnprintf)(char *, size_t, const char *, va_list);
    int    (*vsprintf)(char *, const char *, va_list);
    int    (*vfscanf)(FILE *, const char *, va_list);

    /* ================================================================
     * libc: time.h / setjmp.h / locale.h / libm
     * ================================================================ */
    time_t         (*time)(time_t *);
    int            (*setjmp)(jmp_buf env);
    void           (*longjmp)(jmp_buf env, int val);  /* noreturn */
    struct lconv  *(*localeconv)(void);
    double         (*pow)(double x, double y);

    /* ================================================================
     * libc: assert
     * ================================================================ */
    void (*__assert_func)(const char *file, int line,
                          const char *func, const char *expr);

    /* ================================================================
     * libgcc helpers
     *
     * These are normally emitted implicitly by the compiler when the
     * engine code performs operations that don't map directly to thumb
     * instructions (64-bit divide, float conversions, popcount). The
     * engine provides trampolines named exactly as the compiler expects.
     * ================================================================ */
    int64_t (*aeabi_d2lz)(double);      /* double -> int64 */
    float   (*aeabi_l2f)(int64_t);      /* int64 -> float */
    /* __aeabi_ldivmod returns a {quot,rem} pair in r0..r3 per AAPCS; can't
     * portably model that in a C function pointer. Expose separate quot
     * and rem wrappers; the engine's trampoline composes both. */
    int64_t (*ldivmod_quot)(int64_t, int64_t);
    int64_t (*ldivmod_rem)(int64_t, int64_t);
    int     (*popcountsi2)(unsigned);
    uint64_t (*uldivmod_quot)(uint64_t, uint64_t);
    uint64_t (*uldivmod_rem)(uint64_t, uint64_t);

    /* ================================================================
     * FatFs (ff.h)
     * ================================================================ */
    FRESULT (*f_opendir)(DIR *dp, const TCHAR *path);
    FRESULT (*f_closedir)(DIR *dp);
    FRESULT (*f_readdir)(DIR *dp, FILINFO *fno);

    /* ================================================================
     * G&W hardware: LCD
     * ================================================================ */
    void  (*lcd_swap)(void);
    void *(*lcd_get_active_buffer)(void);
    void *(*lcd_get_inactive_buffer)(void);
    void *(*lcd_clear_active_buffer)(void);
    void *(*lcd_clear_inactive_buffer)(void);

    /* ================================================================
     * G&W hardware: audio
     * ================================================================ */
    void     (*audio_start_playing)(uint16_t length);
    int16_t *(*audio_get_active_buffer)(void);
    void     (*audio_clear_active_buffer)(void);
    void     (*audio_clear_inactive_buffer)(void);

    /* ================================================================
     * G&W hardware: allocators
     * ================================================================ */
    void  *(*itc_malloc)(size_t size);
    void  *(*itc_calloc)(size_t count, size_t size);
    void   (*itc_init)(void);
    void  *(*ram_malloc)(size_t size);
    size_t (*ram_get_free_size)(void);

    /* ================================================================
     * G&W hardware: RTC
     * ================================================================ */
    uint8_t (*GW_GetCurrentYear)(void);
    uint8_t (*GW_GetCurrentMonth)(void);
    uint8_t (*GW_GetCurrentDay)(void);
    uint8_t (*GW_GetCurrentHour)(void);
    uint8_t (*GW_GetCurrentMinute)(void);
    uint8_t (*GW_GetCurrentSecond)(void);

    /* ================================================================
     * G&W hardware: watchdog + HAL
     * ================================================================ */
    void (*wdog_refresh)(void);
    void (*HAL_Delay)(uint32_t ms);
    uint32_t (*HAL_GetTick)(void);

    /* ================================================================
     * retro-go: system
     * ================================================================ */
    void (*odroid_system_init)(int app_id, int sample_rate);
    void (*odroid_system_emu_init)(state_handler_t load_cb,
                                   state_handler_t save_cb,
                                   screenshot_handler_t screenshot_cb,
                                   shutdown_handler_t shutdown_cb,
                                   sleep_post_wakeup_handler_t sleep_post_wakeup_cb,
                                   sram_save_handler_t sram_save_cb);
    void (*odroid_system_switch_app)(int app);  /* noreturn */

    /* ================================================================
     * retro-go: input / display
     * ================================================================ */
    void                     (*odroid_input_read_gamepad)(odroid_gamepad_state_t *out_state);
    odroid_display_scaling_t (*odroid_display_get_scaling_mode)(void);
    void                     (*odroid_display_set_scaling_mode)(odroid_display_scaling_t mode);

    /* ================================================================
     * retro-go: overlay / SD / settings
     * ================================================================ */
    int      (*odroid_overlay_draw_text)(uint16_t x, uint16_t y, uint16_t width,
                                         const char *text, uint16_t color, uint16_t color_bg);
    uint8_t *(*odroid_overlay_cache_file_in_flash)(const char *file_path,
                                                   uint32_t *file_size_p, bool byte_swap);
    int      (*odroid_sdcard_mkdir)(const char *path);
    int32_t  (*odroid_settings_app_int32_get)(const char *key, int32_t default_value);
    void     (*odroid_settings_app_int32_set)(const char *key, int32_t value);

    /* ================================================================
     * retro-go: common emulator loop
     * ================================================================ */
    bool    (*common_emu_frame_loop)(void);
    void    (*common_emu_input_loop)(odroid_gamepad_state_t *joystick,
                                     odroid_dialog_choice_t *game_options,
                                     void_callback_t repaint);
    void    (*common_emu_input_loop_handle_turbo)(odroid_gamepad_state_t *joystick);
    uint8_t (*common_emu_sound_get_volume)(void);
    bool    (*common_emu_sound_loop_is_muted)(void);
    void    (*common_emu_sound_sync)(bool use_nops);
    void    (*common_ingame_overlay)(void);

    /* ================================================================
     * Missing libc (discovered after v1 initial list)
     * ================================================================ */
    char *(*fgets)(char *, int, FILE *);
    void  (*free)(void *);
    void *(*realloc)(void *, size_t);
    int   (*ungetc)(int, FILE *);

    /* ================================================================
     * Firmware data pointers — engine reads firmware globals through
     * these instead of baking in firmware BSS addresses.
     * Each field points to the ADDRESS of the firmware global, so the
     * engine can read/write the live value via single indirection.
     * ================================================================ */
    void                        *common_emu_state_ptr; /* &common_emu_state */
    void                       **ROM_DATA_ptr;        /* &ROM_DATA */
    unsigned                    *ROM_DATA_LENGTH_ptr;  /* &ROM_DATA_LENGTH */
    void                       **ACTIVE_FILE_ptr;     /* &ACTIVE_FILE */
    uint8_t                    **pico8_code_flash_addr_ptr;
    uint32_t                    *pico8_code_flash_size_ptr;
    uint32_t                    *ram_start_ptr;        /* &ram_start */
    void                       **impure_ptr_ptr;      /* &_impure_ptr */
    void                        *dtcm_p8ram_start;    /* &__dtcm_p8ram_start__ (NULL when heap-allocated) */

    /* =====[ APPEND-ONLY FROM HERE — bump version on any change above ]===== */

    /* v1 append: DTCM heap allocator — engine calls this to get 64KB p8ram
     * at runtime instead of relying on a fixed linker section.
     * Freed implicitly by heap watermark reset between emulator launches. */
    void                       *(*dtcm_malloc)(size_t size);

    /* v1 append: deferred state load. Engine calls this from main loop AFTER
     * the first frame body so cart_co is in a stable post-init state. Routed
     * through ABI (not a direct call) so future firmware can change the
     * savestate-path/handler logic without an engine rebuild. */
    bool                        (*odroid_system_emu_load_state)(int slot);

    /* v1 append: audio mute toggle. Engine calls this when entering
     * start_paused state. Routed through ABI for the same reason. */
    void                        (*odroid_audio_mute)(bool mute);

    /* v1 append: LCD pixel-format / framebuffer-layout switch. PICO-8 and
     * NES use this to flip into 8-bit indexed mode (LUT8), halving the
     * LCD memory footprint and freeing 154K of bonus pool for the engine.
     * The lcd_mode argument is an int matching lcd_mode_t. */
    void                        (*lcd_setup_framebuffers)(int lcd_mode);
    void                        (*lcd_get_bonus_pool)(uint8_t **out_ptr,
                                                      size_t *out_size);
    void                        (*lcd_set_clut)(const uint32_t *clut,
                                                uint16_t count);

    /* ================================================================
     * v1 append: surface required to port a "classic" emulator core
     * (e.g. Watara Supervision) to the external-core model. Identified
     * by porting Core/Src/porting/wsv/main_wsv.c against this ABI.
     * ================================================================ */
    char    *(*strcpy)(char *, const char *);
    void    *(*malloc)(size_t size);

    void     (*lcd_wait_for_vblank)(void);
    void     (*lcd_set_refresh_rate)(uint32_t frequency);
    void     (*lcd_clear_buffers)(void);

    uint16_t (*audio_get_buffer_length)(void);

    /* odroid_display_get_filter_mode returns odroid_display_filter_t
     * (enum); exposed as `int` for the same reason as
     * lcd_setup_framebuffers — avoids pulling odroid_display.h's enum
     * into every core that only needs to forward the value. */
    int      (*odroid_display_get_filter_mode)(void);

    size_t   (*odroid_overlay_cache_file_in_ram)(const char *file_path,
                                                 uint8_t *dest_address);

} gw_firmware_abi_t;

/* The firmware publishes this instance at GW_FIRMWARE_ABI_ADDRESS via the
 * linker. Plugins read through (*gw_firmware_abi_ptr). */
extern const gw_firmware_abi_t g_firmware_abi;

/* Engine-side accessor: resolves the absolute address of the ABI struct
 * from VTOR. Stable across bank1/bank2 builds. */
static inline const gw_firmware_abi_t *gw_firmware_abi(void)
{
    uintptr_t base = *(const volatile uint32_t *)GW_VTOR_ADDRESS;
    return (const gw_firmware_abi_t *)(base + GW_FIRMWARE_ABI_OFFSET);
}

/* Convenience for plugin code: `GW_FIRMWARE_ABI.memcpy(...)`, etc. */
#define GW_FIRMWARE_ABI  (*gw_firmware_abi())

#ifdef __cplusplus
}
#endif
