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
#include <sys/time.h>
#include <sys/types.h>

#include "odroid_system.h"
#include "odroid_input.h"
#include "odroid_display.h"
#include "common.h"
#include "ff.h"
#include "rg_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bump on any removal, reorder, or signature change. Append-only is safe
 * within a released version. While external cores are still in active
 * development (no released ABI compatibility window), fields may still
 * be removed/reordered without bumping — see the odroid_system_emu_init
 * comment further down. RTC read getters (GW_GetCurrent*, GW_GetUnixTM,
 * mktime) were dropped in favor of time()+localtime(). */
#define GW_FIRMWARE_ABI_VERSION  2u

/* Progress callback for ranged SD→RAM copies (matches rg_storage.h). Declared
 * here so gw_firmware_abi.h doesn't need to pull in rg_storage.h. */
typedef void (*gw_file_progress_cb_t)(uint32_t total_size, uint32_t total_processed, uint8_t progress);

/* Relocation pass while caching a file into QSPI (matches gw_flash_alloc.h). */
typedef void (*gw_flash_relocate_cb_t)(uint8_t *buffer, uint32_t length, uint32_t offset_in_file,
                                       uint8_t *file_address, uint32_t file_size);

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

/* Memory pool selector for mem_ctl() below. Replaces what used to be one
 * ABI function pointer per pool (itc_malloc/itc_calloc, ram_malloc,
 * ahb_malloc/ahb_calloc, dtc_malloc) plus separate itc_init / ram_init /
 * ram_get_free_size slots — see mem_ctl's comment. */
typedef enum {
    GW_MEM_ITC  = 0,  /* 64KB ITCM bump pool */
    GW_MEM_RAM  = 1,  /* RAM_EMU bump pool (this core's ram_start budget) */
    GW_MEM_AHB  = 2,  /* AHB newlib heap (malloc/free via ahb_*) */
    GW_MEM_DTC  = 3,  /* DTCM bump pool (dtc_*) */
} gw_mem_pool_t;

typedef enum {
    GW_MEM_OP_ALLOC     = 0,  /* calloc from pool; count=1 → malloc(size)+zero */
    GW_MEM_OP_INIT      = 1,  /* reset pool bump (ITC/RAM/DTCM only) */
    GW_MEM_OP_FREE_SIZE = 2,  /* free bytes in pool (RAM only today) */
} gw_mem_op_t;

/* Hardware JPEG ops for jpeg_ctl() below. Replaces four ABI slots
 * (JPEG_DecodeToFrameInit/ToFrame/GetSize/DeInit) with one entry —
 * only the LCD-Game-Emulator core uses this surface. */
typedef enum {
    GW_JPEG_INIT     = 0,  /* a=JPEG_Buffer, b=JPEG_Buffer_Size */
    GW_JPEG_DECODE   = 1,  /* a=Src, b=Dest, c=(x<<16)|y, d=luma_alpha */
    GW_JPEG_GET_SIZE = 2,  /* a=Src, b=(uintptr_t)width*, c=(uintptr_t)height* */
    GW_JPEG_DEINIT   = 3,  /* no args */
} gw_jpeg_op_t;

/* LCD framebuffer selector — argument to GW_LCD_BUFFER. */
typedef enum {
    GW_LCD_BUF_ACTIVE   = 0,
    GW_LCD_BUF_INACTIVE = 1,
    GW_LCD_BUF_BOTH     = 2,  /* clear_buffers — CLEAR flag required */
} gw_lcd_buf_t;

#define GW_LCD_CLEAR  1u  /* OR into GW_LCD_BUFFER flags to zero buffer(s) */

/* Direction for GW_LCD_COPY_FB — replaces lcd_sync + lcd_clone. */
typedef enum {
    GW_LCD_COPY_ACTIVE_TO_INACTIVE = 0,  /* former lcd_sync() */
    GW_LCD_COPY_INACTIVE_TO_ACTIVE = 1,  /* former lcd_clone() */
} gw_lcd_copy_t;

/* Unified LCD ops for lcd_ctl() — absorbs swap/buffer/copy plus the
 * former leftover slots (setup_framebuffers, bonus pool, CLUT, vblank,
 * refresh, pixel position, swap-pending, backlight). */
typedef enum {
    GW_LCD_SWAP            = 0,
    GW_LCD_BUFFER          = 1,  /* a=gw_lcd_buf_t, b=flags → (uintptr_t)ptr */
    GW_LCD_COPY_FB         = 2,  /* a=gw_lcd_copy_t */
    GW_LCD_SETUP_FB        = 3,  /* a=lcd_mode (int) */
    GW_LCD_GET_BONUS_POOL  = 4,  /* a=(uintptr_t)uint8_t**, b=(uintptr_t)size_t* */
    GW_LCD_SET_CLUT        = 5,  /* a=(uintptr_t)clut*, b=count */
    GW_LCD_WAIT_VBLANK     = 6,
    GW_LCD_SET_REFRESH     = 7,  /* a=frequency */
    GW_LCD_GET_PIXEL_POS   = 8,  /* → pixel position */
    GW_LCD_IS_SWAP_PENDING = 9,  /* → non-zero if swap pending */
    GW_LCD_BACKLIGHT_SET   = 10, /* a=brightness */
} gw_lcd_op_t;

/* Hardware audio ops for audio_ctl() below. Replaces the former per-call
 * ABI slots (audio_start_playing / get_active / clear_* / get_*_length /
 * start_playing_full_length / stop_playing + odroid_audio_init / mute /
 * sample_rate_get / volume_get) with one entry. Bridge re-exposes the
 * historical names as thin wrappers. */
typedef enum {
    GW_AUDIO_START           = 0,  /* a = half-buffer sample count */
    GW_AUDIO_START_FULL      = 1,  /* a = full DMA sample count */
    GW_AUDIO_STOP            = 2,
    GW_AUDIO_GET_ACTIVE      = 3,  /* → (uintptr_t)int16_t* */
    GW_AUDIO_CLEAR_ACTIVE    = 4,
    GW_AUDIO_CLEAR_INACTIVE  = 5,
    GW_AUDIO_CLEAR_BOTH      = 6,  /* full DMA buffer memset */
    GW_AUDIO_GET_LENGTH      = 7,  /* → half-buffer sample count */
    GW_AUDIO_GET_FULL_LENGTH = 8,  /* → full DMA sample count */
    GW_AUDIO_INIT            = 9,  /* a = sample_rate (odroid_audio_init) */
    GW_AUDIO_SAMPLE_RATE_GET = 10,
    GW_AUDIO_MUTE            = 11, /* a = mute bool */
    GW_AUDIO_VOLUME_GET      = 12,
} gw_audio_op_t;

/* FatFs directory ops for fatfs_dir_ctl(). */
typedef enum {
    GW_FATFS_OPENDIR  = 0,  /* a=DIR*, b=path */
    GW_FATFS_CLOSEDIR = 1,  /* a=DIR* */
    GW_FATFS_READDIR  = 2,  /* a=DIR*, b=FILINFO* */
} gw_fatfs_dir_op_t;

/* Odroid display scaling/filter for display_ctl(). */
typedef enum {
    GW_DISP_GET_SCALING = 0,  /* → odroid_display_scaling_t as uint */
    GW_DISP_SET_SCALING = 1,  /* a = mode */
    GW_DISP_GET_FILTER  = 2,  /* → filter mode as int */
} gw_disp_op_t;

/* Odroid input for input_ctl() — gamepad + battery (BQ24072 behind
 * firmware). Replaces the former standalone odroid_input_read_gamepad /
 * odroid_input_read_battery ABI slots. Bridge re-exposes those names. */
typedef enum {
    GW_INPUT_READ_GAMEPAD = 0,  /* a = odroid_gamepad_state_t* */
    GW_INPUT_READ_BATTERY = 1,  /* a = odroid_battery_state_t* */
} gw_input_op_t;

/* Hardware SHA-1 for sha1_ctl(). calculate_sha1_file is composed in the
 * bridge as FILE_LIMIT with max_bytes=(ssize_t)-1 (whole file). */
typedef enum {
    GW_SHA1_FILE_LIMIT = 0,  /* a=path, b=(uintptr_t)(ssize_t)max_bytes, c=out[20] */
    GW_SHA1_HW         = 1,  /* a=data, b=len, c=out[20] */
} gw_sha1_op_t;

/* LZ4 helpers for lz4_ctl(). lzma_inflate stays separate (different codec). */
typedef enum {
    GW_LZ4_UNCOMPRESS = 0,  /* a=src, b=dst → uncompressed size */
    GW_LZ4_GET_SIZE   = 1,  /* a=src → file size */
} gw_lz4_op_t;

/* DMA2D ops for dma2d_ctl() — RGB565 M2M blit used by the SNES core
 * (async present while audio runs). Firmware owns the HAL handle; cores
 * must not link stm32h7xx_hal_dma2d. Reconfigure+start every frame so a
 * prior JPEG/cover path that left different Mode/offsets cannot poison
 * the next blit (same rule as jshsakura main_snes.c). */
typedef enum {
    /* a=src, b=dst, c=(width<<16)|height → 0 ok, nonzero0 HAL failure */
    GW_DMA2D_M2M_RGB565_START = 0,
    /* a=timeout_ms → HAL_StatusTypeDef (HAL_OK=0, HAL_TIMEOUT, …) */
    GW_DMA2D_POLL             = 1,
} gw_dma2d_op_t;

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
     *
     * time() is the canonical wall-clock read for cores: pair with
     * localtime() further down for calendar fields. Do not re-add
     * per-field RTC getters.
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
     * FatFs directory API (ff.h) — one ctl for opendir/closedir/readdir.
     * ================================================================ */
    FRESULT (*fatfs_dir_ctl)(gw_fatfs_dir_op_t op, void *a, void *b);

    /* ================================================================
     * G&W hardware: LCD
     *
     * lcd_ctl() is the single entry for swap, framebuffer get/clear/
     * copy, LUT8 setup, bonus pool, CLUT, vblank, refresh rate, pixel
     * position, swap-pending poll, and backlight. Bridge re-exposes
     * historical names as thin wrappers. lcd_sleep_while_swap_pending
     * is composed in the bridge from IS_SWAP_PENDING + WFI (no slot).
     * ================================================================ */
    uintptr_t (*lcd_ctl)(gw_lcd_op_t op, uint32_t a, uint32_t b, uint32_t c);

    /* ================================================================
     * G&W hardware: audio
     *
     * audio_ctl() is the single entry for DMA start/stop, buffer get/
     * clear, length queries, and the odroid mute/init/rate/volume
     * helpers. Replaces twelve former slots (still ABI v2 while cores
     * are in active development). Bridge re-exposes historical names
     * (audio_start_playing, odroid_audio_mute, …) as thin wrappers;
     * audio_get_buffer_size is composed as GET_LENGTH * sizeof(int16_t).
     * ================================================================ */
    uintptr_t (*audio_ctl)(gw_audio_op_t op, uint32_t a);

    /* ================================================================
     * G&W hardware: allocators
     *
     * mem_ctl() is the single entry for every pool-based allocator op
     * (ALLOC / INIT / FREE_SIZE) across ITC / RAM_EMU / AHB / DTCM.
     * ALLOC always zeroes (calloc semantics);
     * pass count=1 for malloc(size)+zero. Replaces the former mem_alloc
     * + itc_init + ram_get_free_size + ram_init slots.
     * gw_core_bridge.c re-exposes the historical per-pool names as thin
     * wrappers so core source is unaffected.
     *
     * Returns: ALLOC → (uintptr_t)ptr; INIT → 0; FREE_SIZE → free bytes.
     * ================================================================ */
    uintptr_t (*mem_ctl)(gw_mem_op_t op, gw_mem_pool_t pool, size_t count, size_t size);

    /* G&W hardware RTC getters (GW_GetCurrentYear/Month/Day/Hour/Minute/
     * Second, GW_GetUnixTM, mktime) were removed during external-core
     * development (still ABI v2 — no released compatibility window).
     * Cores get wall-clock via time()+localtime() (and gettimeofday for
     * sub-second). Firmware UI still calls the rg_rtc.c helpers directly.
     * GW_SetUnixTM (write path) remains further down. */

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
    /* cheat_update_cb (7th arg) added for TGB Dual (Game Boy / Game Boy
     * Color): every core in this repo is rebuilt from source alongside the
     * firmware (the packaged core binaries under cores/ are gitignored,
     * nothing is distributed as a prebuilt blob yet), so this branch has
     * no released-ABI compatibility window to preserve — no
     * GW_FIRMWARE_ABI_VERSION bump needed for this signature change (see
     * that macro's comment above). */
    void (*odroid_system_emu_init)(state_handler_t load_cb,
                                   state_handler_t save_cb,
                                   screenshot_handler_t screenshot_cb,
                                   shutdown_handler_t shutdown_cb,
                                   sleep_post_wakeup_handler_t sleep_post_wakeup_cb,
                                   sram_save_handler_t sram_save_cb,
                                   cheat_update_handler_t cheat_update_cb);
    void (*odroid_system_switch_app)(int app);  /* noreturn */

    /* ================================================================
     * retro-go: input / display
     *
     * input_ctl() folds gamepad + battery reads. display_ctl() folds
     * get/set scaling + get filter.
     * ================================================================ */
    uintptr_t (*input_ctl)(gw_input_op_t op, void *a);
    uintptr_t (*display_ctl)(gw_disp_op_t op, uint32_t a);

    /* ================================================================
     * retro-go: overlay / SD / settings
     * (odroid_overlay_cache_file_in_flash composed as relocate(..., NULL).)
     * ================================================================ */
    int      (*odroid_overlay_draw_text)(uint16_t x, uint16_t y, uint16_t width,
                                         const char *text, uint16_t color, uint16_t color_bg);
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
    uint32_t                    *ram_start_ptr;        /* &ram_start */
    void                       **impure_ptr_ptr;      /* &_impure_ptr */

    /* =====[ APPEND-ONLY FROM HERE — bump version on any change above ]===== */

    /* v1 append: deferred state load. Engine calls this from main loop AFTER
     * the first frame body so cart_co is in a stable post-init state. Routed
     * through ABI (not a direct call) so future firmware can change the
     * savestate-path/handler logic without an engine rebuild. */
    bool                        (*odroid_system_emu_load_state)(int slot);

    /* (odroid_audio_mute folded into audio_ctl — see audio block above.) */

    /* (lcd_setup_framebuffers / lcd_get_bonus_pool / lcd_set_clut folded
     * into lcd_ctl — see LCD block above.) */

    /* ================================================================
     * v1 append: surface required to port a "classic" emulator core
     * (e.g. Watara Supervision) to the external-core model. Identified
     * by porting Core/Src/porting/wsv/main_wsv.c against this ABI.
     * ================================================================ */
    char    *(*strcpy)(char *, const char *);
    void    *(*malloc)(size_t size);

    /* (lcd_wait_for_vblank / lcd_set_refresh_rate / audio_get_buffer_length
     * folded into lcd_ctl / audio_ctl.) */

    /* (odroid_display_get_filter_mode folded into display_ctl.) */

    size_t   (*odroid_overlay_cache_file_in_ram)(const char *file_path,
                                                 uint8_t *dest_address);

    /* ================================================================
     * v1 append: surface required to port the Mega Drive / Genesis
     * (gwenesis) core to the external-core model. Identified by porting
     * Core/Src/porting/gwenesis/main_gwenesis.c against this ABI.
     * (odroid_audio_* / audio_*_full_length folded into audio_ctl.
     * DWT cycle helpers are implemented locally in the bridge via CMSIS
     * MMIO — no ABI slots.)
     * ================================================================ */
    uint8_t  (*odroid_settings_cpu_oc_level_get)(void);
    /* SystemClock_Config's argument is the CPU overclock level (0 = stock);
     * see Core/Src/main.c. On OSPI1 SD hardware any non-zero request is
     * forced back to stock inside SystemClock_Config. */
    void     (*SystemClock_Config)(uint8_t new_oc_level);

    bool     (*get_ofw_is_mario)(void);

    /* odroid_system_get_path's `type` is emu_path_type_t (odroid_system.h),
     * exposed as `int` for the same reason as lcd_setup_framebuffers.
     * Returns a strdup'd string the caller must free(). */
    char    *(*odroid_system_get_path)(int type, const char *romPath);

    /* (lcd_get_pixel_position folded into lcd_ctl.) */

    /* frame_counter (gw_lcd.h): incremented by the LCD vsync ISR. Engine
     * reads the live value through frame_counter_ptr instead of linking
     * against the firmware's global directly. */
    uint32_t                    *frame_counter_ptr;

    /* ================================================================
     * v2 append: surface required to port PC Engine / PC Engine CD
     * (multi-system, multi-segment core) to the external-core model.
     * Identified by porting Core/Src/porting/pce/main_pce.c (+ pce_cd.c)
     * against this ABI. Pure append — no version bump needed.
     * ================================================================ */
    /* Matches Core/Inc/porting/crc32.h's exact declared signature
     * (`unsigned int`/`unsigned char const *`, not uint32_t/uint8_t*) —
     * some arm-none-eabi/newlib configurations typedef uint32_t as `long
     * unsigned int` rather than `unsigned int`, and initializing this
     * pointer field from the real crc32_le function is then an
     * incompatible-pointer-types error despite both being 32-bit. */
    unsigned int (*crc32_le)(unsigned int crc, const unsigned char *buf, unsigned int len);
    void     (*cpumon_sleep)(void);
    int      (*vsscanf)(const char *str, const char *format, va_list ap);
    char    *(*strncat)(char *dest, const char *src, size_t n);
    bool     (*odroid_settings_ActiveGameGenieCodes_is_enabled)(char *game_path, int code_index);

    /* dma_counter (gw_audio.h) / common_emu_sound_dma_marker (common.h):
     * both incremented/compared by the audio DMA ISR + common_emu_sound_sync
     * to pace emulation to real playback time. PCE's CD-DA prefetch loop
     * (pce_sound_sync_with_prefetch) needs to observe/advance the same
     * counters common_emu_sound_sync() uses internally, so it can spend the
     * pacer wait prefetching CD sectors instead of just sleeping — exposed
     * as data pointers, same pattern as frame_counter_ptr. */
    uint32_t                    *dma_counter_ptr;
    uint32_t                    *common_emu_sound_dma_marker_ptr;

    /* ================================================================
     * v2 append: surface required to port TGB Dual (Game Boy / Game Boy
     * Color, C++) to the external-core model. Identified by porting
     * Core/Src/porting/gb_tgbdual/main_gb_tgbdual.cpp (+ gw_renderer.cpp)
     * against this ABI. (GW_GetUnixTM/mktime were dropped during
     * external-core development — use time()+localtime() instead.
     * lcd_clone was folded into lcd_copy_fb — see the LCD block above.)
     * ================================================================ */
    int32_t  (*odroid_settings_Palette_get)(void);
    void     (*odroid_settings_Palette_set)(int32_t value);

    /* ================================================================
     * v2 append: FCEUmm (NES) — ranged SD→RAM copy for mappers.pak blobs
     * (nes_fceu_mappers.c). Same append-only / no version bump policy as
     * the other v2 fields above.
     * ================================================================ */
    size_t   (*rg_storage_copy_file_range_to_ram)(char *file_path, uint8_t *ram_dest,
                                                  uint32_t offset, uint32_t length,
                                                  gw_file_progress_cb_t file_progress_cb);

    /* ================================================================
     * v2 append: blueMSX (MSX) porting surface. Identified by porting
     * Core/Src/porting/msx/main_msx.c (+ msx_database.c) against this ABI.
     * (ahb_* → mem_ctl; odroid_audio_volume_get → audio_ctl;
     * calculate_sha1_* → sha1_ctl. calculate_sha1_file is bridge-composed
     * as FILE_LIMIT with max_bytes=-1.)
     * ================================================================ */
    int8_t   (*sha1_ctl)(gw_sha1_op_t op, uintptr_t a, uintptr_t b, uintptr_t c);

    /* ================================================================
     * v2 append: blueMSX extras (RTC init, disk-swap UI, ROM loader).
     * ================================================================ */
    struct tm *(*localtime)(const time_t *timer);
    int      (*gettimeofday)(struct timeval *tv, void *tz);
    rg_stat_t (*rg_storage_stat)(const char *path);
    bool     (*rg_storage_get_adjacent_files)(const char *path, char *prev_path,
                                              char *next_path);
    const char *(*rg_basename)(const char *path);
    /* (audio_stop_playing folded into audio_ctl.) */

    /* ================================================================
     * v2 append: LCD-Game-Emulator (Game & Watch handhelds).
     * GW_SetUnixTM is the only RTC write entry left after the read-side
     * getters were dropped (no portable libc setter on this firmware).
     * jpeg_ctl replaces the former four JPEG_Decode* slots (same
     * ctl-style unification; still ABI v2 while cores are in
     * active development). Bridge re-exposes JPEG_DecodeToFrameInit /
     * ToFrame / GetSize / DeInit as thin wrappers so gw_romloader.c
     * is unchanged.
     * ================================================================ */
    void     (*GW_SetUnixTM)(struct tm *tm);
    /* (lcd_is_swap_pending folded into lcd_ctl.) */
    uint32_t (*jpeg_ctl)(gw_jpeg_op_t op, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
    size_t   (*lzma_inflate)(uint8_t *dst, size_t dst_size, const uint8_t *src, size_t src_size);
    unsigned int (*lz4_ctl)(gw_lz4_op_t op, const void *a, void *b);

    /* ================================================================
     * v2 append: Tamagotchi P1 (tamalib) — frame-pacing reset after
     * save-state catch-up fast-forward (static frame_integrator lives
     * in firmware common.c).
     * ================================================================ */
    void     (*common_emu_frame_loop_reset)(void);

    /* ================================================================
     * v2 append: GBA (gpSP) — host CPU clock after SystemClock_Config
     * overclock (CMSIS SystemCoreClock is a firmware global; cores must
     * not take its address across the ABI boundary). Plus XIP cache with
     * relocation pass, fatal UI helpers, and backlight. (lcd_sync was
     * folded into lcd_copy_fb — see the LCD block above.)
     * ================================================================ */
    uint32_t (*get_SystemCoreClock)(void);
    /* Plain cache_file_in_flash is bridge-composed as relocate(..., NULL). */
    uint8_t *(*odroid_overlay_cache_file_in_flash_relocate)(
        const char *file_path, uint32_t *file_size_p, bool byte_swap,
        gw_flash_relocate_cb_t relocate_cb);
    /* (lcd_backlight_set folded into lcd_ctl.) */
    void     (*draw_error_screen)(const char *main_line, const char *line_1, const char *line_2);

    /* ================================================================
     * v2 append: per-core option i18n. Returns the active UI language
     * code ("en_us", "fr_fr", "zh_cn", ...). Cores look up their own
     * string tables via gw_i18n() (core_common) with English fallback —
     * curr_lang / lang_t stay firmware-private.
     * ================================================================ */
    const char *(*i18n_lang_code)(void);

    /* ================================================================
     * v2 append: live app descriptor (speedupEnabled, handlers, …).
     * Needed by cores that pace audio off DMA only at 1x (WonderSwan).
     * Append-only while ABI v2 is unpublished — no version bump;
     * required_abi_min_size grows for cores that link this slot.
     * ================================================================ */
    rg_app_desc_t *(*odroid_system_get_app)(void);

    /* ================================================================
     * v2 append: DMA2D M2M RGB565 for external cores (SNES present_frame).
     * Append-only while ABI v2 is unpublished — no version bump.
     * ================================================================ */
    uint32_t (*dma2d_ctl)(gw_dma2d_op_t op, uint32_t a, uint32_t b, uint32_t c);

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
