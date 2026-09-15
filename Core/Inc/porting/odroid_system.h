#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#if !defined(ESP_IDF_VERSION_MAJOR)

// #include "shared.h"
#include "porting.h"

#else

#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 4
#include <esp32/rom/crc.h>
#else
#include <rom/crc.h>
#endif
#endif

#include "config.h"

#include "odroid_audio.h"
#include "odroid_display.h"
#include "odroid_input.h"
#include "odroid_netplay.h"
#include "odroid_overlay.h"
#include "odroid_profiler.h"
#include "odroid_sdcard.h"
#include "odroid_settings.h"
#include "rg_storage.h"
#include "rg_utils.h"

typedef bool (*state_handler_t)(const char *filename);
typedef void *(*screenshot_handler_t)(void);
typedef void (*shutdown_handler_t)(void);
typedef void (*sleep_post_wakeup_handler_t)();
typedef void (*sram_save_handler_t)(void);
typedef void (*cheat_update_handler_t)(void);

typedef void (*sleep_pre_sleep_hook_t)();
typedef void (*sleep_pre_wakeup_callback_t)();

enum
{
    SPEEDUP_MIN = -3,
    SPEEDUP_0_5x = -2,
    SPEEDUP_0_75x = -1,
    SPEEDUP_1x = 0,
    SPEEDUP_1_25x,
    SPEEDUP_1_5x,
    SPEEDUP_2x,
    SPEEDUP_3x,
    SPEEDUP_MAX,
};
typedef int32_t emu_speedup_t;

typedef struct
{
    state_handler_t loadState;
    state_handler_t saveState;
    screenshot_handler_t screenshot;
    shutdown_handler_t shutdown;
    sleep_post_wakeup_handler_t sleep_post_wakeup;
    sram_save_handler_t sram_save;
    /* Optional: apply currently-enabled cheats to the running core.
     * Non-NULL ⇒ pause menu shows the Cheat Codes entry. */
    cheat_update_handler_t cheat_update;
} handlers_t;

typedef struct
{
     uint32_t id;
     uint32_t gameId;
     const char *romPath;
     handlers_t handlers;
     emu_speedup_t speedupEnabled;
     int32_t startAction;
} rg_app_desc_t;

typedef enum
{
     ODROID_PATH_SAVE_STATE = 0,
     ODROID_PATH_SAVE_STATE_1,
     ODROID_PATH_SAVE_STATE_2,
     ODROID_PATH_SAVE_STATE_3,
     ODROID_PATH_SAVE_STATE_OFF,
     ODROID_PATH_SCREENSHOT,  // Screenshots for savestates
     ODROID_PATH_SCREENSHOT_1,
     ODROID_PATH_SCREENSHOT_2,
     ODROID_PATH_SCREENSHOT_3,
     ODROID_PATH_USER_SCREENSHOT,
     ODROID_PATH_SAVE_BACK,
     ODROID_PATH_SAVE_SRAM,
     ODROID_PATH_TEMP_FILE,
     ODROID_PATH_ROM_FILE,
     ODROID_PATH_COVER_FILE,
     ODROID_PATH_CRC_CACHE,
     ODROID_PATH_CHEAT_STATE,
     ODROID_PATH_SYSTEM_CONFIG,
} emu_path_type_t;

typedef enum
{
     SPI_LOCK_ANY     = 0,
     SPI_LOCK_SDCARD  = 1,
     SPI_LOCK_DISPLAY = 2,
} spi_lock_res_t;

typedef struct {
    short nsamples;
    short count;
    float avg;
    float last;
} avgr_t;

typedef struct
{
     uint totalFrames;
     uint skippedFrames;
     uint fullFrames;
     uint busyTime;
     uint realTime;
     uint resetTime;
} runtime_counters_t;

typedef struct
{
     odroid_battery_state_t battery;
     float partialFPS;
     float skippedFPS;
     float totalFPS;
     float emulatedSpeed;
     float busyPercent;
     uint lastTickTime;
     uint freeMemoryInt;
     uint freeMemoryExt;
     uint freeBlockInt;
     uint freeBlockExt;
     uint idleTimeCPU0;
     uint idleTimeCPU1;
} runtime_stats_t;

typedef struct
{
     uint magicWord;
     uint errorCode;
     char message[128];
     char function[128];
     char file[128];
     uint backtrace[32];
} panic_trace_t;

typedef struct
{
    uint8_t id;
    bool is_used;
    bool is_lastused;
    size_t size;
    time_t mtime;
    char preview[RG_PATH_MAX];
    char file[RG_PATH_MAX];
} rg_emu_slot_t;

typedef struct
{
    size_t total;
    size_t used;
    rg_emu_slot_t *lastused;
    rg_emu_slot_t *latest;
    rg_emu_slot_t slots[];
} rg_emu_states_t;

typedef enum
{
     SLEEP_SHOW_ANIMATION = 1 << 0,
     SLEEP_SHOW_LOGO = 1 << 1,
     SLEEP_ENTER_SLEEP = 1 << 2,
     SLEEP_ENTER_STANDBY = 1 << 3,
     SLEEP_ANIMATION_SLOW = 1 << 4,
     SLEEP_ENTER_SLEEP_WITH_ANIMATION = SLEEP_ENTER_SLEEP | SLEEP_SHOW_ANIMATION
} system_sleep_flags_t;

#define PANIC_TRACE_MAGIC 0x12345678

void odroid_system_init(int app_id, int sampleRate);
char* odroid_system_get_path(emu_path_type_t type, const char *romPath);
/* Build /cheats/<rom-relative-stem>.<cheat_ext>. cheat_ext has no leading '.'. */
void odroid_system_get_cheat_path_to_buf(const char *romPath, const char *cheat_ext,
                                         char *buf, int buf_size);
void odroid_system_get_save_path(char *path, size_t size, int slot);
void odroid_system_get_gnw_data_path(char *path, size_t size, int slot);
void odroid_system_get_sram_path(char *path, size_t size, int slot);
void odroid_system_emu_init(state_handler_t load_cb, state_handler_t save_cb, screenshot_handler_t screenshot_cb, shutdown_handler_t shutdown_cb, sleep_post_wakeup_handler_t sleep_post_wakeup_cb, sram_save_handler_t sram_save_cb, cheat_update_handler_t cheat_update_cb);
bool odroid_system_screenshot(const char *filename, int width, int height);
bool odroid_system_emu_save_state(int slot);
bool odroid_system_emu_load_state(int slot);
void odroid_system_shutdown();
void odroid_system_sram_save();
void odroid_system_panic_dialog(const char *reason);
void odroid_system_panic(const char *reason, const char *file, const char *function) __attribute__((noreturn));
void odroid_system_halt() __attribute__((noreturn));
void odroid_system_set_pre_sleep_hook(sleep_pre_sleep_hook_t callback);
void odroid_system_sleep();
void odroid_system_sleep_ex(system_sleep_flags_t flags, sleep_pre_wakeup_callback_t pre_wakeup_callback);
void odroid_system_switch_app(int app) __attribute__((noreturn));
void odroid_system_reload_app() __attribute__((noreturn));
void odroid_system_set_boot_app(int slot);
void odroid_system_set_led(int value);
void odroid_system_tick(uint skippedFrame, uint fullFrame, uint busyTime);
rg_emu_states_t *odroid_system_emu_get_states(const char *romPath, size_t slots);

rg_app_desc_t* odroid_system_get_app();
runtime_stats_t odroid_system_get_stats(bool reset_stats);

void odroid_system_spi_lock_acquire(spi_lock_res_t);
void odroid_system_spi_lock_release(spi_lock_res_t);

/* helpers */

static inline uint get_frame_time(uint refresh_rate)
{
     // return (CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ * 1000000) / refresh_rate
     return (uint)(1000 / refresh_rate + 0.5f);
}

static inline uint get_elapsed_time()
{
     // uint now = xthal_get_ccount();
     return (uint)HAL_GetTick(); // uint is plenty resolution for us
}

static inline uint get_elapsed_time_since(uint start)
{
     // uint now = get_elapsed_time();
     // return ((now > start) ? now - start : ((uint64_t)now + (uint64_t)0xffffffff) - start);
     return get_elapsed_time() - start;
}

#undef MIN
#define MIN(a,b) ({__typeof__(a) _a = (a); __typeof__(b) _b = (b);_a < _b ? _a : _b; })
#undef MAX
#define MAX(a,b) ({__typeof__(a) _a = (a); __typeof__(b) _b = (b);_a > _b ? _a : _b; })

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif

#define RG_PANIC(x) odroid_system_panic(x, __FUNCTION__, __FILE__)


#define MALLOC_CAP_EXEC             (1<<0)  ///< Memory must be able to run executable code
#define MALLOC_CAP_32BIT            (1<<1)  ///< Memory must allow for aligned 32-bit data accesses
#define MALLOC_CAP_8BIT             (1<<2)  ///< Memory must allow for 8/16/...-bit data accesses
#define MALLOC_CAP_DMA              (1<<3)  ///< Memory must be able to accessed by DMA
#define MALLOC_CAP_PID2             (1<<4)  ///< Memory must be mapped to PID2 memory space (PIDs are not currently used)
#define MALLOC_CAP_PID3             (1<<5)  ///< Memory must be mapped to PID3 memory space (PIDs are not currently used)
#define MALLOC_CAP_PID4             (1<<6)  ///< Memory must be mapped to PID4 memory space (PIDs are not currently used)
#define MALLOC_CAP_PID5             (1<<7)  ///< Memory must be mapped to PID5 memory space (PIDs are not currently used)
#define MALLOC_CAP_PID6             (1<<8)  ///< Memory must be mapped to PID6 memory space (PIDs are not currently used)
#define MALLOC_CAP_PID7             (1<<9)  ///< Memory must be mapped to PID7 memory space (PIDs are not currently used)
#define MALLOC_CAP_SPIRAM           (1<<10) ///< Memory must be in SPI RAM
#define MALLOC_CAP_INTERNAL         (1<<11) ///< Memory must be internal; specifically it should not disappear when flash/spiram cache is switched off
#define MALLOC_CAP_DEFAULT          (1<<12) ///< Memory can be returned in a non-capability-specific memory allocation (e.g. malloc(), calloc()) call
#define MALLOC_CAP_INVALID          (1<<31) ///< Memory can't be used / list end marker


#define MEM_ANY   0
#define MEM_SLOW  MALLOC_CAP_SPIRAM
#define MEM_FAST  MALLOC_CAP_INTERNAL
#define MEM_DMA   MALLOC_CAP_DMA
#define MEM_8BIT  MALLOC_CAP_8BIT
#define MEM_32BIT MALLOC_CAP_32BIT
// #define rg_alloc(...)  rg_alloc_(..., __FILE__, __FUNCTION__)

void *rg_alloc(size_t size, uint32_t caps);
void *rg_calloc(size_t nmemb, size_t size);
void rg_free(void *ptr);
void *rg_realloc(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif
