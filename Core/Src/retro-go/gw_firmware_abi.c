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
#include "rg_i18n.h"
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
#include "gw_ofw.h"
#include "crc32.h"
#include "rg_storage.h"
#include "hw_sha1.h"
#include "odroid_audio.h"
#include "rg_utils.h"
#include "hw_jpeg_decoder.h"
#include "lzma.h"
#include "lz4_depack.h"
#include "error_screens.h"
#include "gw_flash_alloc.h"
#include "gui.h"
#include "gw_sdcard.h"
#include "bitmaps.h"

#if CHEAT_CODES != 1
/* Header hides these when cheats are off; ABI still exports the slot. */
static bool odroid_settings_ActiveGameGenieCodes_is_enabled(char *game_path, int code_index)
{
    (void)game_path;
    (void)code_index;
    return false;
}
#endif

/* newlib gettimeofday is a thin wrapper around _gettimeofday (syscalls.c). */
extern int _gettimeofday(struct timeval *tv, void *tzvp);
static int gw_abi_gettimeofday(struct timeval *tv, void *tz)
{
    return _gettimeofday(tv, tz);
}

extern uint32_t SystemCoreClock;
static uint32_t gw_abi_get_SystemCoreClock(void)
{
    return SystemCoreClock;
}

/* lcd_mode_t lives in gw_lcd.h; ABI exposes the argument as int. */
static void gw_abi_lcd_setup_framebuffers(int lcd_mode)
{
    lcd_setup_framebuffers((lcd_mode_t)lcd_mode);
}

/* Several firmware headers still use K&R `foo()` (unspecified args). GCC
 * 15 treats assigning those to a `foo(void)` function pointer as
 * -Wincompatible-pointer-types. Thin (void) wrappers keep the ABI typed. */
static void *gw_abi_lcd_clear_active_buffer(void) { return lcd_clear_active_buffer(); }
static void *gw_abi_lcd_clear_inactive_buffer(void) { return lcd_clear_inactive_buffer(); }
static void gw_abi_lcd_clear_buffers(void) { lcd_clear_buffers(); }
static uint8_t gw_abi_lcd_backlight_get(void) { return lcd_backlight_get(); }
static void gw_abi_lcd_backlight_on(void) { lcd_backlight_on(); }
static void gw_abi_lcd_backlight_off(void) { lcd_backlight_off(); }
static uint32_t gw_abi_lcd_get_pixel_position(void) { return lcd_get_pixel_position(); }
static uint32_t gw_abi_JPEG_DecodeDeInit(void) { return JPEG_DecodeDeInit(); }
static odroid_battery_state_t gw_abi_odroid_input_read_battery(void)
{
    return odroid_input_read_battery();
}
static int gw_abi_odroid_audio_volume_get(void) { return odroid_audio_volume_get(); }
static int gw_abi_odroid_audio_sample_rate_get(void) { return odroid_audio_sample_rate_get(); }

static int gw_abi_odroid_display_get_filter_mode(void)
{
    return (int)odroid_display_get_filter_mode();
}
static odroid_display_backlight_t gw_abi_odroid_display_get_backlight(void)
{
    return odroid_display_get_backlight();
}

/* DMA2D M2M RGB565 for external cores. Own handle (JPEG has another);
 * both drive the same peripheral — START always re-Inits + ConfigLayer. */
static DMA2D_HandleTypeDef gw_abi_dma2d;

static uint32_t gw_abi_dma2d_m2m_rgb565_start(uint32_t src, uint32_t dst,
                                             uint16_t width, uint16_t height)
{
    gw_abi_dma2d.Instance = DMA2D;
    gw_abi_dma2d.Init.Mode = DMA2D_M2M;
    gw_abi_dma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    gw_abi_dma2d.Init.OutputOffset = 0;
    gw_abi_dma2d.Init.AlphaInverted = DMA2D_REGULAR_ALPHA;
    gw_abi_dma2d.Init.RedBlueSwap = DMA2D_RB_REGULAR;
    gw_abi_dma2d.Init.BytesSwap = DMA2D_BYTES_REGULAR;
    gw_abi_dma2d.Init.LineOffsetMode = DMA2D_LOM_PIXELS;
    if (HAL_DMA2D_Init(&gw_abi_dma2d) != HAL_OK)
        return 1;

    gw_abi_dma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_RGB565;
    gw_abi_dma2d.LayerCfg[1].InputOffset = 0;
    gw_abi_dma2d.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
    gw_abi_dma2d.LayerCfg[1].InputAlpha = 0xFF;
    gw_abi_dma2d.LayerCfg[1].AlphaInverted = DMA2D_REGULAR_ALPHA;
    gw_abi_dma2d.LayerCfg[1].RedBlueSwap = DMA2D_RB_REGULAR;
    if (HAL_DMA2D_ConfigLayer(&gw_abi_dma2d, 1) != HAL_OK)
        return 1;

    if (HAL_DMA2D_Start(&gw_abi_dma2d, src, dst, width, height) != HAL_OK)
        return 1;
    return 0;
}

static uint32_t gw_abi_dma2d_poll(uint32_t timeout_ms)
{
    return (uint32_t)HAL_DMA2D_PollForTransfer(&gw_abi_dma2d, timeout_ms);
}

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

/* Single entry for pool ALLOC / INIT / FREE_SIZE — see mem_ctl in
 * gw_firmware_abi.h. ALLOC always zeroes (calloc); count=1 → malloc+zero. */
static uintptr_t abi_mem_ctl(gw_mem_op_t op, gw_mem_pool_t pool, size_t count, size_t size)
{
    switch (op) {
    case GW_MEM_OP_ALLOC: {
        void *p = NULL;
        switch (pool) {
        case GW_MEM_ITC:        p = itc_calloc(count, size); break;
        case GW_MEM_RAM:        p = ram_calloc(count, size); break;
        case GW_MEM_AHB:
            p = ahb_calloc(count, size);
            break;
        case GW_MEM_DTC:       p = dtc_calloc(count, size); break;
        default:
            break;
        }
        return (uintptr_t)p;
    }
    case GW_MEM_OP_INIT:
        switch (pool) {
        case GW_MEM_ITC:
            itc_init();
            break;
        case GW_MEM_RAM:
            ram_init();
            break;
        case GW_MEM_DTC:
            dtc_init();
            break;
        default:
            break;
        }
        return 0;
    case GW_MEM_OP_FREE_SIZE:
        if (pool == GW_MEM_RAM)
            return (uintptr_t)ram_get_free_size();
        return 0;
    default:
        return 0;
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
    .cosf       = cosf,
    .sqrtf      = sqrtf,
    .log10      = log10,

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
    .lcd_swap                     = lcd_swap,
    .lcd_get_active_buffer        = lcd_get_active_buffer,
    .lcd_get_inactive_buffer      = lcd_get_inactive_buffer,
    .lcd_clear_active_buffer      = gw_abi_lcd_clear_active_buffer,
    .lcd_clear_inactive_buffer    = gw_abi_lcd_clear_inactive_buffer,
    .lcd_clear_buffers            = gw_abi_lcd_clear_buffers,
    .lcd_sync                     = lcd_sync,
    .lcd_clone                    = lcd_clone,
    .lcd_wait_for_vblank          = lcd_wait_for_vblank,
    .lcd_set_refresh_rate         = lcd_set_refresh_rate,
    .lcd_get_pixel_position       = gw_abi_lcd_get_pixel_position,
    .lcd_is_swap_pending          = lcd_is_swap_pending,
    .lcd_sleep_while_swap_pending = lcd_sleep_while_swap_pending,
    .lcd_backlight_set            = lcd_backlight_set,
    .lcd_backlight_get            = gw_abi_lcd_backlight_get,
    .lcd_backlight_on             = gw_abi_lcd_backlight_on,
    .lcd_backlight_off            = gw_abi_lcd_backlight_off,
    .lcd_setup_framebuffers       = gw_abi_lcd_setup_framebuffers,
    .lcd_get_bonus_pool           = lcd_get_bonus_pool,
    .lcd_set_clut                 = lcd_set_clut,

    /* G&W audio */
    .audio_start_playing            = audio_start_playing,
    .audio_start_playing_full_length = audio_start_playing_full_length,
    .audio_stop_playing             = audio_stop_playing,
    .audio_get_active_buffer        = audio_get_active_buffer,
    .audio_clear_active_buffer      = audio_clear_active_buffer,
    .audio_clear_inactive_buffer    = audio_clear_inactive_buffer,
    .audio_clear_buffers            = audio_clear_buffers,
    .audio_get_buffer_length        = audio_get_buffer_length,
    .audio_get_buffer_full_length   = audio_get_buffer_full_length,
    .audio_get_buffer_size          = audio_get_buffer_size,
    .odroid_audio_init              = odroid_audio_init,
    .odroid_audio_sample_rate_get   = gw_abi_odroid_audio_sample_rate_get,
    .odroid_audio_mute              = odroid_audio_mute,
    .odroid_audio_volume_get        = gw_abi_odroid_audio_volume_get,
    .odroid_audio_volume_set        = odroid_audio_volume_set,
    .pcm_attach                     = pcm_attach,
    .pcm_audio_enable               = pcm_audio_enable,
    .pcm_audio_set                  = pcm_audio_set,
    .pcm_audio_setpos               = pcm_audio_setpos,
    .pcm_audio_pos                  = pcm_audio_pos,

    /* G&W allocators */
    .mem_ctl = abi_mem_ctl,

    /* G&W RTC getters removed — cores use time()/localtime(). */

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
    .odroid_input_read_battery       = gw_abi_odroid_input_read_battery,
    .odroid_display_get_scaling_mode = odroid_display_get_scaling_mode,
    .odroid_display_set_scaling_mode = odroid_display_set_scaling_mode,
    .odroid_display_get_filter_mode  = gw_abi_odroid_display_get_filter_mode,
    .odroid_display_get_backlight    = gw_abi_odroid_display_get_backlight,
    .odroid_display_set_backlight    = odroid_display_set_backlight,

    /* retro-go: overlay / SD / settings */
    .odroid_overlay_draw_text                    = odroid_overlay_draw_text,
    .odroid_overlay_dialog                       = odroid_overlay_dialog,
    .odroid_overlay_draw_logo                    = odroid_overlay_draw_logo,
    .odroid_overlay_draw_battery                 = odroid_overlay_draw_battery,
    .odroid_overlay_clock                        = odroid_overlay_clock,
    .odroid_overlay_cache_file_in_ram            = odroid_overlay_cache_file_in_ram,
    .odroid_overlay_cache_file_in_flash          = odroid_overlay_cache_file_in_flash,
    .odroid_overlay_cache_file_in_flash_relocate =
        (uint8_t *(*)(const char *, uint32_t *, bool, gw_flash_relocate_cb_t))
            odroid_overlay_cache_file_in_flash_relocate,
    .draw_error_screen                           = draw_error_screen,
    .odroid_sdcard_mkdir                         = odroid_sdcard_mkdir,
    .odroid_settings_app_int32_get               = odroid_settings_app_int32_get,
    .odroid_settings_app_int32_set               = odroid_settings_app_int32_set,

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
    .ram_start_ptr             = &ram_start,
    .impure_ptr_ptr            = (void **)&_impure_ptr,

    .odroid_system_emu_load_state = odroid_system_emu_load_state,

    /* v1 append: classic-core porting surface (see gw_firmware_abi.h) */
    .strcpy  = strcpy,
    .malloc  = malloc,

    /* v1 append: Mega Drive / gwenesis */
    .odroid_settings_cpu_oc_level_get = odroid_settings_cpu_oc_level_get,
    .SystemClock_Config               = SystemClock_Config,

    .get_ofw_is_mario = get_ofw_is_mario,

    /* odroid_system_get_path takes emu_path_type_t (enum); cast to int. */
    .odroid_system_get_path = (char *(*)(int, const char *))odroid_system_get_path,

    .frame_counter_ptr = &frame_counter,

    /* v2 append: PC Engine / PC Engine CD porting surface */
    .crc32_le      = crc32_le,
    .cpumon_sleep  = cpumon_sleep,
    .vsscanf       = vsscanf,
    .strncat       = strncat,
    .odroid_settings_ActiveGameGenieCodes_is_enabled = odroid_settings_ActiveGameGenieCodes_is_enabled,

    .dma_counter_ptr                 = &dma_counter,
    .common_emu_sound_dma_marker_ptr = &common_emu_sound_dma_marker,

    /* TGB Dual (Game Boy / Game Boy Color) porting surface. */
    .odroid_settings_Palette_get     = odroid_settings_Palette_get,
    .odroid_settings_Palette_set     = odroid_settings_Palette_set,

    /* v2 append: FCEUmm (NES) mappers.pak loader */
    .rg_storage_copy_file_range_to_ram =
        (size_t (*)(char *, uint8_t *, uint32_t, uint32_t, gw_file_progress_cb_t))rg_storage_copy_file_range_to_ram,

    /* v2 append: blueMSX (MSX) */
    .calculate_sha1_file       = calculate_sha1_file,
    .calculate_sha1_file_limit = calculate_sha1_file_limit,
    .calculate_sha1_hw         = calculate_sha1_hw,

    .localtime                   = localtime,
    .gettimeofday                = gw_abi_gettimeofday,
    .rg_storage_stat             = rg_storage_stat,
    .rg_storage_get_adjacent_files = rg_storage_get_adjacent_files,
    .rg_basename                 = rg_basename,

    /* v2 append: LCD-Game-Emulator (Game & Watch) */
    .GW_SetUnixTM                = GW_SetUnixTM,
    .JPEG_DecodeToFrameInit      = JPEG_DecodeToFrameInit,
    .JPEG_DecodeToFrame          = JPEG_DecodeToFrame,
    .JPEG_DecodeGetSize          = JPEG_DecodeGetSize,
    .JPEG_DecodeDeInit           = gw_abi_JPEG_DecodeDeInit,
    .lzma_inflate                = lzma_inflate,
    .lz4_uncompress              = lz4_uncompress,
    .lz4_get_file_size           = lz4_get_file_size,

    /* v2 append: Tamagotchi P1 */
    .common_emu_frame_loop_reset = common_emu_frame_loop_reset,

    /* v2 append: GBA (gpSP) */
    .get_SystemCoreClock         = gw_abi_get_SystemCoreClock,

    /* v2 append: per-core option i18n */
    .i18n_get_text_width         = i18n_get_text_width,
    .i18n_draw_text_line         = i18n_draw_text_line,
    .i18n_lang_code              = i18n_lang_code,

    /* v2 append: live app descriptor (speedup / handlers) */
    .odroid_system_get_app       = odroid_system_get_app,

    /* v2 append: DMA2D M2M for external SNES (and similar) */
    .dma2d_m2m_rgb565_start      = gw_abi_dma2d_m2m_rgb565_start,
    .dma2d_poll                  = gw_abi_dma2d_poll,

    .curr_colors_ptr             = (void **)&curr_colors,

    /* v2 append: HW-SPI SD DMA wait poll (video PCM feed) */
    .sd_io_set_poll              = sd_io_set_poll,

    /* v2 append: file-manager homebrew */
    .rg_storage_scandir          = rg_storage_scandir,
    .rg_storage_delete           = rg_storage_delete,
    .get_darken_pixel_d          = get_darken_pixel_d,
    .i18n_get_text_height        = i18n_get_text_height,
    .odroid_overlay_get_font_size  = odroid_overlay_get_font_size,
    .odroid_overlay_get_font_width = odroid_overlay_get_font_width,
    .odroid_overlay_draw_fill_rect = odroid_overlay_draw_fill_rect,
};
