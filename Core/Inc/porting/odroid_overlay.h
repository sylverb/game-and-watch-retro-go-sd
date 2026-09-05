#pragma once

#include "stdbool.h"
#include "stdint.h"
#include "rom_manager.h"
#include "odroid_input.h"

typedef enum {
    ODROID_DIALOG_INIT,
    ODROID_DIALOG_PREV,
    ODROID_DIALOG_NEXT,
    ODROID_DIALOG_FOCUS_GAINED,
    ODROID_DIALOG_ENTER,
} odroid_dialog_event_t;

typedef enum {
    ODROID_DIALOG_IGNORE,
    ODROID_DIALOG_RETURN,
} odroid_dialog_cb_return_t;

typedef enum
{
    ODROID_MENU_CLOSED,
    ODROID_MENU_PENDING,
    ODROID_MENU_OPEN,
} odroid_menu_state_t;

typedef enum
{
    ODROID_MENU_FLAG_DRAW_ONLY = 1 << 0,
    ODROID_MENU_FLAG_NO_BG_DARKEN = 1 << 1,
} odroid_menu_flags_t;

typedef struct odroid_dialog_choice odroid_dialog_choice_t;

struct odroid_dialog_choice {
    int  id;
    const char *label;
    char *value;
    int  enabled;
    bool (*update_cb)(odroid_dialog_choice_t *, odroid_dialog_event_t, uint32_t repeat);
};

typedef void (*void_callback_t)();
typedef int (*pause_input_callback_t)(odroid_gamepad_state_t* joystick);

#define ODROID_DIALOG_CHOICE_LAST {0x0F0F0F0F, "LAST", (char *)"LAST", 0xFFFF, NULL}
#define ODROID_DIALOG_CHOICE_SEPARATOR {0x0F0F0F0E, "-", "-", -1, NULL}


extern odroid_menu_state_t odroid_menu_state;

void odroid_overlay_init();
void odroid_overlay_set_font_size(int size);
int  odroid_overlay_get_font_size();
int  odroid_overlay_get_font_width();
int  odroid_overlay_draw_text(uint16_t x, uint16_t y, uint16_t width, const char *text, uint16_t color, uint16_t color_bg);
void odroid_overlay_draw_rect(int x, int y, int width, int height, int border, uint16_t color);
void odroid_overlay_draw_fill_rect(int x, int y, int width, int height, uint16_t color);
void odroid_overlay_draw_battery(odroid_battery_state_t battery, int x, int y);
void odroid_overlay_draw_dialog(const char *header, odroid_dialog_choice_t *options, int sel);
void odroid_overlay_draw_banner_text(int center_x, int center_y, const char *text);
void odroid_overlay_sleep_pause_banner(void_callback_t repaint, odroid_menu_flags_t flags, pause_input_callback_t input_cb);

int odroid_overlay_dialog(const char *header, odroid_dialog_choice_t *options, int selected, void_callback_t repaint, odroid_menu_flags_t flags);
int odroid_overlay_confirm(const char *text, bool yes_selected, void_callback_t repaint);
void odroid_overlay_alert(const char *text);

uint8_t *odroid_overlay_cache_file_in_flash(const char *file_path, uint32_t *file_size_p, bool byte_swap);
size_t   odroid_overlay_cache_file_in_ram(const char *file_path, uint8_t *dest_address);
size_t   odroid_overlay_cache_file_in_ram_with_offset(const char *file_path, uint8_t *dest_address, uint32_t offset);

int odroid_overlay_settings_menu(odroid_dialog_choice_t *extra_options, void_callback_t repaint, odroid_menu_flags_t flags);
int odroid_overlay_game_settings_menu(odroid_dialog_choice_t *extra_options, void_callback_t repaint, odroid_menu_flags_t flags);
int odroid_overlay_game_menu(odroid_dialog_choice_t *extra_options, void_callback_t repaint, odroid_menu_flags_t flags);
int odroid_savestate_menu(const char *title, const char *rom_path, bool show_preview, bool skip_on_single_used_slot, void_callback_t repaint);
