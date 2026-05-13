#include <assert.h>

#include "odroid_system.h"
#include "rom_manager.h"
#include "gw_linker.h"
#include "rg_rtc.h"
#include "gui.h"
#include "main.h"
#include "gw_lcd.h"
#include "gw_sleep.h"
#include "odroid_settings.h"
#include "appid.h"
#include "rg_welcome_prompt.h"
#if SD_CARD == 1
#include "gw_sdcard.h"
#include "ff.h"
#endif

static rg_app_desc_t currentApp;
static runtime_stats_t statistics;
static runtime_counters_t counters;
static uint skip;

static sleep_pre_sleep_hook_t pre_sleep_hook = NULL;

#define TURBOS_SPEED 10

bool odroid_button_turbos(void)
{
    int turbos = 1000 / TURBOS_SPEED;
    return (get_elapsed_time() % turbos) < (turbos / 2);
}

void odroid_system_panic(const char *reason, const char *function, const char *file)
{
    printf("*** PANIC: %s\n  *** FUNCTION: %s\n  *** FILE: %s\n", reason, function, file);
    assert(0);
}

void odroid_system_init(int appId, int sampleRate)
{
    currentApp.id = appId;
    currentApp.romPath = ACTIVE_FILE->path;

    odroid_settings_init();
    odroid_audio_init(sampleRate);
    odroid_display_init();

    counters.resetTime = get_elapsed_time();

    printf("%s: System ready!\n\n", __func__);
}

void odroid_system_emu_init(state_handler_t load_cb,
                            state_handler_t save_cb,
                            screenshot_handler_t screenshot_cb,
                            shutdown_handler_t shutdown_cb,
                            sleep_post_wakeup_handler_t sleep_post_wakeup_cb,
                            sram_save_handler_t sram_save_cb)
{
    // currentApp.gameId = crc32_le(0, buffer, sizeof(buffer));
    currentApp.gameId = 0;
    currentApp.handlers.loadState = load_cb;
    currentApp.handlers.saveState = save_cb;
    currentApp.handlers.screenshot = screenshot_cb;
    currentApp.handlers.shutdown = shutdown_cb;
    currentApp.handlers.sleep_post_wakeup = sleep_post_wakeup_cb;
    currentApp.handlers.sram_save = sram_save_cb;
    
    printf("%s: Init done. GameId=%08lX\n", __func__, currentApp.gameId);
}

rg_app_desc_t *odroid_system_get_app()
{
    return &currentApp;
}

static char *extract_system(const char *filename) {
    char *last_slash = strrchr(filename, '/');
    char *directory = malloc(last_slash - filename + 2);
    strncpy(directory, filename, last_slash - filename + 1);
    directory[last_slash - filename + 1] = '\0';
    return directory;
}

/* Build a path into the provided buffer. If out_buf is NULL, allocates
 * via strdup (legacy behavior). Callers that provide a buffer avoid heap. */
static void odroid_system_get_path_buf(emu_path_type_t type, const char *_romPath, char *out, int out_size)
{
    const char *fileName = _romPath ?: currentApp.romPath;

    if (strstr(fileName, ODROID_BASE_PATH_ROMS))
    {
        fileName = strstr(fileName, ODROID_BASE_PATH_ROMS);
        fileName += strlen(ODROID_BASE_PATH_ROMS);
    }

    if (!fileName || strlen(fileName) < 4)
    {
        RG_PANIC("Invalid ROM path!");
    }

    switch (type)
    {
        case ODROID_PATH_SAVE_STATE:
        case ODROID_PATH_SAVE_STATE_1:
        case ODROID_PATH_SAVE_STATE_2:
        case ODROID_PATH_SAVE_STATE_3:
            snprintf(out, out_size, "%s%s-%d.sav", ODROID_BASE_PATH_SAVES, fileName, type);
            break;
        case ODROID_PATH_SAVE_STATE_OFF:
            snprintf(out, out_size, "%s/off.sav", ODROID_BASE_PATH_SAVES);
            break;
        case ODROID_PATH_SCREENSHOT:
        case ODROID_PATH_SCREENSHOT_1:
        case ODROID_PATH_SCREENSHOT_2:
        case ODROID_PATH_SCREENSHOT_3:
            snprintf(out, out_size, "%s%s-%d.raw", ODROID_BASE_PATH_SAVES, fileName, type-ODROID_PATH_SCREENSHOT);
            break;

        case ODROID_PATH_USER_SCREENSHOT:
        {
            time_t now = time(NULL);
            struct tm *tm_info = localtime(&now);
            char tempFileName[200];
            const char *baseName = strrchr(fileName, '/');
            if (baseName) baseName++;
            else baseName = fileName;
            strncpy(tempFileName, baseName, sizeof(tempFileName) - 1);
            tempFileName[sizeof(tempFileName) - 1] = '\0';
            char *dot = strrchr(tempFileName, '.');
            if (dot) *dot = '\0';
            snprintf(out, out_size, "%s/%04d-%02d-%02d-%02d-%02d-%02d-%s.bmp",
                    ODROID_BASE_PATH_SCREENSHOTS,
                    1900 + tm_info->tm_year, tm_info->tm_mon + 1, tm_info->tm_mday,
                    tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, tempFileName);
            break;
        }

        case ODROID_PATH_SAVE_BACK:
            snprintf(out, out_size, "%s%s.sav.bak", ODROID_BASE_PATH_SAVES, fileName);
            break;

        case ODROID_PATH_SAVE_SRAM:
            snprintf(out, out_size, "%s%s.sram", ODROID_BASE_PATH_SAVES, fileName);
            break;

        case ODROID_PATH_TEMP_FILE:
            snprintf(out, out_size, "%s/%X%X.tmp", ODROID_BASE_PATH_TEMP, get_elapsed_time(), rand());
            break;

        case ODROID_PATH_ROM_FILE:
            snprintf(out, out_size, "%s%s", ODROID_BASE_PATH_ROMS, fileName);
            break;

        case ODROID_PATH_CRC_CACHE:
            snprintf(out, out_size, "%s%s.crc", ODROID_BASE_PATH_CRC_CACHE, fileName);
            break;

        case ODROID_PATH_COVER_FILE:
        {
            char tempFileName[200];
            strncpy(tempFileName, fileName, sizeof(tempFileName) - 1);
            tempFileName[sizeof(tempFileName) - 1] = '\0';
            char *dot = strrchr(tempFileName, '.');
            if (dot) *dot = '\0';
            snprintf(out, out_size, "%s%s.img", ODROID_BASE_PATH_COVERS, tempFileName);
            break;
        }

        case ODROID_PATH_CHEAT_STATE:
            /* Persist active cheat bitmask alongside savestates (writable /data). */
            snprintf(out, out_size, "%s%s.state", ODROID_BASE_PATH_SAVES, fileName);
            break;

        case ODROID_PATH_CHEAT_PCE:
        {
            char shortFileName[200];
            strncpy(shortFileName, fileName, sizeof(shortFileName) - 1);
            shortFileName[sizeof(shortFileName) - 1] = '\0';
            char *ext = strrchr(shortFileName, '.');
            if (ext) *ext = '\0';
            snprintf(out, out_size, "%s%s.pceplus", ODROID_BASE_PATH_CHEATS, shortFileName);
            break;
        }

        case ODROID_PATH_CHEAT_GAME_GENIE:
        {
            char shortFileName[200];
            strncpy(shortFileName, fileName, sizeof(shortFileName) - 1);
            shortFileName[sizeof(shortFileName) - 1] = '\0';
            char *ext = strrchr(shortFileName, '.');
            if (ext) *ext = '\0';
            snprintf(out, out_size, "%s%s.ggcodes", ODROID_BASE_PATH_CHEATS, shortFileName);
            break;
        }

        case ODROID_PATH_CHEAT_MCF:
        {
            char shortFileName[200];
            strncpy(shortFileName, fileName, sizeof(shortFileName) - 1);
            shortFileName[sizeof(shortFileName) - 1] = '\0';
            char *ext = strrchr(shortFileName, '.');
            if (ext) *ext = '\0';
            snprintf(out, out_size, "%s%s.mcf", ODROID_BASE_PATH_CHEATS, shortFileName);
            break;
        }

        case ODROID_PATH_SYSTEM_CONFIG:
        {
            char systemPath[RG_PATH_MAX];
            const char *last_slash = strrchr(fileName, '/');
            if (last_slash) {
                int len = (int)(last_slash - fileName + 1);
                if (len >= (int)sizeof(systemPath)) len = sizeof(systemPath) - 1;
                memcpy(systemPath, fileName, len);
                systemPath[len] = '\0';
            } else {
                systemPath[0] = '\0';
            }
            snprintf(out, out_size, "%s%sCONFIG", ODROID_BASE_PATH_CONFIG, systemPath);
            break;
        }

        default:
            RG_PANIC("Unknown Type");
    }
}

/* Legacy API — allocates via strdup. Use odroid_system_get_path_to_buf
 * with a caller-provided buffer to avoid heap allocation. */
char* odroid_system_get_path(emu_path_type_t type, const char *_romPath)
{
    char buffer[256];
    odroid_system_get_path_buf(type, _romPath, buffer, sizeof(buffer));
    return strdup(buffer);
}

/* Heap-free version — writes directly into caller's buffer. */
void odroid_system_get_path_to_buf(emu_path_type_t type, const char *_romPath, char *buf, int buf_size)
{
    odroid_system_get_path_buf(type, _romPath, buf, buf_size);
}

bool odroid_system_emu_screenshot(const char *filename)
{
    bool success = false;

    rg_storage_mkdir(rg_dirname(filename));

    uint8_t *data;
    size_t size = sizeof(framebuffer1);
    if (currentApp.handlers.screenshot) {
        data = (*currentApp.handlers.screenshot)();
    } else {
        // If there is no callback for screenshot, we take it from framebuffer
        // which is not the best as it will include menu in the middle
        lcd_wait_for_vblank();
        data = (unsigned char *)lcd_get_inactive_buffer();
    }

    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        return false;
    }

    size_t written = fwrite(data, 1, size, file);

    fclose(file);
    
    if (written != size) {
        return false;
    }
    success = true;

    rg_storage_commit();

    return success;
}

#define EMU_MAX_SAVE_SLOTS 4
/* Static storage for save state info — avoids calloc heap fragmentation.
 * Only one emu_get_states result is active at a time. */
static uint8_t _emu_states_buf[sizeof(rg_emu_states_t) + sizeof(rg_emu_slot_t) * EMU_MAX_SAVE_SLOTS];

rg_emu_states_t *odroid_system_emu_get_states(const char *romPath, size_t slots)
{
    if (slots > EMU_MAX_SAVE_SLOTS) slots = EMU_MAX_SAVE_SLOTS;
    rg_emu_states_t *result = (rg_emu_states_t *)_emu_states_buf;
    memset(result, 0, sizeof(_emu_states_buf));
    uint8_t last_used_slot = 0xFF;

    char pathbuf[RG_PATH_MAX];
    odroid_system_get_path_to_buf(ODROID_PATH_SAVE_STATE, romPath, pathbuf, sizeof(pathbuf));
    FILE *fp = fopen(pathbuf, "rb");
    if (fp)
    {
        fread(&last_used_slot, 1, 1, fp);
        fclose(fp);
    }

    for (size_t i = 0; i < slots; i++)
    {
        rg_emu_slot_t *slot = &result->slots[i];
        odroid_system_get_path_to_buf(ODROID_PATH_SCREENSHOT + i, romPath, slot->preview, sizeof(slot->preview));
        odroid_system_get_path_to_buf(ODROID_PATH_SAVE_STATE + i, romPath, slot->file, sizeof(slot->file));
        rg_stat_t info = rg_storage_stat(slot->file);
        slot->id = i;
        slot->is_used = info.exists;
        slot->is_lastused = false;
        slot->mtime = info.mtime;
        if (slot->is_used)
        {
            if (!result->latest || slot->mtime > result->latest->mtime)
                result->latest = slot;
            if (slot->id == last_used_slot)
                result->lastused = slot;
            result->used++;
        }
    }
    if (!result->lastused && result->latest)
        result->lastused = result->latest;
    if (result->lastused)
        result->lastused->is_lastused = true;
    result->total = slots;

    return result;
}

/* Return true on successful load.
 * Slot -1 is for the OFF_SAVESTATE
 * */
bool odroid_system_emu_load_state(int slot)
{
    if (!currentApp.romPath || !currentApp.handlers.loadState)
    {
        printf("No rom or handler defined...\n");
        return false;
    }

    char filename[RG_PATH_MAX];
    if (slot == -2) {
        printf("Loading state from sram.\n");
        return (*currentApp.handlers.loadState)(NULL);
    } else if (slot == -1) {
        odroid_system_get_path_to_buf(ODROID_PATH_SAVE_STATE_OFF, currentApp.romPath, filename, sizeof(filename));
    } else {
        odroid_system_get_path_to_buf(ODROID_PATH_SAVE_STATE + slot, currentApp.romPath, filename, sizeof(filename));
    }

    printf("Loading state from '%s'.\n", filename);
    return (*currentApp.handlers.loadState)(filename);
};

bool odroid_system_emu_save_state(int slot)
{
    if (!currentApp.romPath || !currentApp.handlers.saveState)
    {
        printf("No rom or handler defined...\n");
        return false;
    }

    char filename[RG_PATH_MAX];
    if (slot == -1) {
        odroid_system_get_path_to_buf(ODROID_PATH_SAVE_STATE_OFF, currentApp.romPath, filename, sizeof(filename));
    } else {
        odroid_system_get_path_to_buf(ODROID_PATH_SAVE_STATE + slot, currentApp.romPath, filename, sizeof(filename));
    }

    printf("Saving state to '%s'.\n", filename);

    if (!rg_storage_mkdir(rg_dirname(filename)))
    {
        printf("Unable to create dir, save might fail...\n");
    }

    bool success = (*currentApp.handlers.saveState)(filename);

    if ((success) && (slot >= 0))
    {
        char screenshot_path[RG_PATH_MAX];
        odroid_system_get_path_to_buf(ODROID_PATH_SCREENSHOT + slot, currentApp.romPath, screenshot_path, sizeof(screenshot_path));
        odroid_system_emu_screenshot(screenshot_path);
    }

    rg_storage_commit();

    return success;
};

void odroid_system_shutdown() {
    if (currentApp.handlers.shutdown) {
        (*currentApp.handlers.shutdown)();
    }
}

void odroid_system_sram_save() {
    if (currentApp.handlers.sram_save) {
        (*currentApp.handlers.sram_save)();
    }
}

IRAM_ATTR void odroid_system_tick(uint skippedFrame, uint fullFrame, uint busyTime)
{
    if (skippedFrame)
        counters.skippedFrames++;
    else if (fullFrame)
        counters.fullFrames++;
    counters.totalFrames++;

    // Because the emulator may have a different time perception, let's just skip the first report.
    if (skip)
    {
        skip = 0;
    }
    else
    {
        counters.busyTime += busyTime;
    }

    statistics.lastTickTime = get_elapsed_time();
}

void odroid_system_switch_app(int app)
{
    printf("%s: Switching to app %d.\n", __FUNCTION__, app);

    odroid_system_sram_save();

    switch (app)
    {
    case 0:
        odroid_settings_StartupFile_set(0);
        odroid_settings_commit();

#if SD_CARD == 1
        // Unmount Fs and Deinit SD Card if needed
        sdcard_deinit();
#endif

#if INTFLASH_BANK == 2
        // Retro-go is in bank2 with Tim's bootloader in bank1.
        // Jump directly to the bank2 entrypoint rather than resetting,
        // which avoids the bootloader and is significantly faster.

        void __attribute__((naked)) _start_app(void (*const pc)(void), uint32_t sp) {
            __asm("           \n\
                  msr msp, r1 /* load r1 into MSP */\n\
                  bx r0       /* branch to the address at r0 */\n\
            ");
        }

        void _boot_bank2(void) {
            /* sp/pc must live in callee-saved registers across HAL_MPU_Disable.
             * If the compiler spills them to the (caller-saved) stack, the
             * subsequent __set_MSP() switches the stack and the reload
             * reads garbage from the new stack (out-of-DTCM at 0x20020004
             * which is 0 → bx 0 → fault). Forcing r6/r7 keeps them safe
             * regardless of optimisation level / register pressure. */
            register uint32_t sp asm("r6") = *((uint32_t *)FLASH_BANK2_BASE);
            register uint32_t pc asm("r7") = *((uint32_t *)FLASH_BANK2_BASE + 1);

            HAL_MPU_Disable();
            __set_MSP(sp);
            __set_PSP(sp);
            _start_app((void (*const)(void))pc, (uint32_t)sp);
        }

        boot_magic_set(BOOT_MAGIC_EMULATOR);

        app_animate_lcd_brightness(odroid_display_get_backlight_raw(), 0, 10);

        HAL_DeInit();

        SCB_InvalidateDCache();
        SCB_InvalidateICache();

        while (1) {
            _boot_bank2();
        }
#else
        // Retro-go is in bank1 with no bootloader present.
        // Reset directly back into retro-go.

        NVIC_SystemReset();
#endif
        break;
    case 9:
        *((uint32_t *)0x2001FFF8) = 0x544F4F42;              // "BOOT"
        *((uint32_t *)0x2001FFFC) = (uint32_t)&__INTFLASH__; // vector table

        NVIC_SystemReset();
        break;
    default:
        assert(0);
    }
}

runtime_stats_t odroid_system_get_stats(bool reset_stats)
{
    float tickTime = (get_elapsed_time() - counters.resetTime);

    statistics.battery = odroid_input_read_battery();
    statistics.busyPercent = counters.busyTime / tickTime * 100.f;
    statistics.skippedFPS = counters.skippedFrames / (tickTime / 1000.f);
    statistics.totalFPS = counters.totalFrames / (tickTime / 1000.f);

    if (reset_stats) {
        skip = 1;
        counters.busyTime = 0;
        counters.totalFrames = 0;
        counters.skippedFrames = 0;
        counters.resetTime = get_elapsed_time();
    }

    return statistics;
}

void odroid_system_set_pre_sleep_hook(sleep_pre_sleep_hook_t callback)
{
    pre_sleep_hook = callback;
}

static void dbgmcu_restore_clock_after_wake(void)
{
    /* Restore debug clock after wake so JTAG works again (e.g. in Auto mode) */
    if (odroid_settings_DebugMenuDebugClockAlwaysOn_get()) {
        DBGMCU->CR = DBGMCU_CR_DBG_SLEEPCD |
                     DBGMCU_CR_DBG_STOPCD |
                     DBGMCU_CR_DBG_STANDBYCD |
                     DBGMCU_CR_DBG_TRACECKEN |
                     DBGMCU_CR_DBG_CKCDEN |
                     DBGMCU_CR_DBG_CKSRDEN;
    } else {
        DBGMCU->CR = DBGMCU_CR_DBG_SLEEPCD;
    }
}

static void odroid_system_sleep_post_wakeup_handler() {
    // Reset idle timer
    gui.idle_start = uptime_get();

    dbgmcu_restore_clock_after_wake();

    if (currentApp.handlers.sleep_post_wakeup) {
        (*currentApp.handlers.sleep_post_wakeup)();
    }

    if (currentApp.id == APPID_LAUNCHER) {
        rg_welcome_prompt_maybe_auto_show_on_launcher();
    }
}

static void odroid_system_sleep_internal(system_sleep_flags_t flags, sleep_pre_wakeup_callback_t pre_wakeup_callback)
{
    if (flags & (SLEEP_ENTER_SLEEP | SLEEP_ENTER_STANDBY)) {
        odroid_system_sram_save();
    }

    if (pre_sleep_hook != NULL)
    {
        pre_sleep_hook();
    }

    // Only persist startup file when sleeping from inside an emulator.
    // If sleeping from the launcher, clear it so wakeup returns to the
    // launcher instead of jumping to bank2 via switch_app.
    if (currentApp.id != APPID_LAUNCHER) {
        odroid_settings_StartupFile_set(ACTIVE_FILE);
    } else {
        odroid_settings_StartupFile_set(NULL);
    }
    odroid_settings_commit();

    if (flags & SLEEP_SHOW_ANIMATION) {
        app_sleep_transition((flags & SLEEP_SHOW_LOGO) != 0, (flags & SLEEP_ANIMATION_SLOW) != 0);
    }

    gui_save_current_tab();

    if (flags & SLEEP_ENTER_SLEEP) {
        GW_EnterDeepSleep(false, pre_wakeup_callback, &odroid_system_sleep_post_wakeup_handler);
    } else if (flags & SLEEP_ENTER_STANDBY) {
        GW_EnterDeepSleep(true, NULL, NULL);
    }
}

void odroid_system_sleep_ex(system_sleep_flags_t flags, sleep_pre_wakeup_callback_t pre_wakeup_callback)
{
    odroid_system_sleep_internal(flags, pre_wakeup_callback);
}

void odroid_system_sleep(void)
{
    odroid_system_sleep_internal(SLEEP_ENTER_SLEEP_WITH_ANIMATION, NULL);
}
