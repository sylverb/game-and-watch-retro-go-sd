#pragma once

#include <odroid_system.h>

#include "main.h"
#include "gw_lcd.h"
#include "gw_audio.h"

#define WIDTH  320
#define HEIGHT 240
#define BPP      4

extern const uint8_t volume_tbl[ODROID_AUDIO_VOLUME_MAX + 1];

void common_emu_frame_loop_reset(void);
bool common_emu_frame_loop(void);
void common_emu_input_loop(odroid_gamepad_state_t *joystick, odroid_dialog_choice_t *game_options, void_callback_t repaint);
void common_emu_input_loop_handle_turbo(odroid_gamepad_state_t *joystick);
void common_emu_sound_sync(bool use_nops);
bool common_emu_sound_loop_is_muted();
uint8_t common_emu_sound_get_volume();

typedef struct {
    uint last_busy;
    uint busy_ms;
    uint sleep_ms;
} cpumon_stats_t;
extern cpumon_stats_t cpumon_stats;

/**
 * Just calls `__WFI()` and measures time spent sleeping.
 */
void cpumon_sleep(void);
void cpumon_busy(void);
void cpumon_reset(void);


enum {
    INGAME_OVERLAY_NONE,
    INGAME_OVERLAY_VOLUME,
    INGAME_OVERLAY_BRIGHTNESS,
    INGAME_OVERLAY_SAVE,
    INGAME_OVERLAY_LOAD,
    INGAME_OVERLAY_SPEEDUP,
    INGAME_OVERLAY_SC,
    INGAME_OVERLAY_BUTTON_A,
    INGAME_OVERLAY_BUTTON_B,
};
typedef uint8_t ingame_overlay_t;

/**
 * Holds common higher-level emu options that need to be used at not-neat
 * locations in each emulator.
 *
 * There should only be one of these objects instantiated.
 */
typedef struct {
    uint32_t last_sync_time;
    uint32_t last_overlay_time;
    uint16_t skipped_frames;
    int16_t frame_time_10us;
    uint8_t skip_frames:2;
    uint8_t pause_frames:1;
    uint8_t pause_after_frames:3;
    uint8_t startup_frames:2;
    uint8_t overlay:4;
    uint8_t clear_frames:2;
} common_emu_state_t;

extern common_emu_state_t common_emu_state;

// DWT start
void common_emu_enable_dwt_cycles(void);
unsigned int common_emu_get_dwt_cycles(void);
void common_emu_clear_dwt_cycles(void);
// DWT end

/**
 * Drawable stuff over current emulation.
 */
void common_ingame_overlay(void);
void common_ingame_overlay_to(pixel_t *fb);

void draw_darken_rounded_rectangle(pixel_t *fb, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

/**
 * Draw border screen for Zelda 3 when not full screen.
 */
void draw_border_zelda3(pixel_t * fb);

/**
 * Draw border screen for Super Mario World.
 */
void draw_border_smw(pixel_t * fb);