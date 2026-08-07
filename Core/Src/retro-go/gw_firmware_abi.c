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
#include "odroid_display.h"
#include "ff.h"
#include "stm32h7xx_hal.h"
#include "gw_malloc.h"     /* ram_start */
#include "gw_ofw.h"
#include "crc32.h"
#include "rg_storage.h"
#include "hw_sha1.h"
#include "odroid_audio.h"
#include "rg_utils.h"
#include "gw_audio.h"
#include "hw_jpeg_decoder.h"
#include "lzma.h"
#include "lz4_depack.h"

/* newlib gettimeofday is a thin wrapper around _gettimeofday (syscalls.c). */
extern int _gettimeofday(struct timeval *tv, void *tzvp);
static int gw_abi_gettimeofday(struct timeval *tv, void *tz)
{
    return _gettimeofday(tv, tz);
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

/* Single entry point behind mem_alloc — see gw_firmware_abi.h's comment on
 * that field. Dispatches to the pool-specific *_calloc() (gw_malloc.c),
 * which all already implement "malloc + memset" — mem_alloc always zeroes,
 * callers passing count=1 get plain malloc(size) semantics plus a free
 * zero-init. */
static void *abi_mem_alloc(gw_mem_pool_t pool, size_t count, size_t size)
{
    switch (pool) {
    case GW_MEM_ITC:  return itc_calloc(count, size);
    case GW_MEM_RAM:  return ram_calloc(count, size);
    case GW_MEM_AHB:  return ahb_calloc(count, size);
    case GW_MEM_DTCM: return dtcm_calloc(count, size);
    default:          return NULL;
    }
}

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
    .mem_alloc         = abi_mem_alloc,
    .itc_init          = itc_init,
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
    .odroid_system_emu_init   = odroid_system_emu_init,
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

    .odroid_system_emu_load_state = odroid_system_emu_load_state,
    .odroid_audio_mute            = odroid_audio_mute,

    /* lcd_setup_framebuffers takes lcd_mode_t (enum); ABI exposes it as
     * `int` so the header doesn't need to leak the enum / pull in the
     * heavy gw_lcd.h. The cast is safe — enum and int are interchangeable
     * for parameter passing in C. */
    .lcd_setup_framebuffers       = (void (*)(int))lcd_setup_framebuffers,
    .lcd_get_bonus_pool           = lcd_get_bonus_pool,
    .lcd_set_clut                 = lcd_set_clut,

    /* v1 append: classic-core porting surface (see gw_firmware_abi.h) */
    .strcpy  = strcpy,
    .malloc  = malloc,

    .lcd_wait_for_vblank  = lcd_wait_for_vblank,
    .lcd_set_refresh_rate = lcd_set_refresh_rate,
    .lcd_clear_buffers    = lcd_clear_buffers,

    .audio_get_buffer_length = audio_get_buffer_length,

    .odroid_display_get_filter_mode = (int (*)(void))odroid_display_get_filter_mode,

    .odroid_overlay_cache_file_in_ram = odroid_overlay_cache_file_in_ram,

    /* v1 append: Mega Drive / gwenesis porting surface */
    .odroid_audio_init             = odroid_audio_init,
    .odroid_audio_sample_rate_get  = odroid_audio_sample_rate_get,
    .audio_start_playing_full_length = audio_start_playing_full_length,
    .audio_get_buffer_full_length     = audio_get_buffer_full_length,

    .common_emu_enable_dwt_cycles = common_emu_enable_dwt_cycles,
    .common_emu_get_dwt_cycles    = common_emu_get_dwt_cycles,
    .common_emu_clear_dwt_cycles  = common_emu_clear_dwt_cycles,

    .odroid_settings_cpu_oc_level_get = odroid_settings_cpu_oc_level_get,
    .SystemClock_Config               = SystemClock_Config,

    .get_ofw_is_mario = get_ofw_is_mario,

    /* odroid_system_get_path takes emu_path_type_t (enum); see the `int`
     * cast rationale above lcd_setup_framebuffers/odroid_display_get_filter_mode. */
    .odroid_system_get_path = (char *(*)(int, const char *))odroid_system_get_path,

    .lcd_get_pixel_position       = lcd_get_pixel_position,
    .lcd_sleep_while_swap_pending = lcd_sleep_while_swap_pending,

    .frame_counter_ptr = &frame_counter,

    /* v2 append: PC Engine / PC Engine CD porting surface */
    .crc32_le      = crc32_le,
    .cpumon_sleep  = cpumon_sleep,
    .vsscanf       = vsscanf,
    .strncat       = strncat,
    .odroid_settings_ActiveGameGenieCodes_is_enabled = odroid_settings_ActiveGameGenieCodes_is_enabled,

    .dma_counter_ptr                 = &dma_counter,
    .common_emu_sound_dma_marker_ptr = &common_emu_sound_dma_marker,

    /* v2 append: TGB Dual (Game Boy / Game Boy Color) porting surface */
    .GW_GetUnixTM                    = GW_GetUnixTM,
    .mktime                          = mktime,
    .lcd_clone                       = lcd_clone,
    .odroid_settings_Palette_get     = odroid_settings_Palette_get,
    .odroid_settings_Palette_set     = odroid_settings_Palette_set,

    /* v2 append: FCEUmm (NES) mappers.pak loader */
    .rg_storage_copy_file_range_to_ram =
        (size_t (*)(char *, uint8_t *, uint32_t, uint32_t, gw_file_progress_cb_t))rg_storage_copy_file_range_to_ram,

    /* v2 append: blueMSX (MSX) porting surface */
    .ahb_init                    = ahb_init,
    .ahb_only_malloc             = ahb_only_malloc,
    .odroid_audio_volume_get     = odroid_audio_volume_get,
    .calculate_sha1_file         = calculate_sha1_file,
    .calculate_sha1_file_limit   = calculate_sha1_file_limit,
    .calculate_sha1_hw           = calculate_sha1_hw,

    .localtime                   = localtime,
    .gettimeofday                = gw_abi_gettimeofday,
    .rg_storage_stat             = rg_storage_stat,
    .rg_storage_get_adjacent_files = rg_storage_get_adjacent_files,
    .rg_basename                 = rg_basename,
    .audio_stop_playing          = audio_stop_playing,

    /* v2 append: LCD-Game-Emulator (Game & Watch) */
    .GW_SetUnixTM                = GW_SetUnixTM,
    .lcd_is_swap_pending         = lcd_is_swap_pending,
    .JPEG_DecodeToFrameInit      = JPEG_DecodeToFrameInit,
    .JPEG_DecodeToFrame          = JPEG_DecodeToFrame,
    .JPEG_DecodeGetSize          = JPEG_DecodeGetSize,
    .JPEG_DecodeDeInit           = JPEG_DecodeDeInit,
    .lzma_inflate                = lzma_inflate,
    .lz4_uncompress              = lz4_uncompress,
    .lz4_get_file_size           = lz4_get_file_size,

    /* v2 append: Tamagotchi P1 */
    .common_emu_frame_loop_reset = common_emu_frame_loop_reset,
};
