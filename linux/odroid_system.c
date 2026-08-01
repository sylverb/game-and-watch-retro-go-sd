#include "odroid_system.h"

static panic_trace_t *panicTrace = (void *)0x0;

static rg_app_desc_t currentApp;
static runtime_stats_t statistics;
static runtime_counters_t counters;

void odroid_system_panic(const char *reason, const char *function, const char *file)
{
    printf("*** PANIC: %s\n  *** FUNCTION: %s\n  *** FILE: %s\n", reason, function, file);

    strcpy(panicTrace->message, reason);
    strcpy(panicTrace->file, file);
    strcpy(panicTrace->function, function);

    panicTrace->magicWord = PANIC_TRACE_MAGIC;

    while(1) {
        ;
    }
}

void odroid_system_init(int appId, int sampleRate)
{
    printf("%s: System ready!\n\n", __func__);
}

void odroid_system_emu_init(state_handler_t load_cb,
                            state_handler_t save_cb,
                            screenshot_handler_t screenshot_cb,
                            shutdown_handler_t shutdown_cb,
                            sleep_post_wakeup_handler_t sleep_post_wakeup_cb,
                            sram_save_handler_t sram_save_cb,
                            cheat_update_handler_t cheat_update_cb)
{
    // currentApp.gameId = crc32_le(0, buffer, sizeof(buffer));
    currentApp.gameId = 0;
    currentApp.handlers.loadState = load_cb;
    currentApp.handlers.saveState = save_cb;
    currentApp.handlers.screenshot = screenshot_cb;
    currentApp.handlers.shutdown = shutdown_cb;
    currentApp.handlers.sleep_post_wakeup = sleep_post_wakeup_cb;
    currentApp.handlers.sram_save = sram_save_cb;
    currentApp.handlers.cheat_update = cheat_update_cb;
    printf("%s: Init done. GameId=%08X\n", __func__, currentApp.gameId);
}

rg_app_desc_t *odroid_system_get_app()
{
    return &currentApp;
}


bool odroid_system_emu_load_state(int slot)
{
    return true;
}

IRAM_ATTR void odroid_system_tick(uint skippedFrame, uint fullFrame, uint busyTime)
{
    if (skippedFrame) counters.skippedFrames++;
    else if (fullFrame) counters.fullFrames++;
    counters.totalFrames++;
    counters.busyTime += busyTime;

    statistics.lastTickTime = get_elapsed_time();
}
