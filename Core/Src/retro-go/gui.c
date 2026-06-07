#include <odroid_system.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>

#include "lupng.h"
#include "gui.h"
#include "gw_lcd.h"
#include "gw_ofw.h"
#include "bitmaps.h"
#include "gw_linker.h"
#include "main.h"
#include "rg_i18n.h"
#include "rg_emulators.h"
#include "gw_malloc.h"

#if !defined(COVERFLOW)
#define COVERFLOW 0
#endif /* COVERFLOW */

#define IMAGE_LOGO_WIDTH (47)
#define IMAGE_LOGO_HEIGHT (51)
#define IMAGE_BANNER_WIDTH (ODROID_SCREEN_WIDTH)
#define IMAGE_BANNER_HEIGHT (32)
#define STATUS_HEIGHT (33)
#define HEADER_HEIGHT (47)

#define CRC_WIDTH (104)
#define CRC_X_OFFSET (ODROID_SCREEN_WIDTH - CRC_WIDTH)
#define CRC_Y_OFFSET (STATUS_HEIGHT)

#define LIST_WIDTH (ODROID_SCREEN_WIDTH)
#define LIST_HEIGHT (ODROID_SCREEN_HEIGHT - STATUS_HEIGHT - HEADER_HEIGHT)
#define LIST_LINE_HEIGHT (odroid_overlay_get_font_size() + 2)
#define LIST_LINE_COUNT (LIST_HEIGHT / LIST_LINE_HEIGHT)
#define LIST_X_OFFSET (0)
#define LIST_Y_OFFSET (STATUS_HEIGHT)

#define COVER_MAX_HEIGHT (100)
#define COVER_MAX_WIDTH (186)

static listbox_item_t *global_items = NULL;

#if COVERFLOW != 0
/* instances for JPEG decoder */
#include "hw_jpeg_decoder.h"

// reuse existing buffer from gw_lcd.h
#define JPEG_BUFFER_SIZE 256*1024

#define NOCOVER_HEIGHT ((uint32_t)(68))
#define NOCOVER_WIDTH ((uint32_t)(68))
#define COVER_420_SIZE ((uint32_t)(COVER_MAX_HEIGHT * COVER_MAX_WIDTH * 3 / 2))
#define COVER_16BITS_SIZE ((uint32_t)(COVER_MAX_HEIGHT * COVER_MAX_WIDTH * 2))

static uint8_t *pJPEG_Buffer = NULL;
static uint16_t *pCover_Buffer = NULL;

const uint8_t cover_light[5] = {60, 120, 255, 120, 60};
const uint8_t cover_light3[3] = {255, 120, 60};
#endif

#if COVERFLOW != 0
const static uint32_t COVER_BORDER = 6;
static uint32_t current_cover_width = NOCOVER_WIDTH;
static uint32_t current_cover_height = NOCOVER_HEIGHT;
#endif

#define _2C_(C) (((C >> 8) & 0xF800) | ((C >> 5) & 0x7E0) | ((C >> 3) & 0x1F))
#define _2CC(C) (((C >> 0) & 0xF800) | ((C >> 13) & 0x7E0) | ((C >> 3) & 0x1F))

// Mario version colors (default)
colors_t gui_colors[] = {
    //Main Theme color
    {_2C_(0x000000), _2C_(0x600000), _2C_(0xD08828), _2C_(0x584010)},
    {_2C_(0x000000), _2C_(0x900000), _2C_(0xD08828), _2C_(0x584010)},
    {_2C_(0x000000), _2C_(0x300000), _2C_(0xD08828), _2C_(0x584010)},
    {_2C_(0x000000), _2C_(0x600000), _2C_(0xFFD700), _2C_(0x604008)},
    {_2C_(0x000000), _2C_(0x900000), _2C_(0xFFD700), _2C_(0x604008)},
    {_2C_(0x000000), _2C_(0x300000), _2C_(0xFFD700), _2C_(0x604008)},

    {_2C_(0x100000), _2C_(0x600000), _2C_(0xD08828), _2C_(0x584010)},
    {_2C_(0x100000), _2C_(0x900000), _2C_(0xD08828), _2C_(0x584010)},
    {_2C_(0x100000), _2C_(0x600000), _2C_(0xFFD700), _2C_(0x604008)},
    {_2C_(0x100000), _2C_(0x900000), _2C_(0xFFD700), _2C_(0x604008)},
    //give mario green or give zelda red theme
    {_2C_(0x000000), _2C_(0x600000), _2C_(0xD08828), _2C_(0x584010)},
    {_2C_(0x000000), _2C_(0x600000), _2C_(0xFFD700), _2C_(0x604008)},
    //othere themes
    {_2C_(0x32435F), _2C_(0x865F48), _2C_(0xE1DCD9), _2C_(0x8F8681)},
    {_2C_(0x2F1812), _2C_(0x40686A), _2C_(0xB78338), _2C_(0x915C4C)},
    {_2C_(0x002C2F), _2C_(0x023459), _2C_(0xB2A59F), _2C_(0x1E646E)},
    {_2C_(0x5D353E), _2C_(0x804838), _2C_(0xB2D6CE), _2C_(0x024B40)},
    {_2C_(0x171516), _2C_(0x2B3C1A), _2C_(0xFFE083), _2C_(0xE99A24)},
    {_2C_(0x3C1832), _2C_(0x92617E), _2C_(0xCC5B3B), _2C_(0x982827)},
    {_2C_(0x2E1E11), _2C_(0x655C57), _2C_(0xFEF0BF), _2C_(0xF3B749)},
    {_2C_(0x2C413C), _2C_(0x61271C), _2C_(0xE5D1AC), _2C_(0xEF6E2C)},
    {_2C_(0x252839), _2C_(0x9AB878), _2C_(0xF2DE99), _2C_(0xE98D24)},
    {_2C_(0x702020), _2C_(0x867182), _2C_(0xF2D3C1), _2C_(0xD36D3D)},
    {_2C_(0x32435F), _2C_(0x80341F), _2C_(0xE8C7B6), _2C_(0xEB9772)},
    {_2C_(0x704020), _2C_(0x801008), _2C_(0xACB320), _2C_(0x786C10)},
    {_2C_(0x525E76), _2C_(0x85683D), _2C_(0xDFE4DE), _2C_(0x126F80)},
    {_2C_(0x603010), _2C_(0x6D8DB6), _2C_(0xFFCC71), _2C_(0x9EA2AB)},

};

// Function to initialize colors based on OFW type
void gui_init_colors()
{
    // If Zelda version, replace colors that should be different
    if (!get_ofw_is_mario()) {
        // Zelda version: change mario red to zelda green
        
        // Update colors that use _2CC (positions that had red colors)
        gui_colors[0].main_c = _2CC(0x600000);
        gui_colors[1].main_c = _2CC(0x900000);
        gui_colors[2].main_c = _2CC(0x300000);
        gui_colors[3].main_c = _2CC(0x600000);
        gui_colors[4].main_c = _2CC(0x900000);
        gui_colors[5].main_c = _2CC(0x300000);
        
        gui_colors[6].bg_c = _2CC(0x100000);
        gui_colors[6].main_c = _2CC(0x600000);
        gui_colors[7].bg_c = _2CC(0x100000);
        gui_colors[7].main_c = _2CC(0x900000);
        gui_colors[8].bg_c = _2CC(0x100000);
        gui_colors[8].main_c = _2CC(0x600000);
        gui_colors[9].bg_c = _2CC(0x100000);
        gui_colors[9].main_c = _2CC(0x900000);
        
        // Mario/Zelda specific themes (positions 10-11).
        // _2CC is a swizzle: feed it the red-looking literal (as entries 0-9 do)
        // to get green out. Passing 0x006000 here yields 0x6000 (red), not green.
        gui_colors[10].main_c = _2CC(0x600000);  // Zelda green
        gui_colors[11].main_c = _2CC(0x600000);  // Zelda green
        
        gui_colors[23].main_c = _2CC(0x801008);  // Other theme that had _2CC
    }
}

colors_t *curr_colors = (colors_t *)(&gui_colors[0]);

int gui_colors_count = 26;

/* Push the 4 RGB565 colors of the active theme into the LUT8 overlay
 * CLUT range so the menu renders with the exact yellow/green/etc. the
 * user picked, instead of nearest-matching them against the cart's
 * palette. No-op when the LCD is in RGB565 mode. Call after changing
 * curr_colors (config load + theme nav). */
void gui_apply_colors_to_overlay_clut(void)
{
    /* Don't gate on LCD mode — lcd_set_overlay_clut() stores the colors
     * for later if not yet in LUT8 (e.g. during early config load before
     * PICO-8 has switched the LCD). */
    if (curr_colors == NULL) return;
    const uint16_t rgb565[4] = {
        curr_colors->bg_c, curr_colors->main_c,
        curr_colors->sel_c, curr_colors->dis_c,
    };
    uint32_t rgb888[4];
    for (int i = 0; i < 4; i++) {
        uint16_t c = rgb565[i];
        /* RGB565 → RGB888 with bit-replication for full 0..255 range. */
        uint32_t r5 = (c >> 11) & 0x1F;
        uint32_t g6 = (c >>  5) & 0x3F;
        uint32_t b5 = (c      ) & 0x1F;
        uint32_t r8 = (r5 << 3) | (r5 >> 2);
        uint32_t g8 = (g6 << 2) | (g6 >> 4);
        uint32_t b8 = (b5 << 3) | (b5 >> 2);
        rgb888[i] = (r8 << 16) | (g8 << 8) | b8;
    }
    lcd_set_overlay_clut(rgb888, 4);
}

static char str_buffer[128];

retro_gui_t gui;

/* Top Y/height of the list viewport (reduced when subfolder path strip is shown). */
static int gui_list_view_y0;
static int gui_list_view_h;

static bool gui_tab_is_in_rom_subfolder(const tab_t *tab)
{
    if (!tab || !tab->arg)
        return false;

    const retro_emulator_t *emu = (const retro_emulator_t *)tab->arg;
    return emu->browse_subpath[0] != '\0';
}

static void gui_list_begin_viewport(tab_t *tab)
{
    gui_list_view_y0 = LIST_Y_OFFSET;
    gui_list_view_h = LIST_HEIGHT;

    if (gui_tab_is_in_rom_subfolder(tab) && tab->status[0] != '\0')
    {
        int ph = i18n_get_text_height() + 4;
        uint16_t strip_bg = get_darken_pixel_d(curr_colors->main_c, curr_colors->bg_c, 45);

        odroid_overlay_draw_fill_rect(0, LIST_Y_OFFSET, LIST_WIDTH, ph, strip_bg);
        i18n_draw_text_line(6, LIST_Y_OFFSET + 2, LIST_WIDTH - 12, tab->status, curr_colors->sel_c, strip_bg, 0);

        gui_list_view_y0 = LIST_Y_OFFSET + ph;
        gui_list_view_h = LIST_HEIGHT - ph;
    }
}

void gui_event(gui_event_t event, tab_t *tab)
{
    if (tab->event_handler)
        (*tab->event_handler)(event, tab);
}

tab_t *gui_add_tab(const char *name, int16_t logo_idx, int16_t header_idx, void *arg, void *event_handler)
{
    tab_t *tab = rg_calloc(1, sizeof(tab_t));

    sprintf(tab->name, "%s", name);
    sprintf(tab->status, "Loading...");

    tab->event_handler = event_handler;
    tab->header_idx = header_idx;
    tab->logo_idx = logo_idx;
    tab->initialized = false;
    tab->is_empty = false;
    tab->arg = arg;

    gui.tabs[gui.tabcount++] = tab;

    //printf("gui_add_tab: Tab '%s' added at index %d\n", tab->name, gui.tabcount - 1);

    return tab;
}

void gui_init_tab(tab_t *tab)
{
    if (tab->initialized)
        return;

    tab->initialized = true;
    // tab->status[0] = 0;

#if COVERFLOW != 0
    /* setup JPEG decoder instance with 32bits aligned address */
    // reuse emulator buffer for JPEG decoder & DMA2 buffering
    // Direct access to DTCM is not allowed for DMA2D :(
    // We use emulator ram.
    if (pJPEG_Buffer == NULL)
        pJPEG_Buffer = (uint8_t *)ram_malloc(COVER_420_SIZE);
    if (pCover_Buffer == NULL)
        pCover_Buffer = (uint16_t *)ram_malloc(COVER_16BITS_SIZE);
    assert(JPEG_DecodeToBufferInit((uint32_t)pJPEG_Buffer, JPEG_BUFFER_SIZE) == 0);
    //printf("JPEG init done\n");
    /* -------------------------- */
#endif

    sprintf(str_buffer, "Sel.%.11s", tab->name);
    // tab->listbox.cursor = odroid_settings_int32_get(str_buffer, 0);
    tab_t *selected_tab = gui_get_tab(odroid_settings_MainMenuSelectedTab_get());
    if (tab->name == selected_tab->name)
    {
        tab->listbox.cursor = odroid_settings_MainMenuCursor_get();
    }

    gui_event(TAB_INIT, tab);

    tab->listbox.cursor = MIN(tab->listbox.cursor, tab->listbox.length - 1);
    tab->listbox.cursor = MAX(tab->listbox.cursor, 0);
}

void gui_refresh_tab(tab_t *tab)
{
    gui_event(TAB_REFRESH_LIST, tab);
}

tab_t *gui_get_tab(int index)
{
    return (index >= 0 && index < gui.tabcount) ? gui.tabs[index] : NULL;
}

tab_t *gui_get_current_tab()
{
    return gui_get_tab(gui.selected);
}

tab_t *gui_set_current_tab(int index)
{
    index %= gui.tabcount;

    if (index < 0)
        index += gui.tabcount;

    gui.selected = index;

    return gui_get_tab(gui.selected);
}

void gui_save_current_tab()
{
    tab_t *tab = gui_get_current_tab();

    odroid_settings_MainMenuCursor_set(tab->listbox.cursor);
    odroid_settings_MainMenuSelectedTab_set(gui.selected);
    if (tab->arg)
        odroid_settings_MainMenuBrowseSubpath_set(((retro_emulator_t *)tab->arg)->browse_subpath);
    odroid_settings_commit();
}

listbox_item_t *gui_get_selected_item(tab_t *tab)
{
    listbox_t *list = &tab->listbox;

    if (list->cursor >= 0 && list->cursor < list->length)
        return &list->items[list->cursor];

    return NULL;
}

static int list_comparator(const void *p, const void *q)
{
    return strcasecmp(((listbox_item_t *)p)->text, ((listbox_item_t *)q)->text);
}

void gui_sort_list(tab_t *tab, int sort_mode)
{
    if (tab->listbox.length == 0)
        return;

    qsort((void *)tab->listbox.items, tab->listbox.length, sizeof(listbox_item_t), list_comparator);
}

void gui_resize_list(tab_t *tab, int new_size)
{
    if (global_items == NULL)
    {
        // we will reuse the same buffer for all lists
        global_items = ram_malloc(1000 * sizeof(listbox_item_t));
    }
    int cur_size = tab->listbox.length;

    if (new_size == cur_size)
        return;

    if (new_size == 0)
    {
        tab->listbox.items = NULL;
    }
    else
    {
        tab->listbox.items = global_items; // We use the global buffer
        for (int i = cur_size; i < new_size; i++)
            memset(&tab->listbox.items[i], 0, sizeof(listbox_item_t));
    }

    tab->listbox.length = new_size;
    tab->listbox.cursor = MIN(tab->listbox.cursor, tab->listbox.length - 1);
    tab->listbox.cursor = MAX(tab->listbox.cursor, 0);

    printf("gui_resize_list: Resized list '%s' from %d to %d items\n", tab->name, cur_size, new_size);
}

void gui_scroll_list(tab_t *tab, scroll_mode_t mode)
{
    listbox_t *list = &tab->listbox;

    if (list->length == 0 || list->cursor > list->length)
    {
        return;
    }

    int cur_cursor = list->cursor;
    int old_cursor = list->cursor;

    if (mode == LINE_UP)
    {
        cur_cursor--;
    }
    else if (mode == LINE_DOWN)
    {
        cur_cursor++;
    }
    else if (mode == PAGE_UP)
    {
        char st = ((char *)list->items[cur_cursor].text)[0];
        int max = LIST_LINE_COUNT - 2;
        while (--cur_cursor > 0 && max-- > 0)
        {
            if (st != ((char *)list->items[cur_cursor].text)[0])
                break;
        }
    }
    else if (mode == PAGE_DOWN)
    {
        char st = ((char *)list->items[cur_cursor].text)[0];
        int max = LIST_LINE_COUNT - 2;
        while (++cur_cursor < list->length - 1 && max-- > 0)
        {
            if (st != ((char *)list->items[cur_cursor].text)[0])
                break;
        }
    }

    if (cur_cursor < 0)
        cur_cursor = list->length - 1;
    if (cur_cursor >= list->length)
        cur_cursor = 0;

    list->cursor = cur_cursor;

    if (cur_cursor != old_cursor)
    {
        gui_event(TAB_SCROLL, tab);
    }
}

void gui_redraw_callback()
{
    tab_t *tab = gui_get_current_tab();
    gui_draw_header(tab);
    gui_draw_status(tab);
    gui_draw_list(tab);
}

void gui_redraw()
{
    lcd_sleep_while_swap_pending();
    gui_redraw_callback();
    lcd_swap();
}

void gui_draw_navbar()
{
    for (int i = 0; i < gui.tabcount; i++)
    {
        retro_logo_image *logo = rg_get_logo(gui.tabs[i]->logo_idx);
        if (logo)
            odroid_display_write(i * IMAGE_LOGO_WIDTH, 0, IMAGE_LOGO_WIDTH, IMAGE_LOGO_HEIGHT, (const uint16_t*)logo);
    }
}

void gui_draw_header(tab_t *tab)
{

    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - IMAGE_BANNER_HEIGHT - 15, ODROID_SCREEN_WIDTH, 32, curr_colors->main_c);

    if (tab->header_idx > 0)
        odroid_overlay_draw_logo(8, ODROID_SCREEN_HEIGHT - IMAGE_BANNER_HEIGHT - 15 + 7, tab->header_idx, curr_colors->sel_c);

    if (tab->logo_idx) {
        retro_logo_image *img_logo = rg_get_logo(tab->logo_idx);
        if (img_logo) {
            int h = img_logo->height;
            h = (IMAGE_BANNER_HEIGHT - h) / 2;
            int w = h + img_logo->width;
            
            odroid_overlay_draw_logo(ODROID_SCREEN_WIDTH - w - 1, 
                                    ODROID_SCREEN_HEIGHT - IMAGE_BANNER_HEIGHT - 15 + h, 
                                    tab->logo_idx, get_shined_pixel(curr_colors->main_c, 25));
        }
    }

    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 15, ODROID_SCREEN_WIDTH, 1, curr_colors->sel_c);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 13, ODROID_SCREEN_WIDTH, 4, curr_colors->main_c);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 10, ODROID_SCREEN_WIDTH, 2, curr_colors->bg_c);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 8, ODROID_SCREEN_WIDTH, 2, curr_colors->main_c);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 6, ODROID_SCREEN_WIDTH, 2, curr_colors->bg_c);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 4, ODROID_SCREEN_WIDTH, 1, curr_colors->main_c);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 3, ODROID_SCREEN_WIDTH, 2, curr_colors->bg_c);
    odroid_overlay_draw_fill_rect(0, ODROID_SCREEN_HEIGHT - 1, ODROID_SCREEN_WIDTH, 1, curr_colors->main_c);
}

// void gui_draw_notice(tab_t *tab)
void gui_draw_notice(const char *text, uint16_t color)
{
    odroid_overlay_draw_text(CRC_X_OFFSET, CRC_Y_OFFSET, CRC_WIDTH, text, color, curr_colors->bg_c);
};

void gui_draw_status(tab_t *tab)
{
    odroid_overlay_draw_fill_rect(0, 0, ODROID_SCREEN_WIDTH, STATUS_HEIGHT, curr_colors->main_c);
    odroid_overlay_draw_fill_rect(0, 1, ODROID_SCREEN_WIDTH, 2, curr_colors->bg_c);
    odroid_overlay_draw_fill_rect(0, 4, ODROID_SCREEN_WIDTH, 2, curr_colors->bg_c);
    odroid_overlay_draw_fill_rect(0, 8, ODROID_SCREEN_WIDTH, 2, curr_colors->bg_c);

    odroid_overlay_draw_logo(8, 16, RG_LOGO_RGW, curr_colors->sel_c);

    odroid_battery_state_t battery_state = odroid_input_read_battery();
    if ((battery_state.percentage > 20) || ((get_elapsed_time() % 1000) < 800))
        odroid_overlay_draw_battery(battery_state, ODROID_SCREEN_WIDTH - 28, 17);

    bool show_battery_percentage =
        battery_state.state == ODROID_BATTERY_CHARGE_STATE_DISCHARGING ||
        battery_state.state == ODROID_BATTERY_CHARGE_STATE_FULL;
    int battery_percentage_width = 0;
    if (show_battery_percentage) {
        char header[10];
        snprintf(header, 10, "%u%%", battery_state.percentage);

        battery_percentage_width = i18n_get_text_width(header);
        i18n_draw_text_line(ODROID_SCREEN_WIDTH - 35 - battery_percentage_width, 16, 40, header, curr_colors->sel_c, curr_colors->main_c, 1);
    }

    odroid_overlay_clock(ODROID_SCREEN_WIDTH - 74 - (show_battery_percentage ? battery_percentage_width + 5 : 0), 17);
}

listbox_item_t *gui_get_selected_prior_item(tab_t *tab)
{
    listbox_t *list = &tab->listbox;

    int x = list->cursor - 1;
    if (x < 0)
        x = list->length - 1;

    if (x >= 0 && x < list->length)
        return &list->items[x];

    return NULL;
}

listbox_item_t *gui_get_selected_next_item(tab_t *tab)
{
    listbox_t *list = &tab->listbox;

    int x = list->cursor + 1;
    if (x >= list->length)
        x = 0;

    if (x >= 0 && x < list->length)
        return &list->items[x];

    return NULL;
}

listbox_item_t *gui_get_item_by_index(tab_t *tab, int *index)
{
    listbox_t *list = &tab->listbox;
    int x = *index;

    if (list->length == 0)
        return NULL;

    if (x < 0)
        x = (list->length + x) % (list->length);

    if (x >= list->length)
        x = x % (list->length);

    if (x >= 0 && x < list->length)
    {
        *index = x;
        return &list->items[x];
    }
    return NULL;
}

void gui_draw_item_postion_v(int posx, int starty, int endy, int cur, int size)
{
    sprintf(str_buffer, "%d", size);
    int len = strlen(str_buffer);
    sprintf(str_buffer, "%0*d/%0*d", len, cur, len, size);
    len = strlen(str_buffer);
    int height = len * odroid_overlay_get_font_size();
    uint16_t *dst_img = lcd_get_active_buffer();
    int posy = (cur * (endy - starty + 1 - height)) / (size + 1);
    //posy = (posy < 0) ? 0 : posy;

    for (int y = starty; y <= starty + posy; y++)
        dst_img[y * ODROID_SCREEN_WIDTH + posx] = get_darken_pixel_d(curr_colors->sel_c, curr_colors->bg_c, ((y - starty + 1) * 90) / posy + 10);
    for (int y = posy + starty + height; y <= endy; y++)
        dst_img[y * ODROID_SCREEN_WIDTH + posx] = get_darken_pixel_d(curr_colors->sel_c, curr_colors->bg_c, ((endy - y + 1) * 90) / (endy - starty - posy - height + 1) + 10);

    odroid_overlay_draw_fill_rect(
        posx - odroid_overlay_get_font_width() / 2 - 1,
        starty + posy - 1,
        odroid_overlay_get_font_size() + 2, height + 2, curr_colors->sel_c);
    odroid_overlay_draw_fill_rect(
        posx - odroid_overlay_get_font_width() / 2,
        starty + posy - 2,
        odroid_overlay_get_font_width(), 1, curr_colors->dis_c);
    odroid_overlay_draw_fill_rect(
        posx - odroid_overlay_get_font_width() / 2,
        starty + posy + height + 1,
        odroid_overlay_get_font_width(), 1, curr_colors->dis_c);

    for (int y = 0; y < len; y++)
        odroid_overlay_draw_text_line(
            posx - odroid_overlay_get_font_width() / 2,
            starty + posy + y * odroid_overlay_get_font_size(), //top
            odroid_overlay_get_font_width(),
            &str_buffer[y],
            curr_colors->bg_c,
            curr_colors->sel_c);
}

void gui_draw_simple_list(int posx, tab_t *tab)
{
    listbox_t *list = &tab->listbox;
    if (list->cursor >= 0 && list->cursor < list->length)
    {
        int font_height = i18n_get_text_height();
        int w = ODROID_SCREEN_WIDTH - posx - 12;
        listbox_item_t *item = &list->items[list->cursor];
        int h1 = gui_list_view_y0 + (gui_list_view_h - font_height) / 2;
        if (item)
            i18n_draw_text_line(posx, h1, w, list->items[list->cursor].text, curr_colors->sel_c, curr_colors->bg_c, 0);

        int index_next = list->cursor + 1;
        int index_proior = list->cursor - 1;
        int max_line = (gui_list_view_h - font_height) / font_height / 2;
        int h2 = h1;
        h1++;
        for (int i = 0; i < max_line; i++)
        {
            listbox_item_t *next_item = gui_get_item_by_index(tab, &index_next);
            h1 = h1 + font_height + max_line - i;
            h2 = h2 - font_height - max_line + i;
            if (h2 < gui_list_view_y0) //out range;
                break;
            if (next_item)
                i18n_draw_text_line(
                    posx,
                    h1,
                    w,
                    list->items[index_next].text,
                    get_darken_pixel_d(curr_colors->dis_c, curr_colors->bg_c, (max_line - i) * 100 / max_line),
                    curr_colors->bg_c,
                    0);
            index_next++;
            listbox_item_t *prior_item = gui_get_item_by_index(tab, &index_proior);
            if (prior_item)
                i18n_draw_text_line(
                    posx,
                    h2,
                    w,
                    list->items[index_proior].text,
                    get_darken_pixel_d(curr_colors->dis_c, curr_colors->bg_c, (max_line - i) * 100 / max_line),
                    curr_colors->bg_c,
                    0);
            index_proior--;
        }
        //draw currpostion
        gui_draw_item_postion_v(ODROID_SCREEN_WIDTH - 5, gui_list_view_y0 + 4, gui_list_view_y0 + gui_list_view_h - 4, list->cursor + 1, list->length);
    }
}

#if COVERFLOW != 0

#define MAX_COVERS 5
#define COVER_SIZE (10 * 1024)

typedef struct {
    char *rom_path;
    uint8_t *buffer;
} CoverCache;

static CoverCache cover_cache[MAX_COVERS] = {0};

static void initialize_cache()
{
    for (int i = 0; i < MAX_COVERS; i++) {
        if (!cover_cache[i].buffer) {
            cover_cache[i].buffer = ram_malloc(COVER_SIZE);
        }
    }
}

static uint8_t *get_coverfile(char *rom_path)
{
    static int next_cache_index = 0;
    
    initialize_cache();

    // Check if cover is present in cache
    for (int i = 0; i < MAX_COVERS; i++) {
        if (cover_cache[i].rom_path && strcmp(cover_cache[i].rom_path, rom_path) == 0) {
            return cover_cache[i].buffer;
        }
    }

    char *coverpath = odroid_system_get_path(ODROID_PATH_COVER_FILE, rom_path);
    FILE *file = fopen(coverpath, "rb");
    if (!file)
    {
        // No cover exists for this game
        free (coverpath);
        return NULL;
    }

    // Check that file can fit in buffer
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size > COVER_SIZE) {
        // file too big, ignore it
        fclose(file);
        free (coverpath);
        return NULL;
    }

    fread(cover_cache[next_cache_index].buffer, size, 1, file);
    fclose(file);
    free(coverpath);

    // If a previous file was cached, free text memory
    if (cover_cache[next_cache_index].rom_path) {
        free(cover_cache[next_cache_index].rom_path);
    }

    cover_cache[next_cache_index].rom_path = strdup(rom_path);

    int current_cache_index = next_cache_index;
    next_cache_index = (next_cache_index + 1) % MAX_COVERS;

    return cover_cache[current_cache_index].buffer;
}

static void draw_centered_local_text_line(uint16_t y_pos,
                                          const char *text,
                                          uint16_t x1,
                                          uint16_t x2,
                                          uint16_t color,
                                          uint16_t color_bg)
{
    int width = i18n_get_text_width(text);
    int x_pos = (x2 - x1) / 2 - width / 2;
    if (x_pos < 0)
        x_pos = 0;
    if (width > (x2 - x1))
        width = x2 - x1;

    i18n_draw_text_line(x_pos + x1, y_pos, width, text, color, color_bg, 0);
}

static const char *gui_no_cover_text_for_item(const listbox_item_t *item)
{
    if (!item || !item->arg)
        return curr_lang->s_No_Cover;

    if (rg_rom_list_arg_is_parent(item->arg))
    {
        if (item->text && item->text[0] != '\0')
            return item->text;
        return curr_lang->s_No_Cover;
    }

    const retro_emulator_file_t *file = (const retro_emulator_file_t *)item->arg;
    if (file->ext == NULL)
    {
        if (item->text && item->text[0] != '\0')
            return item->text;
        return curr_lang->s_No_Cover;
    }

    return curr_lang->s_No_Cover;
}

void gui_draw_item_postion_h(int posy, int startx, int endx, int cur, int size)
{
    sprintf(str_buffer, "%d", size);
    int len = strlen(str_buffer);
    sprintf(str_buffer, "%0*d/%0*d", len, cur, len, size);
    len = strlen(str_buffer);
    int width = len * odroid_overlay_get_font_width();
    uint16_t *dst_img = lcd_get_active_buffer();
    int posx = (cur * (endx - startx + 1 - width)) / (size + 1);

    for (int x = startx; x <= startx + posx; x++)
    {
        dst_img[(posy - 1) * ODROID_SCREEN_WIDTH + x] = get_darken_pixel_d(curr_colors->dis_c,curr_colors->bg_c, 100 - ((x - startx + 1) * 90) / posx);
        dst_img[(posy - 2) * ODROID_SCREEN_WIDTH + x] = get_darken_pixel_d(curr_colors->sel_c,curr_colors->bg_c, 100 - ((x - startx + 1) * 90) / posx);
    }
    for (int x = posx + startx + width; x <= endx; x++)
    {
        dst_img[(posy - 1) * ODROID_SCREEN_WIDTH + x] = get_darken_pixel_d(curr_colors->dis_c, curr_colors->bg_c,100 - ((endx - x + 1) * 90) / (endx - startx - posx - width + 1));
        dst_img[(posy - 2) * ODROID_SCREEN_WIDTH + x] = get_darken_pixel_d(curr_colors->sel_c, curr_colors->bg_c,100 - ((endx - x + 1) * 90) / (endx - startx - posx - width + 1));
    }

    odroid_overlay_draw_text_line(
        posx + startx,
        posy - odroid_overlay_get_font_size(), //top
        width,
        str_buffer,
        curr_colors->sel_c,
        curr_colors->bg_c);
}

static bool gui_get_cover_size(retro_emulator_file_t *file, uint32_t *cov_width, uint32_t *cov_height)
{
    uint32_t jpeg_cov_width = 0, jpeg_cov_height = 0;

    *cov_width = NOCOVER_WIDTH;
    *cov_height = NOCOVER_HEIGHT;

    if (file == NULL)
        return false;

    if (file->img_state == IMG_STATE_COVER)
    {
        if (JPEG_DecodeGetSize((uint32_t)(file->img_address), &jpeg_cov_width, &jpeg_cov_height) == 0)
        {
            *cov_width = jpeg_cov_width;
            *cov_height = jpeg_cov_height;
            return true;
        }
    }
    return false;
}

void gui_draw_coverlight_h(retro_emulator_file_t *file, int cover_position)
{
    int32_t cover_x = 0, cover_y = gui_list_view_y0 + 22;
    uint32_t cover_width = NOCOVER_WIDTH;
    uint32_t cover_height = NOCOVER_HEIGHT;

    static uint32_t nocover_width = NOCOVER_WIDTH;
    static uint32_t nocover_height = NOCOVER_HEIGHT;

    if (file == NULL)
        return;

    if (file->img_state != IMG_STATE_NO_COVER) {
        file->img_address = get_coverfile(file->path);
        if (file->img_address) {
            file->img_state = IMG_STATE_COVER;
        } else {
            // If there is no cover file, never try to load file again
            file->img_state = IMG_STATE_NO_COVER;
        }
    }

    if (file->img_state == IMG_STATE_COVER)
    {
        JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &cover_width, &cover_height, cover_light[cover_position + 2]);
        if (nocover_width > cover_width)
            nocover_width = cover_width;
        if (nocover_height > cover_height)
            nocover_height = cover_height;
    }
    else
    {
        cover_width = nocover_width;
        cover_height = nocover_height;
    }

    switch (cover_position)
    {
    // TOP LEFT Cover
    case -2:
    {
        cover_x = 0;
        cover_y += -20;
    }
    break;
    // MIDDLE LEFT Cover
    case -1:
    {
        cover_x = (GW_LCD_WIDTH - current_cover_width) / 2 - COVER_BORDER - (cover_width + 2 * COVER_BORDER) + 16;
        if (cover_x < 12)
            cover_x = 12;
        cover_y += (COVER_MAX_HEIGHT - cover_height) / 2 - 10;
    }
    break;
    // Current Cover
    case 0:
    {
        cover_x = (GW_LCD_WIDTH - cover_width) / 2 - COVER_BORDER;
        cover_y += (COVER_MAX_HEIGHT - cover_height) / 2;
    }
    break;
    // MIDDLE RIGHT Cover
    case 1:
    {
        cover_x = (GW_LCD_WIDTH + current_cover_width) / 2 + COVER_BORDER - 16;
        if ((cover_x + cover_width + 12) > GW_LCD_WIDTH)
            cover_x = GW_LCD_WIDTH - cover_width - 24;
        cover_y += (COVER_MAX_HEIGHT - cover_height) / 2 - 10;
    }
    break;
    // TOP RIGHT cover
    case 2:
    {
        cover_x = GW_LCD_WIDTH - cover_width - 2 * COVER_BORDER;
        cover_y += -20;
    }
    break;
    }

    /* no cover art, draw a grey box */
    if (file->img_state == IMG_STATE_NO_COVER)
        odroid_overlay_draw_fill_rect(cover_x + COVER_BORDER, cover_y + COVER_BORDER, cover_width, cover_height, get_darken_pixel(C_GRAY, 100 * cover_light[cover_position + 2] / 255));

    /* display the cover art */
    else
        odroid_display_write_rect(cover_x + COVER_BORDER, cover_y + COVER_BORDER, cover_width, cover_height, cover_width, pCover_Buffer);

    /* add decoration around the cover art */
    /* current cover */
    if (cover_position == 0)
    {
        odroid_overlay_draw_rect(cover_x, cover_y, cover_width + 2 * COVER_BORDER, cover_height + 2 * COVER_BORDER, COVER_BORDER, curr_colors->bg_c);
        odroid_overlay_draw_rect(2 + cover_x, 2 + cover_y, cover_width + 8, cover_height + 8, 2, curr_colors->sel_c);

        /* TODO add shadowing */
        //left side
        /*
        uint16_t *pix = lcd_get_active_buffer();
        int pix_pos=0;

        for ( int xs = cover_x -12 ; xs < cover_x; xs++)
            for ( int ys = cover_y-6 ; ys < cover_y+cover_height+6; ys++)
            {
                pix_pos= ys*GW_LCD_WIDTH + xs;
                pix[pix_pos] = get_darken_pixel(pix[pix_pos], 100 - 80*(xs - cover_x + 12)/12);                
            }

         for ( int xs = cover_x+cover_width+2*COVER_BORDER + 12 ; xs < cover_x+cover_width+2*COVER_BORDER; xs--)
            for ( int ys = cover_y-6 ; ys < cover_y+cover_height+6; ys++)
            {
                pix_pos= ys*GW_LCD_WIDTH + xs;
                pix[pix_pos] = get_darken_pixel(pix[pix_pos], 100 - 80*(xs - cover_x-cover_width-2*COVER_BORDER)/12);                
            }

            }

        for ( int xs = cover_x ; xs < cover_x+cover_width-12; xs++)
            for ( int ys = cover_y-6 ; ys < cover_y; ys++)
            {
                pix_pos= ys*GW_LCD_WIDTH + xs;
                pix[pix_pos] = get_darken_pixel(pix[pix_pos], 100 - 80*(ys - cover_y+6)/6);                
            } 
*/

        /* add game titleof the current cover art */
        snprintf(str_buffer, 128, "%s", file->name);
        draw_centered_local_text_line(169,
                                      str_buffer,
                                      0,
                                      ODROID_SCREEN_WIDTH,
                                      curr_colors->sel_c,
                                      curr_colors->bg_c);
    }
    /* other cover */
    else
    {
        odroid_overlay_draw_rect(cover_x + 4, cover_y + 4, cover_width + 4, cover_height + 4, 2, curr_colors->bg_c);
        odroid_overlay_draw_rect(5 + cover_x, 5 + cover_y, cover_width + 2, cover_height + 2, 1, get_darken_pixel_d(curr_colors->sel_c, curr_colors->bg_c, 100 * cover_light[cover_position + 2] / 255));
    }
}

void gui_draw_coverlight_v(retro_emulator_file_t *file, int cover_position)
{
    int32_t cover_x = 0, cover_y = 0;
    uint32_t cover_width = NOCOVER_WIDTH;
    uint32_t cover_height = NOCOVER_HEIGHT;

    static uint32_t nocover_width = NOCOVER_WIDTH;
    static uint32_t nocover_height = NOCOVER_HEIGHT;

    if (file == NULL)
        return;

    if (file->img_state != IMG_STATE_NO_COVER) {
        file->img_address = get_coverfile(file->path);
        if (file->img_address) {
            file->img_state = IMG_STATE_COVER;
        } else {
            // If there is no cover file, never try to load file again
            file->img_state = IMG_STATE_NO_COVER;
        }
    }

    if (file->img_state == IMG_STATE_COVER)
    {
        JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &cover_width, &cover_height, cover_light3[-cover_position]);
        if (nocover_width > cover_width)
            nocover_width = cover_width;
        if (nocover_height > cover_height)
            nocover_height = cover_height;
    }
    else
    {
        cover_width = nocover_width;
        cover_height = nocover_height;
    }

    switch (cover_position)
    {
    // upper
    case -2:
    {
        cover_x = 4;
        cover_y = gui_list_view_y0 + 8;
    }
    break;
    // middle
    case -1:
    {
        cover_x = 8; //16;
        cover_y = (GW_LCD_HEIGHT - HEADER_HEIGHT - current_cover_height - 2 * COVER_BORDER + gui_list_view_y0) / 2;
    }
    break;

    // current cover
    case 0:
    {
        cover_x = 12; //32;
        cover_y = GW_LCD_HEIGHT - HEADER_HEIGHT - cover_height - 2 * COVER_BORDER - 8;
    }
    break;
    }

    /* draw cover art or grey box */
    if (file->img_state == IMG_STATE_NO_COVER)
        odroid_overlay_draw_fill_rect(cover_x + COVER_BORDER, cover_y + COVER_BORDER, cover_width, cover_height, get_darken_pixel(C_GRAY, 100 * cover_light3[-cover_position] / 255));

    /* display the cover art */
    else
        odroid_display_write_rect(cover_x + COVER_BORDER, cover_y + COVER_BORDER, cover_width, cover_height, cover_width, pCover_Buffer);

    /* add decoration around the cover art */
    /* current cover */
    if (cover_position == 0)
    {
        odroid_overlay_draw_rect(cover_x, cover_y, cover_width + 2 * COVER_BORDER, cover_height + 2 * COVER_BORDER, COVER_BORDER, curr_colors->bg_c);
        odroid_overlay_draw_rect(2 + cover_x, 2 + cover_y, cover_width + 8, cover_height + 8, 2, curr_colors->sel_c);
    }
    /* other cover */
    else
    {
        odroid_overlay_draw_rect(cover_x + 4, cover_y + 4, cover_width + 4, cover_height + 4, 2, curr_colors->bg_c);
        odroid_overlay_draw_rect(5 + cover_x, 5 + cover_y, cover_width + 2, cover_height + 2, 1, get_darken_pixel_d(curr_colors->sel_c, curr_colors->bg_c, 100 * cover_light3[-cover_position] / 255));
    }
}

void gui_draw_coverflow_h(tab_t *tab) //------------
{
    retro_emulator_t *emu = (retro_emulator_t *)tab->arg;
    listbox_t *list = &tab->listbox;
    listbox_item_t *item = &list->items[list->cursor];
    retro_emulator_file_t *file = NULL;
    int font_height = i18n_get_text_height();
    uint32_t cover_height = emu->cover_height;
    uint32_t cover_width = emu->cover_width;
    if (cover_height == 0 || cover_width == 0)
    {
        if (item)
        {
            file = (retro_emulator_file_t *)item->arg;
            if (gui_get_cover_size(file, &cover_width, &cover_height))
            {
                emu->cover_height = cover_height;
                emu->cover_width = cover_width;
            }
        } else {
            cover_height = NOCOVER_HEIGHT;
            cover_width = NOCOVER_WIDTH;
        }
    }
    int r_width1 = cover_width * 5 / 8;
    int r_width2 = cover_width * 7 / 8;
    uint32_t jpeg_cover_width = cover_width;
    uint32_t jpeg_cover_height = cover_height;
    //left _|_1_|_2__||_m_||__2_|_1_|_ min 22 pixels space
    int space_width = 22;
    int p_width2 = (ODROID_SCREEN_WIDTH - cover_width - space_width) / 3;
    //p_width must big than 1;
    //space width than real width, draw full size;  7/8
    p_width2 = (p_width2 > r_width2) ? r_width2 : p_width2;
    int p_width1 = (ODROID_SCREEN_WIDTH - cover_width - space_width - p_width2 * 2) / 2;
    //space width than real width, draw full size;  5/8
    p_width1 = (p_width1 > r_width1) ? r_width1 : p_width1;
    int start_xpos = (ODROID_SCREEN_WIDTH - ((p_width2 + p_width1) * 2 + cover_width + space_width)) / 2;
    //fisrt left point pos getted, get fisrt top point;
    int p_height1 = cover_height * 5 / 8;
    int p_height2 = cover_height * 7 / 8;
    int v_space = gui_list_view_h - (cover_height + 6); //180-136=44-6=38-5=33-12=21,top 7,bot//top 11,bottom22
    int cover_top = gui_list_view_y0 + (v_space - font_height - 5 - 10) * 2 / 5 + 10;
    int p2_top = cover_top + (cover_height - p_height2) / 4 * 3;
    int p1_top = cover_top + (cover_height - p_height1) / 4 * 3;
    //let's start draw effect;
    uint16_t *dst_img = lcd_get_active_buffer();
    uint16_t max_y = ODROID_SCREEN_HEIGHT - HEADER_HEIGHT;

    //left1
    odroid_overlay_draw_rect(start_xpos + 1, cover_top + (cover_height - p_height1) / 4 * 3 - 2, p_width1 + 2, p_height1 + 4, 1, get_darken_pixel_d(curr_colors->dis_c, curr_colors->bg_c, 40));
    odroid_overlay_draw_fill_rect(start_xpos + p_width1 + 2, cover_top + (cover_height - p_height1) / 4 * 3 - 1, 1, p_height1 + 2, curr_colors->bg_c);
    odroid_overlay_draw_rect(start_xpos + p_width1 + 4, cover_top + (cover_height - p_height2) / 4 * 3 - 2, p_width2 + 2, p_height2 + 4, 1, get_darken_pixel_d(curr_colors->dis_c, curr_colors->bg_c, 80));
    odroid_overlay_draw_fill_rect(start_xpos + p_width1 + p_width2 + 5, cover_top + (cover_height - p_height2) / 4 * 3 - 1, 1, p_height2 + 2, curr_colors->bg_c);

    odroid_overlay_draw_rect(start_xpos + p_width1 + p_width2 + 8, cover_top - 3, cover_width + 6, cover_height + 6, 1, curr_colors->sel_c);
    odroid_overlay_draw_rect(start_xpos + p_width1 + p_width2 + 9, cover_top - 2, cover_width + 4, cover_height + 4, 1, curr_colors->dis_c);

    odroid_overlay_draw_rect(start_xpos + p_width1 + p_width2 + cover_width + 16, cover_top + (cover_height - p_height2) / 4 * 3 - 2, p_width2 + 2, p_height2 + 4, 1, get_darken_pixel_d(curr_colors->dis_c,curr_colors->bg_c, 80));
    odroid_overlay_draw_fill_rect(start_xpos + p_width1 + p_width2 + cover_width + 16, cover_top + (cover_height - p_height2) / 4 * 3 - 1, 1, p_height2 + 2, curr_colors->bg_c);
    odroid_overlay_draw_rect(start_xpos + p_width1 + p_width2 * 2 + cover_width + 19, cover_top + (cover_height - p_height1) / 4 * 3 - 2, p_width1 + 2, p_height1 + 4, 1, get_darken_pixel_d(curr_colors->dis_c,curr_colors->bg_c, 40));
    odroid_overlay_draw_fill_rect(start_xpos + p_width1 + p_width2 * 2 + cover_width + 19, cover_top + (cover_height - p_height1) / 4 * 3 - 1, 1, p_height1 + 2, curr_colors->bg_c);

    //shadow effect;
    odroid_overlay_draw_rect(start_xpos + p_width1 + p_width2 + 9, cover_top + cover_height + 3, cover_width + 4, 1, 1, curr_colors->bg_c + get_darken_pixel_d(curr_colors->dis_c,curr_colors->bg_c, 50));
    for (int y = 0; y < 15; y++)
    {
        dst_img[(p1_top + p_height1 + 2 + y) * ODROID_SCREEN_WIDTH + start_xpos + 1] = get_darken_pixel_d(curr_colors->dis_c, curr_colors->bg_c, 20 * (100 - y * 6) / 100);
        dst_img[(p2_top + p_height2 + 2 + y) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + 4] = get_darken_pixel_d(curr_colors->dis_c, curr_colors->bg_c ,40 * (100 - y * 6) / 100);

        if ((cover_top + cover_height + 3 + y) < max_y)
        {
            dst_img[(cover_top + cover_height + 3 + y) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + p_width2 + 8] = get_darken_pixel_d(curr_colors->sel_c, curr_colors->bg_c, 50 * (100 - y * 6) / 100);
            dst_img[(cover_top + cover_height + 3 + y) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + p_width2 + 9] = get_darken_pixel_d(curr_colors->dis_c, curr_colors->bg_c, 50 * (100 - y * 6) / 100);
            dst_img[(cover_top + cover_height + 3 + y) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + p_width2 + cover_width + 13] = get_darken_pixel_d(curr_colors->sel_c, curr_colors->bg_c, 50 * (100 - y * 6) / 100);
            dst_img[(cover_top + cover_height + 3 + y) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + p_width2 + cover_width + 12] = get_darken_pixel_d(curr_colors->dis_c, curr_colors->bg_c, 50 * (100 - y * 6) / 100);
        };

        dst_img[(p2_top + p_height2 + 2 + y) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + p_width2 * 2 + cover_width + 17] = get_darken_pixel_d(curr_colors->dis_c,curr_colors->bg_c, 40 * (100 - y * 6) / 100);
        dst_img[(p1_top + p_height1 + 2 + y) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 * 2 + p_width2 * 2 + cover_width + 20] = get_darken_pixel_d(curr_colors->dis_c, curr_colors->bg_c, 20 * (100 - y * 6) / 100);
    };

    if (list->cursor >= 0 && list->cursor < list->length)
        gui_draw_item_postion_h(cover_top - 1, start_xpos + p_width1 + p_width2 + 10, start_xpos + p_width1 + p_width2 + cover_width + 6, list->cursor + 1, list->length);
    else
        return;

    if (item) //current page
    {
        file = (retro_emulator_file_t *)item->arg;
        if (file->img_state != IMG_STATE_NO_COVER) {
            file->img_address = get_coverfile(file->path);
            if (file->img_address) {
                file->img_state = IMG_STATE_COVER;
            } else {
                // If there is no cover file, never try to load file again
                file->img_state = IMG_STATE_NO_COVER;
            }
        }
        if (file->img_state == IMG_STATE_NO_COVER)
        {
            draw_centered_local_text_line(cover_top + (cover_height - font_height) / 2, gui_no_cover_text_for_item(item), start_xpos + p_width1 + p_width2 + 10, start_xpos + p_width1 + p_width2 + 10 + cover_width, get_darken_pixel(curr_colors->main_c, 80), curr_colors->bg_c);
        }
        else
        {
            //draw the cover cenver
            JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
            odroid_display_write_rect(start_xpos + p_width1 + p_width2 + 11, cover_top, jpeg_cover_width, jpeg_cover_height, jpeg_cover_width, pCover_Buffer);
            //draw the cover shadow
            for (int y = 0; y <= 20; y++)
                if ((5 + cover_top + cover_height + y) < max_y)
                {
                    for (int x = 0; x < cover_width; x++)
                        dst_img[(5 + cover_top + cover_height + y) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + p_width2 + 11 + x] =
                            get_darken_pixel_d(pCover_Buffer[(cover_height - y - 1) * cover_width + x], curr_colors->bg_c, 50 * (100 - y * 5) / 100);
                };
        }
    }
    int index = list->cursor + 1;

    item = gui_get_item_by_index(tab, &index);
    if (item)
    {
        file = (retro_emulator_file_t *)item->arg;
        if (file->img_state != IMG_STATE_NO_COVER) {
            file->img_address = get_coverfile(file->path);
            if (file->img_address) {
                file->img_state = IMG_STATE_COVER;
            } else {
                // If there is no cover file, never try to load file again
                file->img_state = IMG_STATE_NO_COVER;
            }
        }
        if (file->img_state == IMG_STATE_NO_COVER)
        {
            draw_centered_local_text_line(cover_top + (cover_height - p_height2) / 4 * 3 + (p_height2 - font_height) / 2, gui_no_cover_text_for_item(item),
                                          start_xpos + p_width1 + p_width2 + cover_width + 17,
                                          start_xpos + p_width1 + p_width2 * 2 + cover_width + 17, get_darken_pixel(curr_colors->dis_c, 80), curr_colors->bg_c);
        }
        else
        {
            JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
            for (int y = 0; y < p_height2; y++)
                for (int x = 0; x < p_width2; x++)
                {
                    dst_img[(y + p2_top) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + p_width2 + jpeg_cover_width + 16 + x] =
                        get_darken_pixel(pCover_Buffer[(y * 8 / 7) * jpeg_cover_width + ((r_width2 - p_width2) + x) * 8 / 7], 40 + x * 40 / p_width2);
                    if (y > (p_height2 - 16))
                        dst_img[(p2_top + p_height2 + 2 + p_height2 - y) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + p_width2 + jpeg_cover_width + 16 + x] =
                            get_darken_pixel_d(pCover_Buffer[(y * 8 / 7) * jpeg_cover_width + ((r_width2 - p_width2) + x) * 8 / 7], curr_colors->bg_c, 40 * (16 - p_height2 + y) * 6 * (40 + x * 40 / p_width2) / 10000);
                };
        };
    };

    index = list->cursor - 1;
    item = gui_get_item_by_index(tab, &index);
    if (item)
    {
        file = (retro_emulator_file_t *)item->arg;
        if (file->img_state != IMG_STATE_NO_COVER) {
            file->img_address = get_coverfile(file->path);
            if (file->img_address) {
                file->img_state = IMG_STATE_COVER;
            } else {
                // If there is no cover file, never try to load file again
                file->img_state = IMG_STATE_NO_COVER;
            }
        }
        if (file->img_state == IMG_STATE_NO_COVER)
        {
            draw_centered_local_text_line(cover_top + (cover_height - p_height2) / 4 * 3 + (p_height2 - font_height) / 2, gui_no_cover_text_for_item(item),
                                          start_xpos + p_width1 + 5,
                                          start_xpos + p_width1 + p_width2 + 5, get_darken_pixel(curr_colors->dis_c, 80), curr_colors->bg_c);
        }
        else
        {
            JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
            for (int y = 0; y < p_height2; y++)
                for (int x = 0; x < p_width2; x++)
                {
                    dst_img[(y + p2_top) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + 6 + x] =
                        get_darken_pixel(pCover_Buffer[(y * 8 / 7) * jpeg_cover_width + x * 8 / 7], 80 - x * 40 / p_width2);
                    if (y > (p_height2 - 16))
                        dst_img[(p2_top + p_height2 + 2 + p_height2 - y) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + 6 + x] =
                            get_darken_pixel_d(pCover_Buffer[(y * 8 / 7) * jpeg_cover_width + x * 8 / 7], curr_colors->bg_c,40 * (16 - p_height2 + y) * 6 * (80 - x * 40 / p_width2) / 10000);
                };
        };
    };

    index = list->cursor + 2;
    item = gui_get_item_by_index(tab, &index);
    if (item)
    {
        file = (retro_emulator_file_t *)item->arg;
        if (file->img_state != IMG_STATE_NO_COVER) {
            file->img_address = get_coverfile(file->path);
            if (file->img_address) {
                file->img_state = IMG_STATE_COVER;
            } else {
                // If there is no cover file, never try to load file again
                file->img_state = IMG_STATE_NO_COVER;
            }
        }
        if (file->img_state == IMG_STATE_COVER)
        {
            JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
            for (int y = 0; y < p_height1; y++)
                for (int x = 0; x < p_width1; x++)
                {
                    dst_img[(y + p1_top) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + p_width2 * 2 + jpeg_cover_width + 19 + x] =
                        get_darken_pixel(pCover_Buffer[(y * 8 / 5) * jpeg_cover_width + ((r_width1 - p_width1) + x) * 8 / 5], 30 + x * 30 / p_width1);
                    if (y > (p_height1 - 12))
                        dst_img[(p1_top + p_height1 + 2 + p_height1 - y) * ODROID_SCREEN_WIDTH + start_xpos + p_width1 + p_width2 * 2 + jpeg_cover_width + 19 + x] =
                            get_darken_pixel_d(pCover_Buffer[(y * 8 / 5) * jpeg_cover_width + ((r_width1 - p_width1) + x) * 8 / 5],curr_colors->bg_c, 30 * (12 - p_height1 + y) * 12 * (30 + x * 30 / p_width1) / 10000);
                };
        };
    };

    index = list->cursor - 2;
    item = gui_get_item_by_index(tab, &index);
    if (item)
    {
        file = (retro_emulator_file_t *)item->arg;
        if (file->img_state != IMG_STATE_NO_COVER) {
            file->img_address = get_coverfile(file->path);
            if (file->img_address) {
                file->img_state = IMG_STATE_COVER;
            } else {
                // If there is no cover file, never try to load file again
                file->img_state = IMG_STATE_NO_COVER;
            }
        }
        if (file->img_state == IMG_STATE_COVER)
        {
            JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
            for (int y = 0; y < p_height1; y++)
                for (int x = 0; x < p_width1; x++)
                {
                    dst_img[(y + p1_top) * ODROID_SCREEN_WIDTH + start_xpos + 3 + x] =
                        get_darken_pixel(pCover_Buffer[(y * 8 / 5) * jpeg_cover_width + x * 8 / 5], 60 - x * 30 / p_width1);
                    if (y > (p_height1 - 12))
                        dst_img[(p1_top + p_height1 + 2 + p_height1 - y) * ODROID_SCREEN_WIDTH + start_xpos + 3 + x] =
                            get_darken_pixel_d(pCover_Buffer[(y * 8 / 5) * jpeg_cover_width + x * 8 / 5], curr_colors->bg_c,30 * (12 - p_height1 + y) * 12 * (60 - x * 30 / p_width1) / 10000);
                };
        };
    };

    index = list->cursor;
    item = gui_get_item_by_index(tab, &index);
    if (item)
    {
        if (rg_rom_list_arg_is_parent(item->arg))
            snprintf(str_buffer, 128, "%s", item->text ? item->text : "");
        else
        {
            file = (retro_emulator_file_t *)item->arg;
            snprintf(str_buffer, 128, "%s", file->name);
        }
        size_t width = i18n_get_text_width(str_buffer);
        if (width > (ODROID_SCREEN_WIDTH - 24))
            width = ODROID_SCREEN_WIDTH - 24;
        int cap_top = (max_y - (cover_top + cover_height + 4) - font_height) / 2;
        cap_top = (cap_top < 0) ? 0 : ((cap_top > 8) ? 8 : cap_top);
        cap_top = max_y - font_height - cap_top; 
        i18n_draw_text_line((ODROID_SCREEN_WIDTH - width) / 2, cap_top, width, str_buffer, curr_colors->sel_c, curr_colors->bg_c, 1);
    };
};

void gui_draw_coverflow_v(tab_t *tab, int start_posx) // ||||||||
{
    retro_emulator_t *emu = (retro_emulator_t *)tab->arg;
    int font_height = i18n_get_text_height();
    listbox_t *list = &tab->listbox;
    listbox_item_t *item = &list->items[list->cursor];
    retro_emulator_file_t *file = NULL;
    uint32_t cover_height = emu->cover_height;
    uint32_t cover_width = emu->cover_width;
    int space_height = 40;
    uint32_t jpeg_cover_width = cover_width;
    uint32_t jpeg_cover_height = cover_height;
    if (cover_height == 0 || cover_width == 0)
    {
        if (item)
        {
            file = (retro_emulator_file_t *)item->arg;
            if (gui_get_cover_size(file, &cover_width, &cover_height))
            {
                emu->cover_height = cover_height;
                emu->cover_width = cover_width;
            }
        } else {
            cover_height = NOCOVER_HEIGHT;
            cover_width = NOCOVER_WIDTH;
        }
    }
    //top ____|_|__|_(pl)__||_(main)_||__(pr)_|__|_|____ min 40;
    int p_height = (gui_list_view_h - cover_height - space_height) / 2;
    p_height = (p_height > cover_height) ? cover_height : p_height; //space width than real width, draw full size;
    p_height = p_height < 0 ? 0 : p_height;
    //real height = 32-8 = 24 //max = 136
    int start_ypos = gui_list_view_y0 + (gui_list_view_h - ((p_height * 2) + cover_height + space_height)) / 2 + 4;
    //fisrt top point pos getted;
    start_ypos = start_ypos < 0 ? 0 : start_ypos;
    int p_width1 = cover_width * 7 / 8;
    int p_width2 = cover_width * 5 / 8;
    int r_height = cover_height * 7 / 8;

    uint16_t *dst_img = lcd_get_active_buffer();

    odroid_overlay_draw_fill_rect(start_posx + (cover_width - p_width2) * 3 / 4 + 7, start_ypos + 4, p_width2 - 6, 1, get_darken_pixel_d(curr_colors->dis_c,curr_colors->bg_c, 60));
    odroid_overlay_draw_fill_rect(start_posx + (cover_width - p_width2) * 3 / 4 + 3, start_ypos + 6, p_width2, 1, get_darken_pixel_d(curr_colors->dis_c, curr_colors->bg_c,80));

    odroid_overlay_draw_rect(start_posx + (cover_width - p_width1) * 3 / 4 + 1, start_ypos + 9, p_width1 + 4, p_height + 2, 1, get_darken_pixel_d(curr_colors->dis_c, curr_colors->bg_c, 80));

    odroid_overlay_draw_rect(start_posx, start_ypos + 13 + p_height, cover_width + 6, cover_height + 6, 1, curr_colors->sel_c);
    odroid_overlay_draw_rect(start_posx + 1, start_ypos + 14 + p_height, cover_width + 4, cover_height + 4, 1, curr_colors->dis_c);

    odroid_overlay_draw_rect(start_posx + (cover_width - p_width1) * 3 / 4 + 1, start_ypos + p_height + cover_height + 21, p_width1 + 4, p_height + 2, 1, get_darken_pixel_d(curr_colors->dis_c,curr_colors->bg_c, 80));

    odroid_overlay_draw_fill_rect(start_posx + (cover_width - p_width2) * 3 / 4 + 3, start_ypos + 2 * p_height + cover_height + 25, p_width2, 1, get_darken_pixel_d(curr_colors->dis_c,curr_colors->bg_c, 80));
    odroid_overlay_draw_fill_rect(start_posx + (cover_width - p_width2) * 3 / 4 + 7, start_ypos + 2 * p_height + cover_height + 27, p_width2 - 6, 1, get_darken_pixel_d(curr_colors->dis_c,curr_colors->bg_c, 60));

    if (p_height)
    {
        odroid_overlay_draw_fill_rect(start_posx + (cover_width - p_width1) * 3 / 4 + 2, start_ypos + p_height + 10, p_width1 + 2, 1, curr_colors->bg_c);
        odroid_overlay_draw_fill_rect(start_posx + (cover_width - p_width1) * 3 / 4 + 2, start_ypos + p_height + cover_height + 21, p_width1 + 2, 1, curr_colors->bg_c);
    }

    if (item) //current page
    {
        file = (retro_emulator_file_t *)item->arg;
        if (file->img_state != IMG_STATE_NO_COVER) {
            file->img_address = get_coverfile(file->path);
            if (file->img_address) {
                file->img_state = IMG_STATE_COVER;
            } else {
                // If there is no cover file, never try to load file again
                file->img_state = IMG_STATE_NO_COVER;
            }
        }
        if (file->img_state == IMG_STATE_NO_COVER)
            draw_centered_local_text_line(start_ypos + p_height + 16 + (cover_height - font_height) / 2, gui_no_cover_text_for_item(item), start_posx + 3, start_posx + 3 + cover_width, get_darken_pixel(curr_colors->main_c, 80), curr_colors->bg_c);
        else
        {
            JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
            odroid_display_write_rect(start_posx + 3 + (cover_width - jpeg_cover_width) / 2, start_ypos + p_height + 16 + (cover_height - jpeg_cover_height) / 2, jpeg_cover_width, jpeg_cover_height, jpeg_cover_width, pCover_Buffer);
        };
    }
    if (p_height)
    {
        int index = list->cursor + 1;
        item = gui_get_item_by_index(tab, &index);
        if (item)
        {
            file = (retro_emulator_file_t *)item->arg;
            if (file->img_state != IMG_STATE_NO_COVER) {
                file->img_address = get_coverfile(file->path);
                if (file->img_address) {
                    file->img_state = IMG_STATE_COVER;
                } else {
                    // If there is no cover file, never try to load file again
                    file->img_state = IMG_STATE_NO_COVER;
                }
            }
            if (file->img_state == IMG_STATE_NO_COVER)
            {
                if (p_height > font_height)
                    draw_centered_local_text_line(start_ypos + p_height + cover_height + 21 + (p_height - font_height) / 2, gui_no_cover_text_for_item(item), start_posx + 3, start_posx + 3 + cover_width, get_darken_pixel(curr_colors->dis_c, 80), curr_colors->bg_c);
            }
            else
            {
                //draw the cover
                JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);
                for (int y = 0; y < p_height; y++)
                    for (int x = 0; x < p_width1; x++)
                        dst_img[(start_ypos + p_height + cover_height + 21 + y) * ODROID_SCREEN_WIDTH + start_posx + (cover_width - p_width1) * 3 / 4 + 3 + x] =
                            get_darken_pixel(pCover_Buffer[((r_height - p_height + y) * 8 / 7) * cover_width + x + x / 8], 40 + y * 20 / p_height);
            }
            index = list->cursor - 1;
            item = gui_get_item_by_index(tab, &index);
            if (item)
            {
                file = (retro_emulator_file_t *)item->arg;
                if (file->img_state != IMG_STATE_NO_COVER) {
                    file->img_address = get_coverfile(file->path);
                    if (file->img_address) {
                        file->img_state = IMG_STATE_COVER;
                    } else {
                        // If there is no cover file, never try to load file again
                        file->img_state = IMG_STATE_NO_COVER;
                    }
                }
                if (file->img_state == IMG_STATE_NO_COVER)
                {
                    if (p_height > font_height)
                        draw_centered_local_text_line(start_ypos + 11 + (p_height - font_height) / 2,
                                                      gui_no_cover_text_for_item(item),
                                                      start_posx + 3,
                                                      start_posx + 3 + cover_width,
                                                      get_darken_pixel(curr_colors->dis_c, 80),
                                                      curr_colors->bg_c);
                }
                else
                {
                    //draw the cover
                    JPEG_DecodeToBuffer((uint32_t)(file->img_address), (uint32_t)pCover_Buffer, &jpeg_cover_width, &jpeg_cover_height, 255);

                    for (int y = 0; y < p_height; y++)
                        for (int x = 0; x < p_width1; x++)
                            dst_img[(start_ypos + 11 + y) * ODROID_SCREEN_WIDTH + start_posx + (cover_width - p_width1) * 3 / 4 + 3 + x] =
                                get_darken_pixel(pCover_Buffer[(y + y / 8) * cover_width + x + x / 8], 60 - y * 20 / p_height);
                }
            }
        }
    }
    gui_draw_simple_list(start_posx + cover_width + 12, tab);
}

#endif

void gui_draw_list(tab_t *tab)
{
    gui_list_begin_viewport(tab);
    odroid_overlay_draw_fill_rect(0, gui_list_view_y0, LIST_WIDTH, gui_list_view_h, curr_colors->bg_c);

#if COVERFLOW != 0
    int theme_index = odroid_settings_theme_get();

    switch (theme_index)
    {
    case 3:
    {
        listbox_t *list = &tab->listbox;

        if (list->cursor >= 0 && list->cursor < list->length)
        {

            listbox_item_t *item = &list->items[list->cursor];

            /* get the current cover size */
            if (item)
                gui_get_cover_size((retro_emulator_file_t *)item->arg, &current_cover_width, &current_cover_height);

            int drawing[5] = {2, -2, 1, -1, 0};
            int idx;

            for (int cov_idx = 0; cov_idx < 5; cov_idx++)
            {
                idx = list->cursor + drawing[cov_idx];
                item = gui_get_item_by_index(tab, &idx);
                if (item)
                    gui_draw_coverlight_h((retro_emulator_file_t *)item->arg, drawing[cov_idx]);
            }

            //draw current postion over all items
            sprintf(str_buffer, "%d/%d", list->cursor + 1, list->length);
            //
            int width = strlen(str_buffer) * odroid_overlay_get_font_width();
            int count_y = gui_list_view_y0 + 11;
            odroid_overlay_draw_fill_rect((ODROID_SCREEN_WIDTH - width) / 2 - 2, count_y, width + 4, 12, curr_colors->sel_c);
            odroid_overlay_draw_fill_rect((ODROID_SCREEN_WIDTH - width) / 2 - 2 - 1, count_y + 1, 1, 10, get_darken_pixel(curr_colors->dis_c, 40));
            odroid_overlay_draw_fill_rect((ODROID_SCREEN_WIDTH - width) / 2 - 2 + width + 4, count_y + 1, 1, 10, get_darken_pixel(curr_colors->dis_c, 40));
            odroid_overlay_draw_fill_rect((ODROID_SCREEN_WIDTH - width) / 2 - 2 + 1, count_y - 1, width + 2, 1, get_darken_pixel(curr_colors->dis_c, 40));
            odroid_overlay_draw_text_line((ODROID_SCREEN_WIDTH - width) / 2, count_y + 2, width, str_buffer, curr_colors->bg_c, curr_colors->sel_c);
        }
    }
    break;
    case 4:
    {
        listbox_t *list = &tab->listbox;

        if (list->cursor >= 0 && list->cursor < list->length)
        {
            listbox_item_t *covitem = NULL;

            /* Get the widther cover,  Draw covers and get the current cover size */
            int idx;
            uint32_t pos_list = NOCOVER_WIDTH + 2 * COVER_BORDER;
            for (int cov_idx = -2; cov_idx < 1; cov_idx++)
            {
                idx = list->cursor + cov_idx;
                covitem = gui_get_item_by_index(tab, &idx);
                if (covitem)
                {
                    gui_get_cover_size((retro_emulator_file_t *)covitem->arg, &current_cover_width, &current_cover_height);
                    pos_list = pos_list < (16 + current_cover_width + 4 * (cov_idx + 2)) ? (16 + current_cover_width + 4 * (cov_idx + 2)) : pos_list;
                    gui_draw_coverlight_v((retro_emulator_file_t *)covitem->arg, cov_idx);
                }
            }

            gui_draw_simple_list(pos_list + 4, tab);
        }
    }

    break;
    case 2:
        gui_draw_coverflow_h(tab);
        break;
    case 1:
        gui_draw_coverflow_v(tab, 4);
        break;
    default:
        gui_draw_simple_list(10, tab);
    }

#else
    gui_draw_simple_list(10, tab);
#endif
}

void gui_jump_list(tab_t *tab, int offset)
{
    // block list jump function
    listbox_t *list = &tab->listbox;

    // If the list is empty or the jump is 0, we do nothing.
    if (list->length == 0 || offset == 0)
    {
        return;
    }

    int old_cursor = list->cursor;

    // We calculate the new cursor using modular arithmetic to avoid overflows
    int cur_cursor = (list->cursor + offset) % list->length;
    if (cur_cursor < 0) {
        cur_cursor += list->length;
    }

    list->cursor = cur_cursor;

    // We triggered the event so that the interface (covers, texts) would be updated.
    if (cur_cursor != old_cursor)
    {
        gui_event(TAB_SCROLL, tab);
    }
}
