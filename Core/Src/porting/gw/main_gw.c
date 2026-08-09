/* This core is built standalone (see cores/gw/) and talks to the
 * firmware only through gw_firmware_abi_t — see Core/Src/porting/
 * core_common/. gw_core_bridge.h must come after the normal firmware
 * headers below so their `extern` declarations of common_emu_state/
 * ACTIVE_FILE/ram_start are parsed first. */
#include <odroid_system.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <time.h>

#include "main.h"
#include "gw_lcd.h"
#include "gw_buttons.h"
#include "appid.h"
#include "common.h"
#include "rom_manager.h"
#include "rg_rtc.h"
#include "gw_malloc.h"

/* G&W system support */
#include "gw_system.h"
#include "gw_romloader.h"

/* access to internals for debug purpose */
#include "sm510.h"

#include "gw_core_bridge.h"
#include "gw_i18n.h"

/* From rg_i18n.h — avoid pulling the full i18n table into the core. */
#define ODROID_DIALOG_CHOICE_SEPARATOR {0x0F0F0F0E, "-", "-", -1, NULL}

/* Uncomment to enable debug menu in overlay */
//#define GW_EMU_DEBUG_OVERLAY

#define ODROID_APPID_GW 6

const uint8_t *gw_rom_image = NULL;
unsigned gw_rom_image_size = 0;

/* Debug overlay text colours (i18n curr_colors not exposed over the ABI). */
#define GW_DBG_FG 0xFFFF
#define GW_DBG_BG 0x0000

/* keys inpus (hw & sw) */
static odroid_gamepad_state_t joystick;
static bool softkey_time_pressed = 0;
static bool softkey_alarm_pressed = 0;
static bool softkey_A_pressed = 0;
static bool softkey_only = 0;

static unsigned int softkey_duration = 0;

static void gw_set_time() {

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    gw_time_t emu_time;
    emu_time.hours = tm->tm_hour;
    emu_time.minutes = tm->tm_min;
    emu_time.seconds = tm->tm_sec;

    // set time of the emulated system
    gw_system_set_time(emu_time);
    printf("Set time done!\n");
}

static void gw_get_time() {

    gw_time_t emu_time = {0};

    // check if the system is able to get the time
    emu_time = gw_system_get_time();
    if (emu_time.hours > 24) return;

    // Set times (read "now" via time()/localtime, write via GW_SetUnixTM —
    // there is no portable libc setter wired into the ABI).
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    tm.tm_hour = emu_time.hours;
    tm.tm_min = emu_time.minutes;
    tm.tm_sec = emu_time.seconds;
    GW_SetUnixTM(&tm);
}

static void gw_check_time() {

    static unsigned int is_gw_time_sync=0;

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    // Set times
    gw_time_t emu_time;
    emu_time.hours = tm->tm_hour;
    emu_time.minutes = tm->tm_min;
    emu_time.seconds = tm->tm_sec;

    // update time every 30s
    if ( (emu_time.seconds == 30) || (is_gw_time_sync==0) ) {
        is_gw_time_sync = 1;
        gw_system_set_time(emu_time);
    }
}
static unsigned char state_save_buffer[sizeof(gw_state_t)];

static bool gw_system_SaveState(const char *savePathName)
{
    memset(state_save_buffer, 0x00, sizeof(state_save_buffer));
    gw_state_save(state_save_buffer);

    FILE *file = fopen(savePathName, "wb");
    if (file == NULL) {
        return false;
    }

    size_t written = fwrite(state_save_buffer, sizeof(state_save_buffer), 1, file);

    fclose(file);

    if (!written) {
        return false;
    }

    return true;
}

static bool gw_system_LoadState(const char *savePathName)
{
    FILE *file = fopen(savePathName, "rb");
    if (file == NULL) {
        printf("failed open %s\n",savePathName);
        while(1);
        return false;
    }

    size_t read = fread(state_save_buffer, sizeof(state_save_buffer), 1, file);

    fclose(file);

    if (!read) {
        return false;
    }

    return gw_state_load((unsigned char *)state_save_buffer);
}

static void *gw_system_Screenshot()
{
    lcd_wait_for_vblank();

    lcd_clear_active_buffer();
    gw_system_blit(lcd_get_active_buffer());
    return lcd_get_active_buffer();
}

/* callback to get buttons state */
unsigned int gw_get_buttons()
{
    unsigned int hw_buttons = 0;
    if (!softkey_only)
    {
        hw_buttons |= joystick.values[ODROID_INPUT_LEFT];
        hw_buttons |= joystick.values[ODROID_INPUT_UP] << 1;
        hw_buttons |= joystick.values[ODROID_INPUT_RIGHT] << 2;
        hw_buttons |= joystick.values[ODROID_INPUT_DOWN] << 3;
        hw_buttons |= joystick.values[ODROID_INPUT_A] << 4;
        hw_buttons |= joystick.values[ODROID_INPUT_B] << 5;
        hw_buttons |= joystick.values[ODROID_INPUT_SELECT] << 6;
        hw_buttons |= joystick.values[ODROID_INPUT_START] << 7;
        hw_buttons |= joystick.values[ODROID_INPUT_VOLUME] << 8;
        hw_buttons |= joystick.values[ODROID_INPUT_POWER] << 9;
        hw_buttons |= joystick.values[ODROID_INPUT_X] << 10;
        hw_buttons |= joystick.values[ODROID_INPUT_Y] << 11;
    }

    // software keys
    hw_buttons |= ((unsigned int)softkey_A_pressed) << 4;
    hw_buttons |= ((unsigned int)softkey_time_pressed) << 10;
    hw_buttons |= ((unsigned int)softkey_alarm_pressed) << 11;

    return hw_buttons;
}

static void gw_sound_init()
{
    /* init emulator sound system with shared audio buffer */
    gw_system_sound_init();

    /* Start playing */
    audio_start_playing(GW_AUDIO_BUFFER_LENGTH);
}

static void gw_sound_submit()
{

    /** Enables the following code to track audio rendering issues **/
    /*
    if (gw_audio_buffer_idx < GW_AUDIO_BUFFER_LENGTH) {
        printf("audio underflow:%u < %u \n",gw_audio_buffer_idx , GW_AUDIO_BUFFER_LENGTH);
        assert(0);
    }

    if (gw_audio_buffer_idx > (GW_AUDIO_BUFFER_LENGTH +12) ) {
        printf("audio oveflow:%u < %u \n",gw_audio_buffer_idx , GW_AUDIO_BUFFER_LENGTH);
        assert(0);
    }
    */

    if (common_emu_sound_loop_is_muted()) {
        return;
    }

    int16_t factor = common_emu_sound_get_volume();
    int16_t* sound_buffer = audio_get_active_buffer();
    uint16_t sound_buffer_length = audio_get_buffer_length();

    // Write to sound buffer and lower the volume accordingly
    for (int i = 0; i < sound_buffer_length; i++)
    {
        sound_buffer[i] = (factor) * (gw_audio_buffer[i] << 4);
    }

    gw_audio_buffer_copied = true;
}

/************************ Debug function in overlay START *******************************/

/* performance monitoring */
/* Emulator loop monitoring
    ( unit is 1/systemcoreclock 1/280MHz )
    loop_cycles
        -measured duration of the loop.
    proc_cycles
        - estimated duration of emulated CPU for a bunch of emulated system clock.
    blit_cycles
        - estimated duration of graphics rendering.
    end_cycles
        - estimated duration of overall processing.
    */

static unsigned int loop_cycles = 1, end_cycles = 1, proc_cycles = 1, blit_cycles = 1;

static void gw_debug_bar()
{

#ifdef GW_EMU_DEBUG_OVERLAY
    static unsigned int loop_duration_us = 1, end_duration_us = 1, proc_duration_us = 1, blit_duration_us = 1;
    static const unsigned int SYSTEM_CORE_CLOCK_MHZ = 280;

    static bool debug_init_done = false;

    if (!debug_init_done)
    {
        common_emu_enable_dwt_cycles();
        debug_init_done = true;
    }

    static unsigned int overflow_count = 0;
    static unsigned int busy_percent = 0;

    char debugMsg[120];

    proc_duration_us = proc_cycles / SYSTEM_CORE_CLOCK_MHZ;
    blit_duration_us = blit_cycles / SYSTEM_CORE_CLOCK_MHZ;
    end_duration_us = end_cycles / SYSTEM_CORE_CLOCK_MHZ;
    loop_duration_us = loop_cycles / SYSTEM_CORE_CLOCK_MHZ;

    busy_percent = 100 * (proc_duration_us + blit_duration_us) / loop_duration_us;

    if (end_duration_us > 1000000 / GW_REFRESH_RATE)
        overflow_count++;

    if (m_halt != 0)
        sprintf(debugMsg, "%04dus EMU:%04dus FX:%04dus %d%%+%d HALT", loop_duration_us, proc_duration_us, blit_duration_us, busy_percent, overflow_count);
    else
        sprintf(debugMsg, "%04dus EMU:%04dus FX:%04dus %d%%+%d", loop_duration_us, proc_duration_us, blit_duration_us, busy_percent, overflow_count);

    odroid_overlay_draw_text(0, 0, GW_SCREEN_WIDTH, debugMsg, GW_DBG_FG, GW_DBG_BG);

#endif
}
/************************ Debug function in overlay END ********************************/

/************************ G&W options Menu ********************************/
// Press Auto Clear ACL
// Auto Set Time
// Press TIME
// Press ALARM

static bool gw_debug_submenu_autoclear(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_ENTER)
        gw_system_reset();

    return event == ODROID_DIALOG_ENTER;
}

static bool gw_debug_submenu_autoset_time(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{

    if (event == ODROID_DIALOG_ENTER)
    {
        gw_set_time();
    }

    return event == ODROID_DIALOG_ENTER;
}

static bool gw_debug_submenu_autoget_time(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{

    if (event == ODROID_DIALOG_ENTER)
    {
        gw_get_time();
    }

    return event == ODROID_DIALOG_ENTER;
}

static bool gw_debug_submenu_press_time(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_ENTER)
    {
        softkey_time_pressed = 1;
        softkey_duration = GW_REFRESH_RATE;
    }
    return event == ODROID_DIALOG_ENTER;
}

static bool gw_debug_submenu_press_alarm(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_ENTER)
    {
        softkey_alarm_pressed = 1;
        softkey_duration = GW_REFRESH_RATE;
    }
    return event == ODROID_DIALOG_ENTER;
}


static char LCD_deflicker_value[16];
static bool gw_debug_submenu_set_deflicker(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    /* LCD deflicker filter level */
    /*
    0 : filter is disabled
    1 : refreshed on keys polling and call subroutine return
    2 : refreshed on keys polling only
    */
    unsigned int max_flag_lcd_deflicker_level = 2;

    if (event == ODROID_DIALOG_PREV)
        flag_lcd_deflicker_level = flag_lcd_deflicker_level > 0 ? flag_lcd_deflicker_level - 1 : max_flag_lcd_deflicker_level;

    if (event == ODROID_DIALOG_NEXT)
        flag_lcd_deflicker_level = flag_lcd_deflicker_level < max_flag_lcd_deflicker_level ? flag_lcd_deflicker_level + 1 : 0;

    if (flag_lcd_deflicker_level == 0) strcpy(option->value, gw_i18n(gw_i18n_filter_none));
    if (flag_lcd_deflicker_level == 1) strcpy(option->value, gw_i18n(gw_i18n_filter_medium));
    if (flag_lcd_deflicker_level == 2) strcpy(option->value, gw_i18n(gw_i18n_filter_high));

    return event == ODROID_DIALOG_ENTER;
}

// Debug menu strings

static char display_ram_value[16];

// Display RAM bool
static unsigned int debug_display_ram = 0;
static bool gw_debug_submenu_display_ram(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT)
        debug_display_ram = debug_display_ram == 0 ? 1 : 0;

    if (debug_display_ram == 0) strcpy(option->value, gw_i18n(gw_i18n_no));
    if (debug_display_ram == 1) strcpy(option->value, gw_i18n(gw_i18n_yes));

    return event == ODROID_DIALOG_ENTER;
}

static char draw_line_content[1+2*17];

static void gw_display_ram_overlay(){

  //  char *p;
   // p = (char *)&draw_line_content[0];
    sprintf(draw_line_content, "   0 1 2 3 4 5 6 7 8 9 A B C D E F");
    odroid_overlay_draw_text(10, 72, 300, draw_line_content, GW_DBG_FG, GW_DBG_BG);

    for (unsigned char i=0;i<8;i++) {
        sprintf(draw_line_content, "%2u%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x%2x",i, \
        gw_ram[i*16], gw_ram[(i*16)+1], gw_ram[(i*16)+2],gw_ram[(i*16)+3],gw_ram[(i*16)+4],gw_ram[(i*16)+5],gw_ram[(i*16)+6],gw_ram[(i*16)+7], \
        gw_ram[(i*16)+8], gw_ram[(i*16)+9], gw_ram[(i*16)+10],gw_ram[(i*16)+11],gw_ram[(i*16)+12],gw_ram[(i*16)+13],gw_ram[(i*16)+14],gw_ram[(i*16)+15]);
    odroid_overlay_draw_text(10, 80+8*i, 300, draw_line_content, GW_DBG_FG, GW_DBG_BG);
    }
}


/* Main — 3-arg signature matches run_dynamic_core / gw_core_entry.S
 * (the classic 2-arg form silently took start_paused as save_slot). */
void app_main_gw(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    odroid_dialog_choice_t options[] = {
        ODROID_DIALOG_CHOICE_SEPARATOR,
        {309, gw_i18n(gw_i18n_press_acl), "", 1, &gw_debug_submenu_autoclear},
        {310, gw_i18n(gw_i18n_press_time), "", 1, &gw_debug_submenu_press_time},
        {320, gw_i18n(gw_i18n_press_alarm), "", 1, &gw_debug_submenu_press_alarm},
        {330, gw_i18n(gw_i18n_copy_rtc_to_gw), "", 1, &gw_debug_submenu_autoset_time},
        {331, gw_i18n(gw_i18n_copy_gw_to_rtc), "", 1, &gw_debug_submenu_autoget_time},
        {360, gw_i18n(gw_i18n_lcd_filter), LCD_deflicker_value, 1, &gw_debug_submenu_set_deflicker},
        {370, gw_i18n(gw_i18n_display_ram), display_ram_value, 1, &gw_debug_submenu_display_ram},
        ODROID_DIALOG_CHOICE_LAST};

    odroid_system_init(ODROID_APPID_GW, GW_AUDIO_FREQ);
    odroid_system_emu_init(&gw_system_LoadState, &gw_system_SaveState, &gw_system_Screenshot, NULL, NULL, NULL, NULL);

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }

    common_emu_state.frame_time_10us = (uint16_t)(100000 / GW_REFRESH_RATE + 0.5f);

    /* Prefer RAM when the file fits; otherwise map into the flash cache. */
    {
        uint32_t size = 0;
        FILE *f = fopen(ACTIVE_FILE->path, "rb");
        if (f != NULL) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fclose(f);
            if (sz > 0)
                size = (uint32_t)sz;
        }
        if (size == 0)
            odroid_system_switch_app(0);

        if (size > ram_get_free_size()) {
            gw_rom_image = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &size, false);
        } else {
            uint8_t *dst = ram_malloc(size);
            if (dst != NULL && odroid_overlay_cache_file_in_ram(ACTIVE_FILE->path, dst) == size)
                gw_rom_image = dst;
        }
        if (gw_rom_image == NULL)
            odroid_system_switch_app(0);
        gw_rom_image_size = size;
    }

    /*** load ROM  */
    bool rom_status = gw_system_romload();

    if (!rom_status)
        odroid_system_switch_app(0);

    /*** Clear audio buffer */
    gw_sound_init();
    printf("Sound initialized\n");

    /* clear soft keys */
    softkey_time_pressed = 0;
    softkey_alarm_pressed = 0;
    softkey_duration = 0;

    /*** Configure the emulated system (sets device_run/start/reset/blit).
     * Ignoring a false return leaves those function pointers NULL and the
     * first gw_system_run() hardfaults with PC=0 (Thumb LR in gw_system_run). */
    if (!gw_system_config()) {
        printf("G&W: unsupported CPU '%s'\n", gw_head.cpu_name);
        odroid_system_switch_app(0);
    }
    printf("G&W configured (cpu=%s)\n", gw_head.cpu_name);

    /*** Start and Reset the emulated system */
    gw_system_start();
    printf("G&W start\n");

    gw_system_reset();
    printf("G&W reset\n");

    /* check if we have to load state */
    bool LoadState_done = false;
    if (load_state != 0) {
        LoadState_done = odroid_system_emu_load_state(save_slot);

        if (LoadState_done) {
            gw_check_time();
            gw_set_time();
        }
    }

    /* emulate watch mode */
    if (!LoadState_done) {
        softkey_time_pressed = 0;
        softkey_alarm_pressed = 0;
        softkey_A_pressed = 0;

        // disable user keys
        softkey_only = 1;

        printf("G&W emulate watch mode\n");

        gw_system_reset();

        // From reset state : run
        gw_system_run(GW_AUDIO_FREQ*2);

        // press TIME to exit TIME settings mode
        softkey_time_pressed = 1;
        gw_system_run(GW_AUDIO_FREQ/2);
        softkey_time_pressed = 0;
        gw_system_run(GW_AUDIO_FREQ*2);

        // synchronize G&W with RTC and run
        gw_check_time();
        gw_set_time();
        gw_system_run(GW_AUDIO_FREQ);

        // press A required by some game
        softkey_A_pressed = 1;
        gw_system_run(GW_AUDIO_FREQ/2);
        softkey_A_pressed = 0;
        gw_system_run(GW_AUDIO_FREQ);

        // enable user keys
        softkey_only = 0;
    }

    /*** Main emulator loop */
    printf("Main emulator loop start\n");

    static unsigned previous_m_halt = 2;

    while (true)
    {
        /* clear DWT counter used to monitor performances */
        common_emu_clear_dwt_cycles();

        wdog_refresh();

        /* refresh internal G&W timer on emulated CPU state transition */
        if (previous_m_halt != m_halt) gw_check_time();

        previous_m_halt = m_halt;

        //hardware keys
        odroid_input_read_gamepad(&joystick);

        //soft keys emulation
        if (softkey_duration > 0)
            softkey_duration--;

        if (softkey_duration == 0)
        {
            softkey_time_pressed = 0;
            softkey_alarm_pressed = 0;
        }

        void _blit()
        {
            gw_system_blit(lcd_get_active_buffer());
            common_ingame_overlay();
        }

        common_emu_input_loop(&joystick, options, &_blit);

        bool drawFrame = common_emu_frame_loop();

        /* Emulate and Blit */
        // Call the emulator function with number of clock cycles
        // to execute on the emulated device
        gw_system_run(GW_SYSTEM_CYCLES);

        /* get how many cycles have been spent in the emulator */
        proc_cycles = common_emu_get_dwt_cycles();

        /* update the screen only if there is no pending frame to render */
        if (!lcd_is_swap_pending() && drawFrame)
        {
            _blit();
            gw_debug_bar();
            if(debug_display_ram == 1) gw_display_ram_overlay();
            lcd_swap();

            /* get how many cycles have been spent in graphics rendering */
            blit_cycles = common_emu_get_dwt_cycles() - proc_cycles;
        }
        /****************************************************************************/

        /* copy audio samples for DMA */
        if (drawFrame)
        {
            gw_sound_submit();
        }
        /* get how many cycles have been spent to process everything */
        end_cycles = common_emu_get_dwt_cycles();

#ifdef GW_EMU_DEBUG_OVERLAY
        common_emu_sound_sync(true);
#else
        common_emu_sound_sync(false);
#endif
        /* get how cycles have been spent inside this loop */
        loop_cycles = common_emu_get_dwt_cycles();

    } // end of loop
}
