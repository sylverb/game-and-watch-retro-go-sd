#include "common.h"
#include <odroid_system.h>
#include <odroid_overlay.h>

#include <string.h>
#include "main.h"
#include "bitmaps.h"
#include "gw_buttons.h"
#include "gw_lcd.h"
#include "gw_audio.h"
#include "gw_linker.h"
#include "odroid_audio.h"
#include "rg_i18n.h"
#include "gw_malloc.h"
#include "gui.h"

static void set_ingame_overlay(ingame_overlay_t type);

/* Per-system automatic CPU boost. A core that needs more headroom than stock
 * 280MHz calls SystemClock_Config(level) once at app start (see MSX / PCE-CD /
 * GBA). Levels 0/1/2 match the launcher menu; level 3 is core-private
 * (~353 MHz) and must not be exposed in settings. NOT persisted: leaving an
 * emulator resets the system, restoring the user's configured clock.
 * SystemClock_Config itself refuses OC on OSPI1 SD hardware.
 */
uint8_t odroid_settings_cpu_oc_level_get(void);

cpumon_stats_t cpumon_stats = {0};

const uint8_t volume_tbl[ODROID_AUDIO_VOLUME_MAX + 1] = {
    (uint8_t)(UINT8_MAX * 0.00f),
    (uint8_t)(UINT8_MAX * 0.015f),
    (uint8_t)(UINT8_MAX * 0.03f),
    (uint8_t)(UINT8_MAX * 0.06f),
    (uint8_t)(UINT8_MAX * 0.125f),
    (uint8_t)(UINT8_MAX * 0.187f),
    (uint8_t)(UINT8_MAX * 0.25f),
    (uint8_t)(UINT8_MAX * 0.42f),
    (uint8_t)(UINT8_MAX * 0.80f),
    (uint8_t)(UINT8_MAX * 1.00f),
};

bool odroid_netplay_quick_start(void)
{
    return true;
}

// TODO: Move to own file
void odroid_audio_mute(bool mute)
{
    if (mute) {
        audio_clear_buffers();
    }

    audio_mute = mute;
}

common_emu_state_t common_emu_state = {
    .frame_time_10us = (uint16_t)(100000 / 60 + 0.5f),  // Reasonable default of 60FPS if not explicitly configured.
    .clear_frames = 2, // Clear each write FB once on first input_loop ticks
};

static int32_t frame_integrator = 0;
static uint8_t skip_streak = 0;

/* Shared with PCE's prefetch sound-sync so pause/resume can't desync the two. */
uint32_t common_emu_sound_dma_marker = 0;

void common_emu_sound_sync_reset(void)
{
    /* Pin to the current DMA half so the next sync waits for a fresh edge.
     * After a long pause the old marker is many IRQs behind, the wait becomes
     * a no-op, and the emu races a frame at 100% CPU (audible audio glitch). */
    common_emu_sound_dma_marker = dma_counter;
}

void common_emu_frame_loop_reset(void){
    common_emu_state.last_sync_time = 0;
    common_emu_state.skipped_frames =0;
    common_emu_state.skip_frames =0;
    common_emu_state.pause_frames=0;
    common_emu_state.pause_after_frames=0;
    common_emu_state.startup_frames=0;
    common_emu_state.clear_frames=0;
    frame_integrator = 0;
    skip_streak = 0;
    common_emu_sound_sync_reset();
}

bool common_emu_frame_loop(void){
    rg_app_desc_t *app = odroid_system_get_app();
    int16_t frame_time_10us = common_emu_state.frame_time_10us;
    /* int32_t: a long stall must not overflow a 16-bit elapsed_10us into
     * negative space (which would randomly "credit" pacing error). */
    int32_t elapsed_10us = 100 * (int32_t)get_elapsed_time_since(common_emu_state.last_sync_time);
    bool draw_frame = common_emu_state.skip_frames < 2;

    /* Overload guard: under sustained slowdown, skip_frames can stay >=2
     * long enough to leave the screen visually frozen. Force one drawn
     * frame in every 4 worst-case so the user always sees something. */
    if (!draw_frame && ++skip_streak >= 4) { draw_frame = true; skip_streak = 0; }
    else if (draw_frame) skip_streak = 0;

    if( !cpumon_stats.busy_ms ) cpumon_busy();
    odroid_system_tick(!draw_frame, 0, cpumon_stats.busy_ms);
    cpumon_reset();

    common_emu_state.pause_frames = 0;
    common_emu_state.skip_frames = 0;

    common_emu_state.last_sync_time = get_elapsed_time();

    if(common_emu_state.startup_frames < 3) {
        common_emu_state.startup_frames++;
        return true;
    }

    switch(app->speedupEnabled){
        case SPEEDUP_0_5x:
            frame_time_10us *= 2;
            break;
        case SPEEDUP_0_75x:
            frame_time_10us *= 5;
            frame_time_10us /= 4;
            break;
        case SPEEDUP_1_25x:
            frame_time_10us *= 4;
            frame_time_10us /= 5;
            break;
        case SPEEDUP_1_5x:
            frame_time_10us *= 2;
            frame_time_10us /= 3;
            break;
        case SPEEDUP_2x:
            frame_time_10us /= 2;
            break;
        case SPEEDUP_3x:
            frame_time_10us /= 3;
            break;
    }
    frame_integrator += (elapsed_10us - frame_time_10us);

    /* Clamp: short-term pacing error, not a debt ledger.
     * One long stall would otherwise make the integrator saturate and
     * keep drawFrame disabled for too long. */
    {
        const int32_t debt_cap   = ((int32_t)frame_time_10us << 1) + (frame_time_10us >> 1);
        const int32_t credit_cap = -(int32_t)frame_time_10us - (frame_time_10us >> 1);
        if (frame_integrator > debt_cap)        frame_integrator = debt_cap;
        else if (frame_integrator < credit_cap) frame_integrator = credit_cap;
    }

    if(frame_integrator > frame_time_10us << 1) common_emu_state.skip_frames = 2;
    else if(frame_integrator > frame_time_10us) common_emu_state.skip_frames = 1;
    else if(frame_integrator < -frame_time_10us) common_emu_state.pause_frames = 1;
    common_emu_state.skipped_frames += common_emu_state.skip_frames;

    return draw_frame;
}

static bool ingame_overlay_loop() {
    if(get_elapsed_time_since(common_emu_state.last_overlay_time) > 1000) {
        if (common_emu_state.overlay != INGAME_OVERLAY_NONE) {
            set_ingame_overlay(INGAME_OVERLAY_NONE);
            return true;
        }
    }
    return false;
}

static void repaint_overlay(void_callback_t repaint) {
    ingame_overlay_loop();
    repaint();
}

static void open_pause_menu(odroid_dialog_choice_t *game_options, void_callback_t repaint, bool pause_banner, odroid_menu_flags_t flags) {
    void _repaint() {
        repaint_overlay(repaint);
    }

    if (pause_banner) {
        // Show the Pause screen, listen to button presses,
        // if PAUSE/SET is pressed, dismiss the Pause screen
        // and open the game settings menu
        bool is_pause_key_pressed = false;

        int _pause_input_handler(odroid_gamepad_state_t* joystick) {
            // PAUSE/SET button
            if(joystick->values[ODROID_INPUT_VOLUME]) {
                is_pause_key_pressed = true;
                return 1;
            }

            return 0;
        }

        odroid_overlay_sleep_pause_banner(_repaint, flags, &_pause_input_handler);

        if (is_pause_key_pressed) {
            lcd_sync();
            odroid_overlay_game_menu(game_options, _repaint, flags);
        }
    } else {
        odroid_overlay_game_menu(game_options, _repaint, flags);
    }

    if ((flags & ODROID_MENU_FLAG_DRAW_ONLY) == 0) {
        common_emu_state.pause_after_frames = 0;
        common_emu_state.startup_frames = 0;
        /* Clear BOTH framebuffers now. Deferred clear_frames=2 only wiped
         * the active write buffer per input_loop tick; a skipped frame
         * (no lcd_swap) burned a clear on the same buffer and left pause
         * chrome on the other — letterboxed cores (NES scaling-off) then
         * showed menu leftovers on the unreblitted sides ~50% of the time. */
        lcd_sleep_while_swap_pending();
        lcd_clear_buffers();
        common_emu_state.clear_frames = 0;
        common_emu_state.skip_frames = 0;
        common_emu_state.pause_frames = 0;
        frame_integrator = 0;
        common_emu_state.last_sync_time = get_elapsed_time();
        cpumon_stats.last_busy = 0;
        common_emu_sound_sync_reset();
    }
}

static void sleep_and_open_pause_menu(odroid_dialog_choice_t *game_options, void_callback_t repaint, bool pause_banner, odroid_menu_flags_t flags) {
    open_pause_menu(game_options, repaint, pause_banner, ODROID_MENU_FLAG_DRAW_ONLY);
    lcd_sync();
    odroid_system_sleep_ex(SLEEP_ENTER_SLEEP, NULL);

    open_pause_menu(game_options, repaint, pause_banner, 0);
}

/**
 * Common input/macro/menuing features inside all emu loops. This is to be called
 * after inputs are read into `joystick`, but before the actual emulation tick
 * is called.
 *
 * Note: No pending LCD swaps are allowed when calling this function.
 *       This is to ensure a stutter free handling of the in-game overlays.
 */
void common_emu_input_loop(odroid_gamepad_state_t *joystick, odroid_dialog_choice_t *game_options, void_callback_t repaint) {
    rg_app_desc_t *app = odroid_system_get_app();
    static emu_speedup_t last_speedup = SPEEDUP_1_5x;
    static int8_t last_key = -1;
    static bool pause_pressed = false;
    static bool macro_activated = false;

    void _repaint() {
        repaint_overlay(repaint);
    }

    if(joystick->values[ODROID_INPUT_VOLUME]){  // PAUSE/SET button
        // PAUSE/SET has been pressed, checking additional inputs for macros
        pause_pressed = true;
        if(last_key < 0) {
            if (joystick->values[ODROID_INPUT_POWER]){
                // Do NOT save-state and then poweroff
                last_key = ODROID_INPUT_POWER;
                audio_stop_playing();
                sleep_and_open_pause_menu(game_options, _repaint, false, 0);
            }
            else if(joystick->values[ODROID_INPUT_START]){ // GAME button
#if SD_CARD == 1
                // Create BMP header for 320x240 RGB565
                uint8_t bmp_header[66] = {
                    0x42, 0x4D,             // BM signature
                    0x42, 0x58, 0x02, 0x00, // File size (66 + 320x240x2 = 153666 = 0x25842)
                    0x00, 0x00,             // Reserved
                    0x00, 0x00,             // Reserved
                    0x42, 0x00, 0x00, 0x00, // Data offset (66 bytes)
                    0x28, 0x00, 0x00, 0x00, // DIB header size (40 bytes)
                    0x40, 0x01, 0x00, 0x00, // Width = 320
                    0xF0, 0x00, 0x00, 0x00, // Height = 240
                    0x01, 0x00,             // Planes = 1
                    0x10, 0x00,             // BitCount = 16
                    0x03, 0x00, 0x00, 0x00, // Compression = BI_BITFIELDS
                    0x00, 0x58, 0x02, 0x00, // Image size = 153600
                    0x00, 0x00, 0x00, 0x00, // X pixels per meter
                    0x00, 0x00, 0x00, 0x00, // Y pixels per meter
                    0x00, 0x00, 0x00, 0x00, // Colors used
                    0x00, 0x00, 0x00, 0x00, // Important colors

                    // Color masks (12 bytes)
                    0x00, 0xF8, 0x00, 0x00, // Red mask   = 0xF800
                    0xE0, 0x07, 0x00, 0x00, // Green mask = 0x07E0
                    0x1F, 0x00, 0x00, 0x00  // Blue mask  = 0x001F
                };
                rg_storage_mkdir(ODROID_BASE_PATH_SCREENSHOTS);

                char *file_path = odroid_system_get_path(ODROID_PATH_USER_SCREENSHOT, odroid_system_get_app()->romPath);
                printf("file_path: %s\n", file_path);
                FILE *file = fopen(file_path, "wb");
                if (file == NULL) {
                    free(file_path);
                    return;
                }

                // Write BMP header (always RGB565 — convert from LUT8 if needed)
                fwrite(bmp_header, 1, 66, file);

                odroid_audio_mute(true);
                lcd_sleep_while_swap_pending();
                uint8_t *data = (uint8_t*)lcd_get_inactive_buffer();

                /* BMP is bottom-up. In LUT8 the framebuffer is 1 byte/pixel
                 * (CLUT indices); expand via the live CLUT so the on-disk
                 * BMP stays RGB565 like RGB565-mode screenshots. Same
                 * conversion helper as savestate-preview loading. */
                if (lcd_get_mode() == LCD_MODE_LUT8) {
                    uint16_t row[GW_LCD_WIDTH];
                    for (int y = GW_LCD_HEIGHT - 1; y >= 0; y--) {
                        lcd_convert_lut8_to_rgb565(&data[y * GW_LCD_WIDTH],
                                                   row, GW_LCD_WIDTH, NULL);
                        fwrite(row, sizeof(uint16_t), GW_LCD_WIDTH, file);
                    }
                } else {
                    for (int y = GW_LCD_HEIGHT - 1; y >= 0; y--) {
                        uint8_t *src_line = &data[y * GW_LCD_WIDTH * 2];
                        fwrite(src_line, 1, GW_LCD_WIDTH * 2, file);
                    }
                }

                fclose(file);
                free(file_path);
                set_ingame_overlay(INGAME_OVERLAY_SC);
                odroid_audio_mute(false);
#else
#if ENABLE_SCREENSHOT
/*
                printf("Capturing screenshot...\n");
                odroid_audio_mute(true);
                lcd_sleep_while_swap_pending();
                fs_file_t *file = fs_open("SCREENSHOT", FS_WRITE, FS_COMPRESS);
                fs_write(file, lcd_get_inactive_buffer(), lcd_get_frame_size());
                fs_close(file);
                set_ingame_overlay(INGAME_OVERLAY_SC);
                odroid_audio_mute(false);
                common_emu_state.startup_frames = 0;
                printf("Screenshot captured\n");*/
#else
                printf("Screenshot support is disabled\n");
#endif
#endif
                last_key = ODROID_INPUT_START;
            }
            else if(joystick->values[ODROID_INPUT_SELECT]){ // TIME button
                // Toggle Speedup
                last_key = ODROID_INPUT_SELECT;
                if(app->speedupEnabled == SPEEDUP_1x) {
                    app->speedupEnabled = last_speedup;
                }
                else {
                    last_speedup = app->speedupEnabled;
                    app->speedupEnabled = SPEEDUP_1x;
                }
                set_ingame_overlay(INGAME_OVERLAY_SPEEDUP);
            }
            else if(joystick->values[ODROID_INPUT_LEFT]){
                // Volume Up
                last_key = ODROID_INPUT_LEFT;
                int8_t level = odroid_audio_volume_get();
                if (level > ODROID_AUDIO_VOLUME_MIN) odroid_audio_volume_set(--level);
                set_ingame_overlay(INGAME_OVERLAY_VOLUME);
            }
            else if(joystick->values[ODROID_INPUT_RIGHT]){
                // Volume Down
                last_key = ODROID_INPUT_RIGHT;
                int8_t level = odroid_audio_volume_get();
                if (level < ODROID_AUDIO_VOLUME_MAX) odroid_audio_volume_set(++level);
                set_ingame_overlay(INGAME_OVERLAY_VOLUME);
            }
            else if(joystick->values[ODROID_INPUT_UP]){
                // Brightness Up
                last_key = ODROID_INPUT_UP;
                int8_t level = odroid_display_get_backlight();
                if (level < ODROID_BACKLIGHT_LEVEL_COUNT - 1) odroid_display_set_backlight(++level);
                set_ingame_overlay(INGAME_OVERLAY_BRIGHTNESS);
            }
            else if(joystick->values[ODROID_INPUT_DOWN]){
                // Brightness Down
                last_key = ODROID_INPUT_DOWN;
                int8_t level = odroid_display_get_backlight();
                if (level > 0) odroid_display_set_backlight(--level);
                set_ingame_overlay(INGAME_OVERLAY_BRIGHTNESS);
            }
            else if(joystick->values[ODROID_INPUT_A]){
                // Save State
                last_key = ODROID_INPUT_A;
                odroid_audio_mute(true);

                // Call ingame overlay so that the save icon gets displayed first.
                set_ingame_overlay(INGAME_OVERLAY_SAVE);

                odroid_system_emu_save_state(0);
                odroid_audio_mute(false);
                common_emu_state.startup_frames = 0;
            }
            else if(joystick->values[ODROID_INPUT_B]){
                // Load State
                last_key = ODROID_INPUT_B;
                // TODO : handle the last saved slot thing
                odroid_system_emu_load_state(0);
                common_emu_state.startup_frames = 0;
                set_ingame_overlay(INGAME_OVERLAY_LOAD);
            }
            else if(joystick->values[ODROID_INPUT_X]){
                last_key = ODROID_INPUT_X;
                odroid_audio_mute(true);
                //change turbo
                uint8_t turbo_key = odroid_settings_turbo_buttons_get();
                turbo_key ^= 1;
                odroid_settings_turbo_buttons_set(turbo_key);
                set_ingame_overlay(INGAME_OVERLAY_BUTTON_A);
                odroid_audio_mute(false);
                common_emu_state.startup_frames = 0;
            }
            else if(joystick->values[ODROID_INPUT_Y]){
                last_key = ODROID_INPUT_Y;
                odroid_audio_mute(true);
                //change turbo
                uint8_t turbo_key = odroid_settings_turbo_buttons_get();
                turbo_key ^= 2;
                odroid_settings_turbo_buttons_set(turbo_key);
                set_ingame_overlay(INGAME_OVERLAY_BUTTON_B);
                odroid_audio_mute(false);
                common_emu_state.startup_frames = 0;
            }
        }

        if (last_key >= 0) {
            macro_activated = true;
            if (!joystick->values[last_key]) {
                last_key = -1;
            }

            // Consume all inputs so it doesn't get passed along to the
            // running emulator
            memset(joystick, '\x00', sizeof(odroid_gamepad_state_t));
        }

        // Refresh the last_overlay_time so that it won't disappear until after
        // PAUSE/SET has been released.
        common_emu_state.last_overlay_time = get_elapsed_time();
    }
    else if (pause_pressed && !joystick->values[ODROID_INPUT_VOLUME] && !macro_activated){
        // PAUSE/SET has been released without performing any macro. Launch menu
        pause_pressed = false;

        open_pause_menu(game_options, _repaint, false, 0);
    }
    else if (!joystick->values[ODROID_INPUT_VOLUME]){
        pause_pressed = false;
        macro_activated = false;
        last_key = -1;
    }

    if (ingame_overlay_loop()) {
        /* Two ticks: clear only the write buffer each time so the next
         * emu paint+swap wipes HUD leftovers from both FBs. Clearing the
         * currently displayed buffer (lcd_clear_buffers) flashes black. */
        common_emu_state.clear_frames = 2;
    }

    if (common_emu_state.clear_frames) {
        common_emu_state.clear_frames--;
        lcd_sleep_while_swap_pending();
        lcd_clear_active_buffer();
    }

    if (joystick->values[ODROID_INPUT_POWER]) {
        // Save-state and poweroff
        audio_stop_playing();
        odroid_system_sleep_ex(SLEEP_SHOW_ANIMATION, NULL);
#if OFF_SAVESTATE == 1 || SD_CARD == 1
        odroid_system_emu_save_state(-1);
#else
        odroid_system_emu_save_state(0);
#endif
        sleep_and_open_pause_menu(game_options, _repaint, true, ODROID_MENU_FLAG_DRAW_ONLY);
    }

    if (common_emu_state.pause_after_frames > 0) {
        (common_emu_state.pause_after_frames)--;
        if (common_emu_state.pause_after_frames == 0) {
            pause_pressed = true;
        }
    }
}

void common_emu_input_loop_handle_turbo(odroid_gamepad_state_t *joystick) {
    uint8_t turbo_buttons = odroid_settings_turbo_buttons_get();
    bool turbo_a = (joystick->values[ODROID_INPUT_A] && (turbo_buttons & 1));
    bool turbo_b = (joystick->values[ODROID_INPUT_B] && (turbo_buttons & 2));
    bool turbo_button = odroid_button_turbos();
    if (turbo_a)
        joystick->values[ODROID_INPUT_A] = turbo_button;
    if (turbo_b)
        joystick->values[ODROID_INPUT_B] = !turbo_button;
}

void common_emu_sound_sync(bool use_nops) {
    if (!common_emu_state.skip_frames) {
        if (common_emu_sound_dma_marker == 0) {
            common_emu_sound_dma_marker = dma_counter;
        }
        for (uint8_t p = 0; p < common_emu_state.pause_frames + 1; p++) {
            while (dma_counter == common_emu_sound_dma_marker) {
                if (use_nops) {
                    __NOP();
                } else {
                    cpumon_sleep();
                }
            }
            common_emu_sound_dma_marker = dma_counter;
        }
    }
}

bool common_emu_sound_loop_is_muted() {
    if (audio_mute || odroid_audio_volume_get() == ODROID_AUDIO_VOLUME_MIN) {
        audio_clear_active_buffer();
        return true;
    }
    return false;
}

uint8_t common_emu_sound_get_volume() {
    return volume_tbl[odroid_audio_volume_get()];
}

/* DWT counter used to measure time execution */
volatile unsigned int *DWT_CONTROL = (unsigned int *)0xE0001000;
volatile unsigned int *DWT_CYCCNT = (unsigned int *)0xE0001004;
volatile unsigned int *DEMCR = (unsigned int *)0xE000EDFC;
volatile unsigned int *LAR = (unsigned int *)0xE0001FB0; // <-- lock access register

void common_emu_enable_dwt_cycles()
{
    /* Use DWT cycle counter to get precision time elapsed during loop.
       The DWT cycle counter is cleared on every loop.
       It may crash if the DWT is used during trace profiling */

    *DEMCR = *DEMCR | 0x01000000;    // enable trace
    *LAR = 0xC5ACCE55;               // <-- added unlock access to DWT (ITM, etc.)registers
    *DWT_CYCCNT = 0;                 // clear DWT cycle counter
    *DWT_CONTROL = *DWT_CONTROL | 1; // enable DWT cycle counter
}

inline __attribute__((always_inline))
unsigned int common_emu_get_dwt_cycles() {
    return *DWT_CYCCNT;
}

inline __attribute__((always_inline))
void common_emu_clear_dwt_cycles() {
    *DWT_CYCCNT = 0;
}

static void cpumon_common(bool sleep){
    uint t0 = get_elapsed_time();
    if(cpumon_stats.last_busy){
        cpumon_stats.busy_ms += t0 - cpumon_stats.last_busy;
    }
    else{
        cpumon_stats.busy_ms = 0;
    }
    if(sleep) __WFI();
    uint t1 = get_elapsed_time();
    cpumon_stats.last_busy = t1;
    cpumon_stats.sleep_ms += t1 - t0;
}


void cpumon_busy(void){
    cpumon_common(false);
}

void cpumon_sleep(void){
    cpumon_common(true);
}

void cpumon_reset(void){
    cpumon_stats.busy_ms = 0;
    cpumon_stats.sleep_ms = 0;
}

#define OVERLAY_COLOR_565 0xFFFF

static const uint8_t ROUND[] = {  // This is the top/left of a 8-pixel radius circle
    0b00000011,
    0b00001111,
    0b00011111,
    0b00111111,
    0b01111111,
    0b01111111,
    0b11111111,
    0b11111111,
};

// These must be multiples of 8
#define IMG_H 24
#define IMG_W 24

/* Mode-agnostic via lcd_pen_t — handles both RGB565 and LUT8. The fb
 * argument is unused (the pen captures lcd_get_active_buffer() itself);
 * it's kept on the signature for back-compat with existing call sites. */

__attribute__((optimize("unroll-loops")))
static void draw_img(pixel_t *fb, const uint8_t *img, uint16_t x, uint16_t y){
    (void)fb;
    lcd_pen_t pen = lcd_pen(OVERLAY_COLOR_565);
    uint16_t idx = 0;
    for(uint8_t i=0; i < IMG_H; i++) {
        for(uint8_t j=0; j < IMG_W; j++) {
            if(img[idx / 8] & (1 << (7 - idx % 8))){
                lcd_pen_set(&pen, x + j + GW_LCD_WIDTH * (y + i));
            }
            idx++;
        }
    }
}

__attribute__((optimize("unroll-loops")))
static void draw_rectangle(pixel_t *fb, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2){
    (void)fb;
    lcd_pen_t pen = lcd_pen(OVERLAY_COLOR_565);
    for(uint16_t i=y1; i < y2; i++){
        lcd_pen_run(&pen, x1 + GW_LCD_WIDTH * i, x2 - x1);
    }
}

__attribute__((optimize("unroll-loops")))
static void draw_darken_rectangle(pixel_t *fb, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2){
    (void)fb;
    lcd_pen_t pen = lcd_pen(0); /* color unused for darken — pen carries mode */
    for(uint16_t i=y1; i < y2; i++){
        for(uint16_t j=x1; j < x2; j++){
            lcd_pen_darken(&pen, j + GW_LCD_WIDTH * i);
        }
    }
}

/*
 * LUT8 panel pixel:
 *  - Game content: darken (≈ translucent gray over the scene).
 *  - Letterbox (0), already-shaded HUD gray, other overlay slots, or
 *    near-black game pixels: solid panel gray.
 * Never darken overlay indices — that nearest-matched into the cart
 * palette and turned letterbox gray greenish on the next frame.
 */
static inline void hud_lut8_shade(uint8_t *fb, int off, uint8_t gray_idx)
{
    uint8_t idx = fb[off];
    if (idx == 0 || idx == gray_idx ||
        (idx >= LCD_OVERLAY_CLUT_BASE &&
         idx < (uint8_t)(LCD_OVERLAY_CLUT_BASE + LCD_OVERLAY_CLUT_MAX)) ||
        lcd_clut_luma_sum(idx) < 48) {
        fb[off] = gray_idx;
        return;
    }
    uint8_t d = lcd_darken_index(idx);
    fb[off] = (d == 0 || lcd_clut_luma_sum(d) < 48) ? gray_idx : d;
}

static uint8_t hud_lut8_gray_idx(void)
{
    return (uint8_t)(LCD_OVERLAY_CLUT_BASE + LCD_OVERLAY_CLUT_GRAY);
}

static uint8_t hud_lut8_gray_dark_idx(void)
{
    return (uint8_t)(LCD_OVERLAY_CLUT_BASE + LCD_OVERLAY_CLUT_GRAY_DARK);
}

__attribute__((optimize("unroll-loops")))
static void draw_hud_shade_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    if (lcd_get_mode() != LCD_MODE_LUT8) {
        draw_darken_rectangle(NULL, x1, y1, x2, y2);
        return;
    }

    uint8_t *fb = (uint8_t *)lcd_get_active_buffer();
    uint8_t gray = hud_lut8_gray_idx();
    for (uint16_t i = y1; i < y2; i++) {
        for (uint16_t j = x1; j < x2; j++)
            hud_lut8_shade(fb, j + GW_LCD_WIDTH * i, gray);
    }
}

__attribute__((optimize("unroll-loops")))
static void draw_hud_empty_box(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    if (lcd_get_mode() != LCD_MODE_LUT8) {
        draw_darken_rectangle(NULL, x1, y1, x2, y2);
        return;
    }
    /* Opaque darker gray — second darken of panel gray would be a no-op
     * (overlay map is identity) and looked identical to the panel. */
    uint8_t *fb = (uint8_t *)lcd_get_active_buffer();
    uint8_t dark = hud_lut8_gray_dark_idx();
    for (uint16_t i = y1; i < y2; i++) {
        memset(&fb[x1 + GW_LCD_WIDTH * i], dark, (size_t)(x2 - x1));
    }
}

__attribute__((optimize("unroll-loops")))
void draw_darken_rounded_rectangle(pixel_t *fb, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2){
    (void)fb;
    // *1 is inclusive, *2 is exclusive
    uint16_t h = y2 - y1;
    uint16_t w = x2 - x1;
    if (w < 16 || h < 16) {
        draw_hud_shade_rectangle(x1, y1, x2, y2);
        return;
    }

    if (lcd_get_mode() == LCD_MODE_LUT8) {
        uint8_t *dst = (uint8_t *)lcd_get_active_buffer();
        uint8_t gray = hud_lut8_gray_idx();

        for (uint8_t i = 0; i < 8; i++)
            for (uint8_t j = 0; j < 8; j++)
                if (ROUND[i] & (1 << (7 - j))) {
                    hud_lut8_shade(dst, x1 + j + GW_LCD_WIDTH * (y1 + i), gray);
                    hud_lut8_shade(dst, x2 - j - 1 + GW_LCD_WIDTH * (y1 + i), gray);
                    hud_lut8_shade(dst, x1 + j + GW_LCD_WIDTH * (y2 - i - 1), gray);
                    hud_lut8_shade(dst, x2 - j - 1 + GW_LCD_WIDTH * (y2 - i - 1), gray);
                }

        for (uint16_t i = x1 + 8; i < x2 - 8; i++)
            for (uint8_t j = 0; j < 8; j++) {
                hud_lut8_shade(dst, i + GW_LCD_WIDTH * (y1 + j), gray);
                hud_lut8_shade(dst, i + GW_LCD_WIDTH * (y2 - j - 1), gray);
            }

        for (uint16_t i = x1; i < x2; i++)
            for (uint16_t j = y1 + 8; j < y2 - 8; j++)
                hud_lut8_shade(dst, i + GW_LCD_WIDTH * j, gray);
        return;
    }

    lcd_pen_t pen = lcd_pen(0); /* mode-only pen, color irrelevant for darken */

    // Draw upper left round
    for(uint8_t i=0; i < 8; i++) for(uint8_t j=0; j < 8; j++)
        if(ROUND[i] & (1 << (7 - j))) lcd_pen_darken(&pen, x1 + j + GW_LCD_WIDTH * (y1 + i));

    // Draw upper right round
    for(uint8_t i=0; i < 8; i++) for(uint8_t j=0; j < 8; j++)
        if(ROUND[i] & (1 << (7 - j))) lcd_pen_darken(&pen, x2 - j - 1 + GW_LCD_WIDTH * (y1 + i));

    // Draw lower left round
    for(uint8_t i=0; i < 8; i++) for(uint8_t j=0; j < 8; j++)
        if(ROUND[i] & (1 << (7 - j))) lcd_pen_darken(&pen, x1 + j + GW_LCD_WIDTH * (y2 - i - 1));

    // Draw lower right round
    for(uint8_t i=0; i < 8; i++) for(uint8_t j=0; j < 8; j++)
        if(ROUND[i] & (1 <<  (7 - j))) lcd_pen_darken(&pen, x2 - j - 1 + GW_LCD_WIDTH * (y2 - i - 1));

    // Draw upper rectangle
    for(uint16_t i=x1+8; i < x2 - 8; i++) for(uint8_t j=0; j < 8; j++)
        lcd_pen_darken(&pen, i + GW_LCD_WIDTH * (y1 + j));

    // Draw central rectangle
    for(uint16_t i=x1; i < x2; i++) for(uint16_t j=y1+8; j < y2-8; j++)
        lcd_pen_darken(&pen, i + GW_LCD_WIDTH * j);

    // Draw lower rectangle
    for(uint16_t i=x1+8; i < x2 - 8; i++) for(uint8_t j=0; j < 8; j++)
        lcd_pen_darken(&pen, i + GW_LCD_WIDTH * (y2 - j - 1));
}

static void hud_draw_panel(pixel_t *fb, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    draw_darken_rounded_rectangle(fb, x1, y1, x2, y2);
}

static void hud_draw_level_box(pixel_t *fb, int filled, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    if (filled)
        draw_rectangle(fb, x1, y1, x2, y2);
    else
        draw_hud_empty_box(x1, y1, x2, y2);
}

#define INGAME_OVERLAY_X 265
#define INGAME_OVERLAY_Y 10
#define INGAME_OVERLAY_BARS_H 128
#define INGAME_OVERLAY_W 39
#define INGAME_OVERLAY_BORDER 4
#define INGAME_OVERLAY_BOX_GAP 2

#define INGAME_OVERLAY_BARS_W INGAME_OVERLAY_W
#define INGAME_OVERLAY_IMG_H  (IMG_H + 2 * INGAME_OVERLAY_BORDER)  // For when only an image is showing

#define INGAME_OVERLAY_BOX_W (INGAME_OVERLAY_BARS_W - (2 * INGAME_OVERLAY_BORDER) - 6)
#define INGAME_OVERLAY_BOX_X (INGAME_OVERLAY_X + ((INGAME_OVERLAY_BARS_W - INGAME_OVERLAY_BOX_W) / 2))
#define INGAME_OVERLAY_BOX_Y (INGAME_OVERLAY_Y + INGAME_OVERLAY_BORDER + 3)

// For when only an image is shown
#define INGAME_OVERLAY_IMG_X (INGAME_OVERLAY_X + ((INGAME_OVERLAY_BARS_W - IMG_W) / 2))
#define INGAME_OVERLAY_IMG_Y (INGAME_OVERLAY_Y + INGAME_OVERLAY_BORDER)

// Places the image at the bottom for bars-related overlay (volume, brightness)
#define INGAME_OVERLAY_BARS_IMG_X INGAME_OVERLAY_IMG_X
#define INGAME_OVERLAY_BARS_IMG_Y (INGAME_OVERLAY_Y + INGAME_OVERLAY_BARS_H - IMG_H - INGAME_OVERLAY_BORDER)

#define DARKEN_IMG_ONLY() hud_draw_panel(fb, \
                    INGAME_OVERLAY_X, \
                    INGAME_OVERLAY_Y, \
                    INGAME_OVERLAY_X + INGAME_OVERLAY_BARS_W, \
                    INGAME_OVERLAY_Y + INGAME_OVERLAY_IMG_H)


static uint8_t box_height(uint8_t n) {
    return ((INGAME_OVERLAY_BARS_IMG_Y - INGAME_OVERLAY_BOX_Y) / n) - INGAME_OVERLAY_BOX_GAP;
}

void common_ingame_overlay(void) {
    rg_app_desc_t *app = odroid_system_get_app();
    pixel_t *fb = lcd_get_active_buffer();
    int8_t level;
    uint8_t bh;
    uint8_t turbo_key;
    uint16_t by = INGAME_OVERLAY_BOX_Y;

    odroid_battery_state_t battery_state = odroid_input_read_battery();
    uint16_t percentage = battery_state.percentage;
    if (percentage <= 15) {
        if ((get_elapsed_time() % 1000) < 300)
            odroid_overlay_draw_battery(battery_state, 150, 90); 
    }
    
    switch(common_emu_state.overlay)
    {
        case INGAME_OVERLAY_NONE:
            break;
        case INGAME_OVERLAY_VOLUME:
            level = odroid_audio_volume_get();
            bh = box_height(ODROID_AUDIO_VOLUME_MAX);

            hud_draw_panel(fb,
                    INGAME_OVERLAY_X,
                    INGAME_OVERLAY_Y,
                    INGAME_OVERLAY_X + INGAME_OVERLAY_BARS_W,
                    INGAME_OVERLAY_Y + INGAME_OVERLAY_BARS_H);
            draw_img(fb, IMG_SPEAKER, INGAME_OVERLAY_BARS_IMG_X, INGAME_OVERLAY_BARS_IMG_Y);

            for(int8_t i=ODROID_AUDIO_VOLUME_MAX; i > 0; i--){
                hud_draw_level_box(fb, i <= level,
                            INGAME_OVERLAY_BOX_X,
                            by,
                            INGAME_OVERLAY_BOX_X + INGAME_OVERLAY_BOX_W,
                            by + bh);
                by += bh + INGAME_OVERLAY_BOX_GAP;
            }
            break;
        case INGAME_OVERLAY_BRIGHTNESS:
            level = odroid_display_get_backlight();
            bh = box_height(ODROID_BACKLIGHT_LEVEL_COUNT - 1);

            hud_draw_panel(fb,
                    INGAME_OVERLAY_X,
                    INGAME_OVERLAY_Y,
                    INGAME_OVERLAY_X + INGAME_OVERLAY_BARS_W,
                    INGAME_OVERLAY_Y + INGAME_OVERLAY_BARS_H);
            draw_img(fb, IMG_SUN, INGAME_OVERLAY_BARS_IMG_X, INGAME_OVERLAY_BARS_IMG_Y);

            for(int8_t i=ODROID_BACKLIGHT_LEVEL_COUNT-1; i > 0; i--){
                hud_draw_level_box(fb, i <= level,
                            INGAME_OVERLAY_BOX_X,
                            by,
                            INGAME_OVERLAY_BOX_X + INGAME_OVERLAY_BOX_W,
                            by + bh);
                by += bh + INGAME_OVERLAY_BOX_GAP;
            }
            break;
        case INGAME_OVERLAY_LOAD:
            DARKEN_IMG_ONLY();
            draw_img(fb, IMG_FOLDER, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
            break;
        case INGAME_OVERLAY_SAVE:
            DARKEN_IMG_ONLY();
            draw_img(fb, IMG_DISKETTE, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
            break;
        case INGAME_OVERLAY_SC:
            DARKEN_IMG_ONLY();
            draw_img(fb, IMG_SC, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
            break;
        case INGAME_OVERLAY_BUTTON_A:
            DARKEN_IMG_ONLY();
            turbo_key = odroid_settings_turbo_buttons_get();
            if (turbo_key & 1)
                draw_img(fb, IMG_BUTTON_A_P, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
            else
                draw_img(fb, IMG_BUTTON_A, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
            break;
        case INGAME_OVERLAY_BUTTON_B:
            DARKEN_IMG_ONLY();
            turbo_key = odroid_settings_turbo_buttons_get();
            if (turbo_key & 2)
                draw_img(fb, IMG_BUTTON_B_P, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
            else
                draw_img(fb, IMG_BUTTON_B, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
            break;
        case INGAME_OVERLAY_SPEEDUP:
            DARKEN_IMG_ONLY();
            switch(app->speedupEnabled){
                case SPEEDUP_0_5x:
                    draw_img(fb, IMG_0_5X, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
                    break;
                case SPEEDUP_0_75x:
                    draw_img(fb, IMG_0_75X, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
                    break;
                case SPEEDUP_1x:
                    draw_img(fb, IMG_1X, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
                    break;
                case SPEEDUP_1_25x:
                    draw_img(fb, IMG_1_25X, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
                    break;
                case SPEEDUP_1_5x:
                    draw_img(fb, IMG_1_5X, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
                    break;
                case SPEEDUP_2x:
                    draw_img(fb, IMG_2X, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
                    break;
                case SPEEDUP_3x:
                    draw_img(fb, IMG_3X, INGAME_OVERLAY_IMG_X, INGAME_OVERLAY_IMG_Y);
                    break;
            }
            break;

    }
}

static void set_ingame_overlay(ingame_overlay_t type){
    common_emu_state.overlay = type;
    common_emu_state.last_overlay_time = get_elapsed_time();
}
