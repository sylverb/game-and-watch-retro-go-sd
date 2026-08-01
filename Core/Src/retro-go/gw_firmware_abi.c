/*
 * Populate the versioned firmware ABI table used by runtime-loaded
 * plugin overlays. The struct is pinned to a fixed intflash address via
 * the linker script (see .firmware_abi section). See gw_firmware_abi.h
 * for the contract and backwards-compat rules.
 */

#include "gw_firmware_abi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <locale.h>
#include <time.h>
#include <math.h>
#include <errno.h>

#include "gw_lcd.h"
#include "gw_audio.h"
#include "gw_malloc.h"
#include "gw_buttons.h"
#include "main.h"
#include "rg_rtc.h"
#include "common.h"
#include "odroid_system.h"
#include "odroid_input.h"
#include "odroid_display.h"
#include "odroid_overlay.h"
#include "odroid_sdcard.h"
#include "odroid_settings.h"
#include "ff.h"
#include "stm32h7xx_hal.h"
#include "gw_malloc.h"     /* ram_start */

/* ABI keeps the historical 6-arg odroid_system_emu_init signature; Core
 * now takes a 7th cheat_update callback. Bridge here so old plugins keep
 * working (cheats stay NULL for ABI-loaded cores). */
static void abi_odroid_system_emu_init(state_handler_t load_cb,
                                       state_handler_t save_cb,
                                       screenshot_handler_t screenshot_cb,
                                       shutdown_handler_t shutdown_cb,
                                       sleep_post_wakeup_handler_t sleep_post_wakeup_cb,
                                       sram_save_handler_t sram_save_cb)
{
    odroid_system_emu_init(load_cb, save_cb, screenshot_cb, shutdown_cb,
                           sleep_post_wakeup_cb, sram_save_cb, NULL);
}

/* These are defined in rg_emulators.c */
extern uint8_t *pico8_code_flash_addr;
extern uint32_t pico8_code_flash_size;
/* DTCM p8 RAM linker symbol removed — now heap-allocated via dtcm_malloc */

/* newlib's __errno is not in a public header on all targets. */
extern int *__errno(void);
/* newlib assert handler. */
extern void __assert_func(const char *, int, const char *, const char *);
/* newlib reentrancy pointer (for errno / stdio macros). */
extern struct _reent *_impure_ptr;

/* Firmware applies --wrap=fflush for FatFS file descriptors.
 * The engine needs the REAL libc fflush (for FILE* streams like stdout).
 * __real_fflush is provided by the linker's --wrap mechanism. */
extern int __real_fflush(FILE *stream);

/* libgcc soft-integer helpers (exposed via typed wrappers below). */
static int64_t  abi_ldivmod_quot (int64_t a, int64_t b)  { return a / b; }
static int64_t  abi_ldivmod_rem  (int64_t a, int64_t b) { return a % b; }
static uint64_t abi_uldivmod_quot(uint64_t a, uint64_t b) { return a / b; }
static uint64_t abi_uldivmod_rem (uint64_t a, uint64_t b) { return a % b; }
extern int64_t __aeabi_d2lz(double);
extern float   __aeabi_l2f(int64_t);
extern int     __popcountsi2(unsigned);

/*
 * The struct is placed in .firmware_abi via the linker; that section is
 * pinned to GW_FIRMWARE_ABI_ADDRESS in the linker script.
 *
 * `used` keeps it through --gc-sections even if the firmware itself has
 * no references to the symbol. Plugins reach it via the fixed address
 * macro, not by symbol name.
 */
__attribute__((section(".firmware_abi"), used))
const gw_firmware_abi_t g_firmware_abi = {
    .version = GW_FIRMWARE_ABI_VERSION,
    .size    = sizeof(gw_firmware_abi_t),

    /* string.h */
    .memchr   = memchr,
    .memcmp   = memcmp,
    .memcpy   = memcpy,
    .memmem   = memmem,
    .memmove  = memmove,
    .memset   = memset,
    .strchr   = strchr,
    .strcmp   = strcmp,
    .strcoll  = strcoll,
    .strlen   = strlen,
    .strncmp  = strncmp,
    .strncpy  = strncpy,
    .strpbrk  = strpbrk,
    .strrchr  = strrchr,
    .strspn   = strspn,
    .strstr   = strstr,
    .strerror = strerror,

    /* ctype.h */
    .isalnum  = isalnum,
    .isalpha  = isalpha,
    .iscntrl  = iscntrl,
    .isgraph  = isgraph,
    .islower  = islower,
    .ispunct  = ispunct,
    .isspace  = isspace,
    .isupper  = isupper,
    .isxdigit = isxdigit,
    .tolower  = tolower,
    .toupper  = toupper,

    /* stdlib.h */
    .abort   = abort,
    .qsort   = qsort,
    .strtod  = strtod,
    .strtol  = strtol,

    /* stdio.h */
    .fopen     = fopen,
    .fclose    = fclose,
    .fread     = fread,
    .fwrite    = fwrite,
    .fseek     = fseek,
    .ftell     = ftell,
    .feof      = feof,
    .ferror    = ferror,
    .fgetc     = fgetc,
    .fputc     = fputc,
    .freopen   = freopen,
    .remove    = remove,
    .putchar   = putchar,
    .puts      = puts,
    .fflush    = __real_fflush,   /* real libc fflush, not the FatFS wrapper */
    .__errno   = __errno,
    .vfprintf  = vfprintf,
    .vprintf   = vprintf,
    .vsnprintf = vsnprintf,
    .vsprintf  = vsprintf,
    .vfscanf   = vfscanf,

    /* time.h / setjmp.h / locale.h / libm */
    .time       = time,
    .setjmp     = setjmp,
    .longjmp    = longjmp,
    .localeconv = localeconv,
    .pow        = pow,

    /* assert */
    .__assert_func = __assert_func,

    /* libgcc */
    .aeabi_d2lz   = __aeabi_d2lz,
    .aeabi_l2f    = __aeabi_l2f,
    .ldivmod_quot = abi_ldivmod_quot,
    .ldivmod_rem  = abi_ldivmod_rem,
    .popcountsi2  = __popcountsi2,
    .uldivmod_quot = abi_uldivmod_quot,
    .uldivmod_rem  = abi_uldivmod_rem,

    /* FatFs */
    .f_opendir  = f_opendir,
    .f_closedir = f_closedir,
    .f_readdir  = f_readdir,

    /* G&W LCD */
    .lcd_swap                  = lcd_swap,
    .lcd_get_active_buffer     = lcd_get_active_buffer,
    .lcd_get_inactive_buffer   = lcd_get_inactive_buffer,
    .lcd_clear_active_buffer   = lcd_clear_active_buffer,
    .lcd_clear_inactive_buffer = lcd_clear_inactive_buffer,

    /* G&W audio */
    .audio_start_playing         = audio_start_playing,
    .audio_get_active_buffer     = audio_get_active_buffer,
    .audio_clear_active_buffer   = audio_clear_active_buffer,
    .audio_clear_inactive_buffer = audio_clear_inactive_buffer,

    /* G&W allocators */
    .itc_malloc        = itc_malloc,
    .itc_calloc        = itc_calloc,
    .itc_init          = itc_init,
    .ram_malloc        = ram_malloc,
    .ram_get_free_size = ram_get_free_size,

    /* G&W RTC */
    .GW_GetCurrentYear   = GW_GetCurrentYear,
    .GW_GetCurrentMonth  = GW_GetCurrentMonth,
    .GW_GetCurrentDay    = GW_GetCurrentDay,
    .GW_GetCurrentHour   = GW_GetCurrentHour,
    .GW_GetCurrentMinute = GW_GetCurrentMinute,
    .GW_GetCurrentSecond = GW_GetCurrentSecond,

    /* G&W watchdog + HAL */
    .wdog_refresh = wdog_refresh,
    .HAL_Delay    = HAL_Delay,
    .HAL_GetTick  = HAL_GetTick,

    /* retro-go: system */
    .odroid_system_init       = odroid_system_init,
    .odroid_system_emu_init   = abi_odroid_system_emu_init,
    .odroid_system_switch_app = odroid_system_switch_app,

    /* retro-go: input / display */
    .odroid_input_read_gamepad       = odroid_input_read_gamepad,
    .odroid_display_get_scaling_mode = odroid_display_get_scaling_mode,
    .odroid_display_set_scaling_mode = odroid_display_set_scaling_mode,

    /* retro-go: overlay / SD / settings */
    .odroid_overlay_draw_text           = odroid_overlay_draw_text,
    .odroid_overlay_cache_file_in_flash = odroid_overlay_cache_file_in_flash,
    .odroid_sdcard_mkdir                = odroid_sdcard_mkdir,
    .odroid_settings_app_int32_get      = odroid_settings_app_int32_get,
    .odroid_settings_app_int32_set      = odroid_settings_app_int32_set,

    /* retro-go: common emulator loop */
    .common_emu_frame_loop             = common_emu_frame_loop,
    .common_emu_input_loop             = common_emu_input_loop,
    .common_emu_input_loop_handle_turbo= common_emu_input_loop_handle_turbo,
    .common_emu_sound_get_volume       = common_emu_sound_get_volume,
    .common_emu_sound_loop_is_muted    = common_emu_sound_loop_is_muted,
    .common_emu_sound_sync             = common_emu_sound_sync,
    .common_ingame_overlay             = common_ingame_overlay,

    /* Missing libc */
    .fgets   = fgets,
    .free    = free,
    .realloc = realloc,
    .ungetc  = ungetc,

    /* Firmware data pointers */
    .common_emu_state_ptr      = &common_emu_state,
    .ROM_DATA_ptr              = (void **)&ROM_DATA,
    .ROM_DATA_LENGTH_ptr       = &ROM_DATA_LENGTH,
    .ACTIVE_FILE_ptr           = (void **)&ACTIVE_FILE,
    .pico8_code_flash_addr_ptr = &pico8_code_flash_addr,
    .pico8_code_flash_size_ptr = &pico8_code_flash_size,
    .ram_start_ptr             = &ram_start,
    .impure_ptr_ptr            = (void **)&_impure_ptr,
    .dtcm_p8ram_start          = NULL,  /* no longer a fixed section — use dtcm_malloc */

    .dtcm_malloc               = dtcm_malloc,

    .odroid_system_emu_load_state = odroid_system_emu_load_state,
    .odroid_audio_mute            = odroid_audio_mute,

    /* lcd_setup_framebuffers takes lcd_mode_t (enum); ABI exposes it as
     * `int` so the header doesn't need to leak the enum / pull in the
     * heavy gw_lcd.h. The cast is safe — enum and int are interchangeable
     * for parameter passing in C. */
    .lcd_setup_framebuffers       = (void (*)(int))lcd_setup_framebuffers,
    .lcd_get_bonus_pool           = lcd_get_bonus_pool,
    .lcd_set_clut                 = lcd_set_clut,
};
