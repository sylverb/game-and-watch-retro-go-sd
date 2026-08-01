#pragma once

#include <odroid_input.h>
#include "rg_emulators.h"
#include "stdbool.h"

typedef enum {
    KEY_PRESS_A,
    KEY_PRESS_B,
    TAB_SCROLL,
    TAB_INIT,
    TAB_REFRESH_LIST,
    TAB_SAVE,
    TAB_IDLE,
    TAB_REDRAW,
} gui_event_t;

typedef enum {
    LINE_UP,
    LINE_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    FIRST_ROW,
    LAST_ROW,
} scroll_mode_t;

typedef struct {
    //char name[24];
    uint16_t bg_c;
    uint16_t main_c;
    uint16_t sel_c;
    uint16_t dis_c;
} colors_t;

typedef struct {
    const char * text;
    int enabled;
    int id;
    int arg_type;
    void *arg;
} listbox_item_t;

typedef struct {
    // listbox_item_t **items;
    listbox_item_t *items;
    int length;
    int cursor;
} listbox_t;

typedef void (*gui_event_handler_t)(gui_event_t event, void *arg);

typedef struct {
    char name[64];
    char status[96];
    int16_t header_idx;
    int16_t logo_idx;
    bool initialized;
    bool is_empty;
    void *arg;
    listbox_t listbox;
    gui_event_handler_t event_handler;
} tab_t;

typedef struct {
    tab_t *tabs[32];
    int tabcount;
    int selected;
    int theme;
    int show_empty;
    int show_cover;
    int idle_start;
    int last_key;
    odroid_gamepad_state_t joystick;
} retro_gui_t;

extern retro_gui_t gui;
extern int gui_colors_count;
extern colors_t *curr_colors;
extern colors_t gui_colors[];

tab_t *gui_add_tab(const char *name, int16_t logo_idx, int16_t header_idx, void *arg, void *event_handler);
tab_t *gui_get_tab(int index);
tab_t *gui_get_current_tab();
tab_t *gui_set_current_tab(int index);
/** Move to the next (+1) or previous (-1) non-empty tab. */
bool gui_change_tab(int direction);
void gui_init_tab(tab_t *tab);
void gui_init_colors(void);
void gui_apply_colors_to_overlay_clut(void);
void gui_refresh_tab(tab_t *tab);
void gui_save_current_tab(void);

void gui_sort_list(tab_t *tab, int sort_mode);
void gui_scroll_list(tab_t *tab, scroll_mode_t mode);
void gui_resize_list(tab_t *tab, int new_size);
listbox_item_t *gui_get_selected_item(tab_t *tab);

void gui_event(gui_event_t event, tab_t *tab);
/** Pop one ROM browse level if tab is inside a subfolder; refreshes list. */
bool rg_emulator_browse_pop_if_in_subfolder(tab_t *tab);
/** True when ROM list is browsing below /roms/<system>/ (not root of that system). */
bool rg_emulator_tab_in_rom_subfolder(const tab_t *tab);
bool rg_emulator_validate_browse_path_for_tab(tab_t *tab);
void gui_redraw_callback(void);
void gui_redraw(void);
void gui_draw_header(tab_t *tab);
void gui_draw_status(tab_t *tab);
void gui_draw_list(tab_t *tab);
void gui_draw_notice(const char *text, uint16_t color);
void gui_jump_list(tab_t *tab, int offset);
