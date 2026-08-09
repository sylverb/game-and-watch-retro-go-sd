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
#include "error_screens.h"
#include "gw_flash_alloc.h"

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

/* Single ABI entry for the hardware JPEG decoder (LCD-Game-Emulator).
 * Dispatches to the historical JPEG_Decode* helpers; argument packing
 * matches gw_jpeg_op_t in gw_firmware_abi.h. */
static uint32_t gw_abi_jpeg_ctl(gw_jpeg_op_t op, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    switch (op) {
    case GW_JPEG_INIT:
        return JPEG_DecodeToFrameInit(a, b);
    case GW_JPEG_DECODE:
        return JPEG_DecodeToFrame(a, b, (uint16_t)(c >> 16), (uint16_t)c, (uint8_t)d);
    case GW_JPEG_GET_SIZE:
        return JPEG_DecodeGetSize(a, (uint32_t *)(uintptr_t)b, (uint32_t *)(uintptr_t)c);
    case GW_JPEG_DEINIT:
        return JPEG_DecodeDeInit();
    default:
        return (uint32_t)-1;
    }
}

/* Unified LCD — see lcd_ctl in gw_firmware_abi.h. */
static uintptr_t gw_abi_lcd_ctl(gw_lcd_op_t op, uint32_t a, uint32_t b, uint32_t c)
{
    (void)c;
    switch (op) {
    case GW_LCD_SWAP:
        lcd_swap();
        return 0;
    case GW_LCD_BUFFER: {
        const int do_clear = (b & GW_LCD_CLEAR) != 0;
        switch ((gw_lcd_buf_t)a) {
        case GW_LCD_BUF_ACTIVE:
            return (uintptr_t)(do_clear ? lcd_clear_active_buffer() : lcd_get_active_buffer());
        case GW_LCD_BUF_INACTIVE:
            return (uintptr_t)(do_clear ? lcd_clear_inactive_buffer() : lcd_get_inactive_buffer());
        case GW_LCD_BUF_BOTH:
            if (do_clear)
                lcd_clear_buffers();
            return 0;
        default:
            return 0;
        }
    }
    case GW_LCD_COPY_FB:
        if ((gw_lcd_copy_t)a == GW_LCD_COPY_INACTIVE_TO_ACTIVE)
            lcd_clone();
        else
            lcd_sync();
        return 0;
    case GW_LCD_SETUP_FB:
        lcd_setup_framebuffers((lcd_mode_t)a);
        return 0;
    case GW_LCD_GET_BONUS_POOL:
        lcd_get_bonus_pool((uint8_t **)(uintptr_t)a, (size_t *)(uintptr_t)b);
        return 0;
    case GW_LCD_SET_CLUT:
        lcd_set_clut((const uint32_t *)(uintptr_t)a, (uint16_t)b);
        return 0;
    case GW_LCD_WAIT_VBLANK:
        lcd_wait_for_vblank();
        return 0;
    case GW_LCD_SET_REFRESH:
        lcd_set_refresh_rate(a);
        return 0;
    case GW_LCD_GET_PIXEL_POS:
        return (uintptr_t)lcd_get_pixel_position();
    case GW_LCD_IS_SWAP_PENDING:
        return (uintptr_t)lcd_is_swap_pending();
    case GW_LCD_BACKLIGHT_SET:
        lcd_backlight_set((uint8_t)a);
        return 0;
    default:
        return 0;
    }
}

/* Unified audio DMA / odroid helpers — see audio_ctl in gw_firmware_abi.h. */
static uintptr_t gw_abi_audio_ctl(gw_audio_op_t op, uint32_t a)
{
    switch (op) {
    case GW_AUDIO_START:
        audio_start_playing((uint16_t)a);
        return 0;
    case GW_AUDIO_START_FULL:
        audio_start_playing_full_length((uint16_t)a);
        return 0;
    case GW_AUDIO_STOP:
        audio_stop_playing();
        return 0;
    case GW_AUDIO_GET_ACTIVE:
        return (uintptr_t)audio_get_active_buffer();
    case GW_AUDIO_CLEAR_ACTIVE:
        audio_clear_active_buffer();
        return 0;
    case GW_AUDIO_CLEAR_INACTIVE:
        audio_clear_inactive_buffer();
        return 0;
    case GW_AUDIO_CLEAR_BOTH:
        audio_clear_buffers();
        return 0;
    case GW_AUDIO_GET_LENGTH:
        return (uintptr_t)audio_get_buffer_length();
    case GW_AUDIO_GET_FULL_LENGTH:
        return (uintptr_t)audio_get_buffer_full_length();
    case GW_AUDIO_INIT:
        odroid_audio_init((int)a);
        return 0;
    case GW_AUDIO_SAMPLE_RATE_GET:
        return (uintptr_t)(unsigned)odroid_audio_sample_rate_get();
    case GW_AUDIO_MUTE:
        odroid_audio_mute(a != 0);
        return 0;
    case GW_AUDIO_VOLUME_GET:
        return (uintptr_t)(unsigned)odroid_audio_volume_get();
    default:
        return 0;
    }
}

/* FatFs directory ops. */
static FRESULT gw_abi_fatfs_dir_ctl(gw_fatfs_dir_op_t op, void *a, void *b)
{
    switch (op) {
    case GW_FATFS_OPENDIR:
        return f_opendir((DIR *)a, (const TCHAR *)b);
    case GW_FATFS_CLOSEDIR:
        return f_closedir((DIR *)a);
    case GW_FATFS_READDIR:
        return f_readdir((DIR *)a, (FILINFO *)b);
    default:
        return FR_INVALID_PARAMETER;
    }
}

/* Odroid display scaling / filter. */
static uintptr_t gw_abi_display_ctl(gw_disp_op_t op, uint32_t a)
{
    switch (op) {
    case GW_DISP_GET_SCALING:
        return (uintptr_t)odroid_display_get_scaling_mode();
    case GW_DISP_SET_SCALING:
        odroid_display_set_scaling_mode((odroid_display_scaling_t)a);
        return 0;
    case GW_DISP_GET_FILTER:
        return (uintptr_t)(unsigned)odroid_display_get_filter_mode();
    default:
        return 0;
    }
}

/* Hardware SHA-1. */
static int8_t gw_abi_sha1_ctl(gw_sha1_op_t op, uintptr_t a, uintptr_t b, uintptr_t c)
{
    switch (op) {
    case GW_SHA1_FILE_LIMIT:
        return calculate_sha1_file_limit((const char *)a, (ssize_t)b, (uint8_t *)c);
    case GW_SHA1_HW:
        return calculate_sha1_hw((const uint8_t *)a, (size_t)b, (uint8_t *)c);
    default:
        return -1;
    }
}

/* LZ4 helpers. */
static unsigned int gw_abi_lz4_ctl(gw_lz4_op_t op, const void *a, void *b)
{
    switch (op) {
    case GW_LZ4_UNCOMPRESS:
        return lz4_uncompress(a, b);
    case GW_LZ4_GET_SIZE:
        return lz4_get_file_size(a);
    default:
        return 0;
    }
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
        case GW_MEM_AHB:        p = ahb_calloc(count, size); break;
        case GW_MEM_DTCM:       p = dtcm_calloc(count, size); break;
        case GW_MEM_DTCM_ARENA: p = dtcm_arena_calloc(count, size); break;
        case GW_MEM_AHB_ONLY: {
            size_t bytes = count * size;
            p = ahb_only_malloc(bytes);
            if (p)
                memset(p, 0, bytes);
            break;
        }
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
        case GW_MEM_AHB:
        case GW_MEM_AHB_ONLY:
            ahb_init();
            break;
        case GW_MEM_DTCM_ARENA:
            dtcm_arena_init();
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
    .fatfs_dir_ctl = gw_abi_fatfs_dir_ctl,

    /* G&W LCD */
    .lcd_ctl = gw_abi_lcd_ctl,

    /* G&W audio */
    .audio_ctl = gw_abi_audio_ctl,

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
    .odroid_input_read_gamepad = odroid_input_read_gamepad,
    .display_ctl               = gw_abi_display_ctl,

    /* retro-go: overlay / SD / settings */
    .odroid_overlay_draw_text      = odroid_overlay_draw_text,
    .odroid_sdcard_mkdir           = odroid_sdcard_mkdir,
    .odroid_settings_app_int32_get = odroid_settings_app_int32_get,
    .odroid_settings_app_int32_set = odroid_settings_app_int32_set,

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

    /* v1 append: classic-core porting surface (see gw_firmware_abi.h) */
    .strcpy  = strcpy,
    .malloc  = malloc,

    .odroid_overlay_cache_file_in_ram = odroid_overlay_cache_file_in_ram,

    /* v1 append: Mega Drive / gwenesis (DWT local in bridge; audio → audio_ctl) */
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

    /* TGB Dual (Game Boy / Game Boy Color) porting surface.
     * GW_GetUnixTM/mktime dropped — use time()/localtime().
     * lcd_clone folded into lcd_copy_fb. */
    .odroid_settings_Palette_get     = odroid_settings_Palette_get,
    .odroid_settings_Palette_set     = odroid_settings_Palette_set,

    /* v2 append: FCEUmm (NES) mappers.pak loader */
    .rg_storage_copy_file_range_to_ram =
        (size_t (*)(char *, uint8_t *, uint32_t, uint32_t, gw_file_progress_cb_t))rg_storage_copy_file_range_to_ram,

    /* v2 append: blueMSX (MSX) — sha1_ctl; ahb/audio volume already folded */
    .sha1_ctl                    = gw_abi_sha1_ctl,

    .localtime                   = localtime,
    .gettimeofday                = gw_abi_gettimeofday,
    .rg_storage_stat             = rg_storage_stat,
    .rg_storage_get_adjacent_files = rg_storage_get_adjacent_files,
    .rg_basename                 = rg_basename,

    /* v2 append: LCD-Game-Emulator (Game & Watch) */
    .GW_SetUnixTM                = GW_SetUnixTM,
    .jpeg_ctl                    = gw_abi_jpeg_ctl,
    .lzma_inflate                = lzma_inflate,
    .lz4_ctl                     = gw_abi_lz4_ctl,

    /* v2 append: Tamagotchi P1 */
    .common_emu_frame_loop_reset = common_emu_frame_loop_reset,

    /* v2 append: GBA (gpSP) */
    .get_SystemCoreClock         = gw_abi_get_SystemCoreClock,
    .odroid_overlay_cache_file_in_flash_relocate = odroid_overlay_cache_file_in_flash_relocate,
    .draw_error_screen           = draw_error_screen,

    /* v2 append: per-core option i18n */
    .i18n_lang_code              = i18n_lang_code,
};
