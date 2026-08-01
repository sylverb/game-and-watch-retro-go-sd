#include <odroid_system.h>

#include <assert.h>
#include "gw_lcd.h"
#include "gw_linker.h"
#include "gw_buttons.h"
#include "gw_ofw.h"
#include "rom_manager.h"
#include "common.h"
#include "appid.h"
#include "bilinear.h"
#include "rg_i18n.h"
#include "cap32.h"
#include "main_amstrad.h"
#include "amstrad_loader.h"

#define AMSTRAD_FPS 50
#define AMSTRAD_SAMPLE_RATE 22050
#define AUDIO_BUFFER_LENGTH_AMSTRAD  (AMSTRAD_SAMPLE_RATE / AMSTRAD_FPS)

#define AMSTRAD_DISK_EXTENSION "dsk"
#define AMSTRAD_COMPRESSED_DISK_EXTENSION "cdk"

static void blit(uint8_t *src_fb, uint16_t *framebuffer);

typedef enum
{
    CPC_0,
    CPC_1,
    CPC_2,
    CPC_3,
    CPC_4,
    CPC_5,
    CPC_6,
    CPC_7,
    CPC_8,
    CPC_9,
    CPC_A,
    CPC_B,
    CPC_C,
    CPC_D,
    CPC_E,
    CPC_F,
    CPC_G,
    CPC_H,
    CPC_I,
    CPC_J,
    CPC_K,
    CPC_L,
    CPC_M,
    CPC_N,
    CPC_O,
    CPC_P,
    CPC_Q,
    CPC_R,
    CPC_S,
    CPC_T,
    CPC_U,
    CPC_V,
    CPC_W,
    CPC_X,
    CPC_Y,
    CPC_Z,
    CPC_a,
    CPC_b,
    CPC_c,
    CPC_d,
    CPC_e,
    CPC_f,
    CPC_g,
    CPC_h,
    CPC_i,
    CPC_j,
    CPC_k,
    CPC_l,
    CPC_m,
    CPC_n,
    CPC_o,
    CPC_p,
    CPC_q,
    CPC_r,
    CPC_s,
    CPC_t,
    CPC_u,
    CPC_v,
    CPC_w,
    CPC_x,
    CPC_y,
    CPC_z,
    CPC_AMPERSAND,
    CPC_ASTERISK,
    CPC_AT,
    CPC_BACKQUOTE,
    CPC_BACKSLASH,
    CPC_CAPSLOCK,
    CPC_CLR,
    CPC_COLON,
    CPC_COMMA,
    CPC_CONTROL,
    CPC_COPY,
    CPC_CPY_DOWN,
    CPC_CPY_LEFT,
    CPC_CPY_RIGHT,
    CPC_CPY_UP,
    CPC_CUR_DOWN,
    CPC_CUR_LEFT,
    CPC_CUR_RIGHT,
    CPC_CUR_UP,
    CPC_CUR_ENDBL,
    CPC_CUR_HOMELN,
    CPC_CUR_ENDLN,
    CPC_CUR_HOMEBL,
    CPC_DBLQUOTE,
    CPC_DEL,
    CPC_DOLLAR,
    CPC_ENTER,
    CPC_EQUAL,
    CPC_ESC,
    CPC_EXCLAMATN,
    CPC_F0,
    CPC_F1,
    CPC_F2,
    CPC_F3,
    CPC_F4,
    CPC_F5,
    CPC_F6,
    CPC_F7,
    CPC_F8,
    CPC_F9,
    CPC_FPERIOD,
    CPC_GREATER,
    CPC_HASH,
    CPC_LBRACKET,
    CPC_LCBRACE,
    CPC_LEFTPAREN,
    CPC_LESS,
    CPC_LSHIFT,
    CPC_MINUS,
    CPC_PERCENT,
    CPC_PERIOD,
    CPC_PIPE,
    CPC_PLUS,
    CPC_POUND,
    CPC_POWER,
    CPC_QUESTION,
    CPC_QUOTE,
    CPC_RBRACKET,
    CPC_RCBRACE,
    CPC_RETURN,
    CPC_RIGHTPAREN,
    CPC_RSHIFT,
    CPC_SEMICOLON,
    CPC_SLASH,
    CPC_SPACE,
    CPC_TAB,
    CPC_UNDERSCORE,
    CPC_J0_UP,
    CPC_J0_DOWN,
    CPC_J0_LEFT,
    CPC_J0_RIGHT,
    CPC_J0_FIRE1,
    CPC_J0_FIRE2,
    CPC_J1_UP,
    CPC_J1_DOWN,
    CPC_J1_LEFT,
    CPC_J1_RIGHT,
    CPC_J1_FIRE1,
    CPC_J1_FIRE2,
    CPC_ES_NTILDE,
    CPC_ES_nTILDE,
    CPC_ES_PESETA,
    CPC_FR_eACUTE,
    CPC_FR_eGRAVE,
    CPC_FR_cCEDIL,
    CPC_FR_aGRAVE,
    CPC_FR_uGRAVE,
    CAP32_FPS,
    CAP32_JOY,
    CAP32_INCY,
    CAP32_DECY,
    CAP32_RENDER,
    CAP32_LOAD,
    CAP32_SAVE,
    CAP32_RESET,
    CAP32_AUTOFIRE,
    CAP32_INCFIRE,
    CAP32_DECFIRE,
    CAP32_SCREEN,
} CPC_KEYS;

static char palette_name[7];
static int selected_palette_index = 0;

static char controls_name[10];
static int selected_controls_index = 0;

static char disk_name[128];
static char current_disk_path[255] = {0};

char DISKA_NAME[256] = "\0";
char DISKB_NAME[256] = "\0";
char cart_name[16] = "\0";
int emu_status;

static odroid_gamepad_state_t previous_joystick_state;
int amstrad_button_left_key = CPC_J0_LEFT;
int amstrad_button_right_key = CPC_J0_RIGHT;
int amstrad_button_up_key = CPC_J0_UP;
int amstrad_button_down_key = CPC_J0_DOWN;
int amstrad_button_a_key = CPC_J0_FIRE1;
int amstrad_button_b_key = CPC_J0_FIRE2;
int amstrad_button_game_key = CPC_SPACE;
int amstrad_button_time_key = CPC_RETURN;
int amstrad_button_start_key = CPC_SPACE;
int amstrad_button_select_key = CPC_RETURN;

static char game_button_name[10];
static char time_button_name[10];
static char start_button_name[10];
static char select_button_name[10];
static char a_button_name[10];
static char b_button_name[10];

#define RELEASE_KEY_DELAY 5
static int selected_key_index = 0;
static char key_name[10];
static struct amstrad_key_info *pressed_key = NULL;
static struct amstrad_key_info *release_key = NULL;
static int release_key_delay = RELEASE_KEY_DELAY;
int SHIFTON = 0;

uint8_t amstrad_framebuffer[CPC_SCREEN_WIDTH*CPC_SCREEN_HEIGHT];

char loader_buffer[512];

static const uint8_t IMG_DISKETTE[] = {
    0x00, 0x00, 0x00, 0x3F, 0xFF, 0xE0, 0x7C, 0x00, 0x70, 0x7C, 0x03, 0x78,
    0x7C, 0x03, 0x7C, 0x7C, 0x03, 0x7E, 0x7C, 0x00, 0x7E, 0x7F, 0xFF, 0xFE,
    0x7F, 0xFF, 0xFE, 0x7F, 0xFF, 0xFE, 0x7F, 0xFF, 0xFE, 0x7F, 0xFF, 0xFE,
    0x7F, 0xFF, 0xFE, 0x7E, 0x00, 0x7E, 0x7C, 0x00, 0x3E, 0x7C, 0x00, 0x3E,
    0x7D, 0xFF, 0xBE, 0x7C, 0x00, 0x3E, 0x7C, 0x00, 0x3E, 0x7D, 0xFF, 0xBE,
    0x7C, 0x00, 0x3E, 0x7C, 0x00, 0x3E, 0x3F, 0xFF, 0xFC, 0x00, 0x00, 0x00,
};

static bool auto_key = false;
#define AUDIO_BUFFER_LENGTH_AM (AMSTRAD_SAMPLE_RATE / AMSTRAD_FPS)
static int16_t soundBuffer[AUDIO_BUFFER_LENGTH_AM];
extern void amstrad_set_volume(uint8_t volume);
static const uint8_t volume_table[ODROID_AUDIO_VOLUME_MAX + 1] = {
    0,
    3,
    6,
    15,
    23,
    35,
    46,
    60,
    80,
    100,
};

static char *headerString = "AMST0000";

extern int cap32_save_state(FILE *file);
extern int cap32_load_state(FILE *file);

static bool SaveState(const char *savePathName) {
    int selected_disk_index = 0; // Dummy value for compatibility with old savestates
    // Show disk icon when saving state
    uint16_t *dest = lcd_get_inactive_buffer();
    uint16_t idx = 0;
    for (uint8_t i = 0; i < 24; i++) {
        for (uint8_t j = 0; j < 24; j++) {
        if (IMG_DISKETTE[idx / 8] & (1 << (7 - idx % 8))) {
            dest[274 + j + GW_LCD_WIDTH * (2 + i)] = 0xFFFF;
        }
        idx++;
        }
    }

    FILE *file = fopen(savePathName, "wb");
    if (file == NULL) {
        return false;
    }
    fwrite(headerString, 1, 8, file);
    cap32_save_state(file);
    fwrite(&selected_palette_index, 4, 1, file);
    fwrite(&selected_disk_index, 4, 1, file);
    fwrite(&selected_controls_index, 4, 1, file);
    fwrite(&selected_key_index, 4, 1, file);
    fwrite(&amstrad_button_a_key, 4, 1, file);
    fwrite(&amstrad_button_b_key, 4, 1, file);
    fwrite(&amstrad_button_game_key, 4, 1, file);
    fwrite(&amstrad_button_time_key, 4, 1, file);
    fwrite(&amstrad_button_start_key, 4, 1, file);
    fwrite(&amstrad_button_select_key, 4, 1, file);
    fwrite(current_disk_path, 255, 1, file);
    fclose(file);

    return true;
}

static bool LoadState(const char *savePathName) {
    FILE *file = fopen(savePathName, "rb");
    if (file == NULL) {
        return false;
    }

    unsigned char readin_header[8] = {0};
    fread(readin_header, 1, 8, file);
    // Check for header
    if (memcmp(headerString, readin_header, sizeof(readin_header)) == 0) { 
        int selected_disk_index; // Dummy variable for compatibility with old savestates
        cap32_load_state(file);
        fread(&selected_palette_index, 4, 1, file);
        fread(&selected_disk_index, 4, 1, file);
        fread(&selected_controls_index, 4, 1, file);
        fread(&selected_key_index, 4, 1, file);
        fread(&amstrad_button_a_key, 4, 1, file);
        fread(&amstrad_button_b_key, 4, 1, file);
        fread(&amstrad_button_game_key, 4, 1, file);
        fread(&amstrad_button_time_key, 4, 1, file);
        fread(&amstrad_button_start_key, 4, 1, file);
        fread(&amstrad_button_select_key, 4, 1, file);
        if (fread(current_disk_path, 255, 1, file) > 0) {
            detach_disk(0);
            attach_disk(current_disk_path, 0);
        }
    }
    fclose(file);
    return true;
}

static void *Screenshot()
{
    lcd_wait_for_vblank();

    lcd_clear_active_buffer();

    blit(amstrad_framebuffer, lcd_get_active_buffer());

    return lcd_get_active_buffer();
}

unsigned int *amstrad_getScreenPtr()
{
    return (unsigned int *)amstrad_framebuffer;
}

struct amstrad_key_info
{
    int key_id;
    const char *name;
    bool auto_release;
};

struct amstrad_key_info amstrad_keyboard[] = {
    {CPC_F0, "F0", true},
    {CPC_F1, "F1", true},
    {CPC_F2, "F2", true},
    {CPC_F3, "F3", true},
    {CPC_F4, "F4", true},
    {CPC_F5, "F5", true},
    {CPC_F6, "F6", true},
    {CPC_F7, "F7", true},
    {CPC_F8, "F8", true},
    {CPC_F9, "F9", true},
    {CPC_FPERIOD, ".", true},
    {CPC_ENTER, "Enter", true},
    {CPC_RETURN, "Return", true},
    {CPC_SPACE, "Space", true},
    {CPC_ESC, "ESC", true},
    {CPC_TAB, "TAB", true},
    {CPC_CAPSLOCK, "CAPSLOCK", true},
    {CPC_LSHIFT, "L Shift", false},
    {CPC_RSHIFT, "R Shift", false},
    {CPC_CONTROL, "Control", false},
    {CPC_COPY, "COPY", true},
    {CPC_CLR, "CLR", true},
    {CPC_DEL, "DEL", true},
    {CPC_MINUS, "-", true},
    {CPC_POWER, "^", true},
    {CPC_1, "1/!", true},
    {CPC_2, "2/\"", true},
    {CPC_3, "3/#", true},
    {CPC_4, "4/$", true},
    {CPC_5, "5/\%", true},
    {CPC_6, "6/&", true},
    {CPC_7, "7/'", true},
    {CPC_8, "8/(", true},
    {CPC_9, "9/)", true},
    {CPC_0, "0/_", true},
    {CPC_a, "a", true},
    {CPC_b, "b", true},
    {CPC_c, "c", true},
    {CPC_d, "d", true},
    {CPC_e, "e", true},
    {CPC_f, "f", true},
    {CPC_g, "g", true},
    {CPC_h, "h", true},
    {CPC_i, "i", true},
    {CPC_j, "j", true},
    {CPC_k, "k", true},
    {CPC_l, "l", true},
    {CPC_m, "m", true},
    {CPC_n, "n", true},
    {CPC_o, "o", true},
    {CPC_p, "p", true},
    {CPC_q, "q", true},
    {CPC_r, "r", true},
    {CPC_s, "s", true},
    {CPC_t, "t", true},
    {CPC_u, "u", true},
    {CPC_v, "v", true},
    {CPC_w, "w", true},
    {CPC_x, "x", true},
    {CPC_y, "y", true},
    {CPC_z, "z", true},
    {CPC_J0_FIRE1, "JOY1 A", true},
    {CPC_J0_FIRE2, "JOY1 B", true},
};

static bool update_game_button_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(amstrad_keyboard) / sizeof(amstrad_keyboard[0]) - 1;
    int amstrad_button_key_index = 0;

    // find index for current key
    for (int i = 0; i <= max_index; i++) {
        if (amstrad_keyboard[i].key_id == amstrad_button_game_key) {
            amstrad_button_key_index = i;
            break;
        }
    }

    if (event == ODROID_DIALOG_PREV) {
        amstrad_button_key_index = amstrad_button_key_index > 0 ? amstrad_button_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        amstrad_button_key_index = amstrad_button_key_index < max_index ? amstrad_button_key_index + 1 : 0;
    }

    amstrad_button_game_key = amstrad_keyboard[amstrad_button_key_index].key_id;
    strcpy(option->value, amstrad_keyboard[amstrad_button_key_index].name);
    return event == ODROID_DIALOG_ENTER;
}

static bool update_time_button_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(amstrad_keyboard) / sizeof(amstrad_keyboard[0]) - 1;
    int amstrad_button_key_index = 0;

    // find index for current key
    for (int i = 0; i <= max_index; i++) {
        if (amstrad_keyboard[i].key_id == amstrad_button_time_key) {
            amstrad_button_key_index = i;
            break;
        }
    }

    if (event == ODROID_DIALOG_PREV) {
        amstrad_button_key_index = amstrad_button_key_index > 0 ? amstrad_button_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        amstrad_button_key_index = amstrad_button_key_index < max_index ? amstrad_button_key_index + 1 : 0;
    }

    amstrad_button_time_key = amstrad_keyboard[amstrad_button_key_index].key_id;
    strcpy(option->value, amstrad_keyboard[amstrad_button_key_index].name);
    return event == ODROID_DIALOG_ENTER;
}

static bool update_start_button_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(amstrad_keyboard) / sizeof(amstrad_keyboard[0]) - 1;
    int amstrad_button_key_index = 0;

    // find index for current key
    for (int i = 0; i <= max_index; i++) {
        if (amstrad_keyboard[i].key_id == amstrad_button_start_key) {
            amstrad_button_key_index = i;
            break;
        }
    }

    if (event == ODROID_DIALOG_PREV) {
        amstrad_button_key_index = amstrad_button_key_index > 0 ? amstrad_button_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        amstrad_button_key_index = amstrad_button_key_index < max_index ? amstrad_button_key_index + 1 : 0;
    }

    amstrad_button_start_key = amstrad_keyboard[amstrad_button_key_index].key_id;
    strcpy(option->value, amstrad_keyboard[amstrad_button_key_index].name);
    return event == ODROID_DIALOG_ENTER;
}

static bool update_select_button_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(amstrad_keyboard) / sizeof(amstrad_keyboard[0]) - 1;
    int amstrad_button_key_index = 0;

    // find index for current key
    for (int i = 0; i <= max_index; i++) {
        if (amstrad_keyboard[i].key_id == amstrad_button_select_key) {
            amstrad_button_key_index = i;
            break;
        }
    }

    if (event == ODROID_DIALOG_PREV) {
        amstrad_button_key_index = amstrad_button_key_index > 0 ? amstrad_button_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        amstrad_button_key_index = amstrad_button_key_index < max_index ? amstrad_button_key_index + 1 : 0;
    }

    amstrad_button_select_key = amstrad_keyboard[amstrad_button_key_index].key_id;
    strcpy(option->value, amstrad_keyboard[amstrad_button_key_index].name);
    return event == ODROID_DIALOG_ENTER;
}

static bool update_a_button_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(amstrad_keyboard) / sizeof(amstrad_keyboard[0]) - 1;
    int amstrad_button_key_index = 0;

    // find index for current key
    for (int i = 0; i <= max_index; i++) {
        if (amstrad_keyboard[i].key_id == amstrad_button_a_key) {
            amstrad_button_key_index = i;
            break;
        }
    }

    if (event == ODROID_DIALOG_PREV) {
        amstrad_button_key_index = amstrad_button_key_index > 0 ? amstrad_button_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        amstrad_button_key_index = amstrad_button_key_index < max_index ? amstrad_button_key_index + 1 : 0;
    }

    amstrad_button_a_key = amstrad_keyboard[amstrad_button_key_index].key_id;
    strcpy(option->value, amstrad_keyboard[amstrad_button_key_index].name);
    return event == ODROID_DIALOG_ENTER;
}

static bool update_b_button_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(amstrad_keyboard) / sizeof(amstrad_keyboard[0]) - 1;
    int amstrad_button_key_index = 0;

    // find index for current key
    for (int i = 0; i <= max_index; i++) {
        if (amstrad_keyboard[i].key_id == amstrad_button_b_key) {
            amstrad_button_key_index = i;
            break;
        }
    }

    if (event == ODROID_DIALOG_PREV) {
        amstrad_button_key_index = amstrad_button_key_index > 0 ? amstrad_button_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        amstrad_button_key_index = amstrad_button_key_index < max_index ? amstrad_button_key_index + 1 : 0;
    }

    amstrad_button_b_key = amstrad_keyboard[amstrad_button_key_index].key_id;
    strcpy(option->value, amstrad_keyboard[amstrad_button_key_index].name);
    return event == ODROID_DIALOG_ENTER;
}

static bool update_keyboard_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(amstrad_keyboard) / sizeof(amstrad_keyboard[0]) - 1;

    if (event == ODROID_DIALOG_PREV)
    {
        selected_key_index = selected_key_index > 0 ? selected_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT)
    {
        selected_key_index = selected_key_index < max_index ? selected_key_index + 1 : 0;
    }

    if (vkbd_key_state(cpc_kbd[amstrad_keyboard[selected_key_index].key_id])) {
        // If key is pressed, add a * in front of key name
        option->value[0] = '*';
        strcpy(option->value+1, amstrad_keyboard[selected_key_index].name);
    } else {
        strcpy(option->value, amstrad_keyboard[selected_key_index].name);
    }

    if (event == ODROID_DIALOG_ENTER)
    {
        pressed_key = &amstrad_keyboard[selected_key_index];
    }
    return event == ODROID_DIALOG_ENTER;
}

static bool update_disk_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    char new_game_name[255];

    if (event == ODROID_DIALOG_PREV) {
        rg_storage_get_adjacent_files(current_disk_path, new_game_name, NULL);
        strcpy(current_disk_path, new_game_name);
        detach_disk(0);
        attach_disk(current_disk_path, 0);
    }
    if (event == ODROID_DIALOG_NEXT) {
        rg_storage_get_adjacent_files(current_disk_path, NULL, new_game_name);
        strcpy(current_disk_path, new_game_name);
        detach_disk(0);
        attach_disk(current_disk_path, 0);
    }
    strcpy(option->value, rg_basename(current_disk_path));
    return event == ODROID_DIALOG_ENTER;
}

static bool update_palette_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    const char *palette_names[] = {
        curr_lang->s_amd_palette_Color,curr_lang->s_amd_palette_Green, curr_lang->s_amd_palette_Grey};

    int max = 2;

    if (event == ODROID_DIALOG_PREV) selected_palette_index = selected_palette_index > 0 ? selected_palette_index - 1 : max;
    if (event == ODROID_DIALOG_NEXT) selected_palette_index = selected_palette_index < max ? selected_palette_index + 1 : 0;

    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
        cap32_set_palette(selected_palette_index);
    }

    strcpy(option->value, palette_names[selected_palette_index]);

    return event == ODROID_DIALOG_ENTER;
}

static bool update_controls_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    const char *controls_names[] = {
        curr_lang->s_amd_Controls_Joystick, curr_lang->s_amd_Controls_Keyboard};
    int max = 1;

    if (event == ODROID_DIALOG_PREV) selected_controls_index = selected_controls_index > 0 ? selected_controls_index - 1 : max;
    if (event == ODROID_DIALOG_NEXT) selected_controls_index = selected_controls_index < max ? selected_controls_index + 1 : 0;

    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
        switch (selected_controls_index)
        {
        case 0: // Joystick
            amstrad_button_left_key = CPC_J0_LEFT;
            amstrad_button_right_key = CPC_J0_RIGHT;
            amstrad_button_up_key = CPC_J0_UP;
            amstrad_button_down_key = CPC_J0_DOWN;
            amstrad_button_a_key = CPC_J0_FIRE1;
            amstrad_button_b_key = CPC_J0_FIRE2;
            amstrad_button_game_key = CPC_SPACE;
            amstrad_button_time_key = CPC_RETURN;
            amstrad_button_start_key = CPC_SPACE;
            amstrad_button_select_key = CPC_RETURN;
            break;
        case 1: // Keyboard
            amstrad_button_left_key = CPC_z;
            amstrad_button_right_key = CPC_x;
            amstrad_button_up_key = CPC_o;
            amstrad_button_down_key = CPC_k;
            amstrad_button_a_key = CPC_SPACE;
            amstrad_button_b_key = CPC_RETURN;
            amstrad_button_game_key = CPC_1;
            amstrad_button_time_key = CPC_2;
            amstrad_button_start_key = CPC_3;
            amstrad_button_select_key = CPC_4;
            break;
        }
    }

    strcpy(option->value, controls_names[selected_controls_index]);

    return event == ODROID_DIALOG_ENTER;
}

static void createOptionMenu(odroid_dialog_choice_t *options)
{
    int index = 0;
    options[index].id = 100;
    options[index].label = curr_lang->s_Palette;
    options[index].value = palette_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_palette_cb;
    index++;
    options[index].id = 100;
    options[index].label = curr_lang->s_amd_Change_Dsk;
    options[index].value = disk_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_disk_cb;
    index++;
    options[index].id = 100;
    options[index].label = curr_lang->s_amd_Controls;
    options[index].value = controls_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_controls_cb;
    index++;
    options[index].id = 100;
    options[index].label = curr_lang->s_amd_game_Button;
    options[index].value = game_button_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_game_button_cb;
    index++;
    options[index].id = 100;
    options[index].label = curr_lang->s_amd_time_Button;
    options[index].value = time_button_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_time_button_cb;
    index++;
    if (!get_ofw_is_mario()) {
        options[index].id = 100;
        options[index].label = curr_lang->s_amd_start_Button;
        options[index].value = start_button_name;
        options[index].enabled = 1;
        options[index].update_cb = &update_start_button_cb;
        index++;
        options[index].id = 100;
        options[index].label = curr_lang->s_amd_select_Button;
        options[index].value = select_button_name;
        options[index].enabled = 1;
        options[index].update_cb = &update_select_button_cb;
        index++;
    }
    options[index].id = 100;
    options[index].label = curr_lang->s_amd_A_Button;
    options[index].value = a_button_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_a_button_cb;
    index++;
    options[index].id = 100;
    options[index].label = curr_lang->s_amd_B_Button;
    options[index].value = b_button_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_b_button_cb;
    index++;
    options[index].id = 100;
    options[index].label = curr_lang->s_amd_Press_Key;
    options[index].value = key_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_keyboard_cb;
    index++;
    options[index].id = 0x0F0F0F0F;
    options[index].label = "LAST";
    options[index].value = "LAST";
    options[index].enabled = 0xFFFF;
    options[index].update_cb = NULL;
}

static void amstrad_input_update(odroid_gamepad_state_t *joystick)
{
    if ((joystick->values[ODROID_INPUT_LEFT]) && !previous_joystick_state.values[ODROID_INPUT_LEFT])
    {
        vkbd_key(cpc_kbd[amstrad_button_left_key], 1);
    }
    else if (!(joystick->values[ODROID_INPUT_LEFT]) && previous_joystick_state.values[ODROID_INPUT_LEFT])
    {
        vkbd_key(cpc_kbd[amstrad_button_left_key], 0);
    }
    if ((joystick->values[ODROID_INPUT_RIGHT]) && !previous_joystick_state.values[ODROID_INPUT_RIGHT])
    {
        vkbd_key(cpc_kbd[amstrad_button_right_key], 1);
    }
    else if (!(joystick->values[ODROID_INPUT_RIGHT]) && previous_joystick_state.values[ODROID_INPUT_RIGHT])
    {
        vkbd_key(cpc_kbd[amstrad_button_right_key], 0);
    }
    if ((joystick->values[ODROID_INPUT_UP]) && !previous_joystick_state.values[ODROID_INPUT_UP])
    {
        vkbd_key(cpc_kbd[amstrad_button_up_key], 1);
    }
    else if (!(joystick->values[ODROID_INPUT_UP]) && previous_joystick_state.values[ODROID_INPUT_UP])
    {
        vkbd_key(cpc_kbd[amstrad_button_up_key], 0);
    }
    if ((joystick->values[ODROID_INPUT_DOWN]) && !previous_joystick_state.values[ODROID_INPUT_DOWN])
    {
        vkbd_key(cpc_kbd[amstrad_button_down_key], 1);
    }
    else if (!(joystick->values[ODROID_INPUT_DOWN]) && previous_joystick_state.values[ODROID_INPUT_DOWN])
    {
        vkbd_key(cpc_kbd[amstrad_button_down_key], 0);
    }
    if ((joystick->values[ODROID_INPUT_A]) && !previous_joystick_state.values[ODROID_INPUT_A])
    {
        vkbd_key(cpc_kbd[amstrad_button_a_key], 1);
    }
    else if (!(joystick->values[ODROID_INPUT_A]) && previous_joystick_state.values[ODROID_INPUT_A])
    {
        vkbd_key(cpc_kbd[amstrad_button_a_key], 0);
    }
    if ((joystick->values[ODROID_INPUT_B]) && !previous_joystick_state.values[ODROID_INPUT_B])
    {
        vkbd_key(cpc_kbd[amstrad_button_b_key], 1);
    }
    else if (!(joystick->values[ODROID_INPUT_B]) && previous_joystick_state.values[ODROID_INPUT_B])
    {
        vkbd_key(cpc_kbd[amstrad_button_b_key], 0);
    }
    // Game button on G&W
    if ((joystick->values[ODROID_INPUT_START]) && !previous_joystick_state.values[ODROID_INPUT_START])
    {
        vkbd_key(cpc_kbd[amstrad_button_game_key], 1);
    }
    else if (!(joystick->values[ODROID_INPUT_START]) && previous_joystick_state.values[ODROID_INPUT_START])
    {
        vkbd_key(cpc_kbd[amstrad_button_game_key], 0);
    }
    // Time button on G&W
    if ((joystick->values[ODROID_INPUT_SELECT]) && !previous_joystick_state.values[ODROID_INPUT_SELECT])
    {
        vkbd_key(cpc_kbd[amstrad_button_time_key], 1);
    }
    else if (!(joystick->values[ODROID_INPUT_SELECT]) && previous_joystick_state.values[ODROID_INPUT_SELECT])
    {
        vkbd_key(cpc_kbd[amstrad_button_time_key], 0);
    }
    // Start button on Zelda G&W
    if ((joystick->values[ODROID_INPUT_X]) && !previous_joystick_state.values[ODROID_INPUT_X])
    {
        vkbd_key(cpc_kbd[amstrad_button_start_key], 1);
    }
    else if (!(joystick->values[ODROID_INPUT_X]) && previous_joystick_state.values[ODROID_INPUT_X])
    {
        vkbd_key(cpc_kbd[amstrad_button_start_key], 0);
    }
    // Select button on Zelda G&W
    if ((joystick->values[ODROID_INPUT_Y]) && !previous_joystick_state.values[ODROID_INPUT_Y])
    {
        vkbd_key(cpc_kbd[amstrad_button_select_key], 1);
    }
    else if (!(joystick->values[ODROID_INPUT_Y]) && previous_joystick_state.values[ODROID_INPUT_Y])
    {
        vkbd_key(cpc_kbd[amstrad_button_select_key], 0);
    }

    // Handle keyboard emulation
    if (pressed_key != NULL)
    {
        vkbd_key(cpc_kbd[pressed_key->key_id], !vkbd_key_state(cpc_kbd[pressed_key->key_id]));
        if (pressed_key->auto_release)
        {
            release_key = pressed_key;
        }
        pressed_key = NULL;
    }
    else if (release_key != NULL)
    {
        if (release_key_delay == 0)
        {
            vkbd_key(cpc_kbd[release_key->key_id], 0);
            release_key = NULL;
            release_key_delay = RELEASE_KEY_DELAY;
        }
        else
        {
            release_key_delay--;
        }
    }

    memcpy(&previous_joystick_state, joystick, sizeof(odroid_gamepad_state_t));
}

static bool show_disk_icon = false;
static uint16_t palette565[256];
int image_buffer_current_width = 384;

// No scaling
__attribute__((optimize("unroll-loops"))) static inline void blit_normal(uint8_t *src_fb, uint16_t *framebuffer)
{
    const int w1 = CPC_SCREEN_WIDTH;
    const int w2 = GW_LCD_WIDTH;
    const int h2 = GW_LCD_HEIGHT;
    const int hpad = 0;
    uint8_t *src_row;
    uint16_t *dest_row;

    for (int y = 0; y < h2; y++)
    {
        src_row = &src_fb[(y + 24) * w1 + 32];
        dest_row = &framebuffer[y * w2 + hpad];
        for (int x = 0; x < w2; x++)
        {
            dest_row[x] = palette565[src_row[x]];
        }
    }
}

__attribute__((optimize("unroll-loops"))) static inline void screen_blit_nn(uint8_t *fb, uint16_t *framebuffer /*int32_t dest_width, int32_t dest_height*/)
{
    int src_x_offset = 0;
    int src_y_offset = 0;

    int x_ratio = (int)((CPC_SCREEN_WIDTH << 16) / GW_LCD_WIDTH) + 1;
    int y_ratio = (int)((CPC_SCREEN_HEIGHT << 16) / GW_LCD_HEIGHT) + 1;
    int hpad = 0;
    int wpad = 0;

    int x2;
    int y2;

    for (int i = 0; i < GW_LCD_HEIGHT; i++)
    {
        for (int j = 0; j < GW_LCD_WIDTH; j++)
        {
            x2 = ((j * x_ratio) >> 16);
            y2 = ((i * y_ratio) >> 16);
            uint8_t b2 = fb[((y2 + src_y_offset) * image_buffer_current_width) + x2 + src_x_offset];
            framebuffer[((i + wpad) * WIDTH) + j + hpad] = palette565[b2];
        }
    }
}

static void blit(uint8_t *src_fb, uint16_t *framebuffer)
{
    odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();
    uint16_t offset = GW_LCD_WIDTH-26;

    switch (scaling)
    {
    case ODROID_DISPLAY_SCALING_OFF:
        blit_normal(src_fb, framebuffer);
        break;
    // Full height, borders on the side
    case ODROID_DISPLAY_SCALING_FIT:
    case ODROID_DISPLAY_SCALING_FULL:
        screen_blit_nn(src_fb, framebuffer);
        break;
    default:
        printf("Unsupported scaling mode %d\n", scaling);
        blit_normal(src_fb, framebuffer);
        break;
    }

    if (show_disk_icon) {
        uint16_t *dest = lcd_get_active_buffer();
        uint16_t idx = 0;
        for (uint8_t i = 0; i < 24; i++) {
            for (uint8_t j = 0; j < 24; j++) {
            if (IMG_DISKETTE[idx / 8] & (1 << (7 - idx % 8))) {
                dest[offset + j + GW_LCD_WIDTH * (2 + i)] = 0xFFFF;
            }
            idx++;
            }
        }
    }
}

void amstrad_pcm_submit()
{
    // update sound volume in emulator
    amstrad_set_volume(volume_table[odroid_audio_volume_get()]);

    memcpy(audio_get_active_buffer(), soundBuffer, audio_get_buffer_size());
}

bool amstrad_is_cpm;

static int autorun_delay = 50;
static int wait_computer = 1;

bool autorun_command()
{
    if (autorun_delay)
    {
        autorun_delay--;
        return false;
    }

    // wait one loop for the key be pressed
    wait_computer ^= 1;
    if (wait_computer)
        return false;

    if (kbd_buf_update())
    {
        auto_key = false;
        // prepare next autorun
        autorun_delay = 50;
        wait_computer = 1;
    }

    return true;
}

void amstrad_ui_set_led(bool state) {
    show_disk_icon = state;
}

static void computer_autoload()
{
    amstrad_is_cpm = false;
    // if name contains [CPM] then it's CPM file
    //    if (file_check_flag(ACTIVE_FILE->name, size, FLAG_BIOS_CPM, 5))
    //    {
    //        amstrad_is_cpm = true;
    //    }

    //   if (game_configuration.has_btn && retro_computer_cfg.use_internal_remap)
    //   {
    //      printf("[DB] game remap applied.\n");
    //      memcpy(btnPAD[0].buttons, game_configuration.btn_config.buttons, sizeof(t_button_cfg));
    //   }

    //   if (game_configuration.has_command)
    //   {
    //      strncpy(loader_buffer, game_configuration.loader_command, LOADER_MAX_SIZE);
    //   } else {
    loader_run(loader_buffer);
    //   }

    strcat(loader_buffer, "\n");
    printf("[core] DSK autorun: %s", loader_buffer);
    kbd_buf_feed(loader_buffer);
    auto_key = true;
    //   if (amstrad_is_cpm)
    //   {
    //      cpm_boot(loader_buffer);
    //   }
    //   else
    //   {
    //      ev_autorun_prepare(loader_buffer);
    //   }
}

extern uint8_t *pbSndBuffer;

void _blit()
{
    blit(amstrad_framebuffer, lcd_get_active_buffer());
    common_ingame_overlay();
}

/* gw_sleep() restores the *settings* OC level on wake, but this core forces the
 * maximum OC during gameplay when the user left the setting at 0.  Without this
 * the game keeps running at the (slower) settings clock after a sleep/wake
 * cycle.  Re-apply the boost and reinit audio (SystemClock_Config also
 * reprograms the audio PLL). */
static void amstrad_sleep_wake_up()
{
    if (odroid_settings_cpu_oc_level_get() == 0) {
        SystemClock_Config(2);
        odroid_audio_init(odroid_audio_sample_rate_get());
        audio_start_playing_full_length(audio_get_buffer_full_length());
    }
}

void app_main_amstrad(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    odroid_gamepad_state_t joystick;
    odroid_dialog_choice_t options[11];
    int disk_load_result = 0;
    int load_result = 0;

    // Set maximum clock speed for better performance if CPU is not overclocked
    if (odroid_settings_cpu_oc_level_get() == 0) {
        SystemClock_Config(2);
    }

    // Create RGB8 to RGB565 table
    for (int i = 0; i < 256; i++)
    {
        // RGB 8bits to RGB 565 (RRR|GGG|BB -> RRRRR|GGGGGG|BBBBB)
        palette565[i] = (((i >> 5) * 31 / 7) << 11) |
                        ((((i & 0x1C) >> 2) * 63 / 7) << 5) |
                        ((i & 0x3) * 31 / 3);
    }

    if (start_paused)
    {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    }
    else
    {
        common_emu_state.pause_after_frames = 0;
    }
    common_emu_state.frame_time_10us = (uint16_t)(100000 / AMSTRAD_FPS + 0.5f);
    lcd_set_refresh_rate(AMSTRAD_FPS);

    odroid_system_init(APPID_AMSTRAD, AMSTRAD_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, NULL, &amstrad_sleep_wake_up, NULL, NULL);

    // Init Sound
    audio_start_playing(AUDIO_BUFFER_LENGTH_AM);

    capmain(0, NULL);
    amstrad_set_audio_buffer((int8_t *)soundBuffer, sizeof(soundBuffer));

    if (0 == strcmp(ACTIVE_FILE->ext, AMSTRAD_DISK_EXTENSION) ||
        0 == strcmp(ACTIVE_FILE->ext, AMSTRAD_COMPRESSED_DISK_EXTENSION))
    {
        strcpy(current_disk_path, ACTIVE_FILE->path);
        attach_disk(current_disk_path, 0);
    }

    cap32_set_palette(selected_palette_index);

    if (load_state) {
        load_result = odroid_system_emu_load_state(save_slot);
    } else {
        lcd_clear_buffers();
    }
    // If disk loaded and no save state loaded, enter autoload command
    if ((disk_load_result == 0) && (load_result == 0)) {
        computer_autoload();
    }

    createOptionMenu(options);

    while (1)
    {
        wdog_refresh();
        bool drawFrame = common_emu_frame_loop();

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &_blit);

        if (auto_key) {
            autorun_command();
        } else {
            common_emu_input_loop_handle_turbo(&joystick);

            amstrad_input_update(&joystick);
        }

        caprice_retro_loop();

        if (drawFrame) {
            _blit();
            lcd_swap();
        }

        amstrad_pcm_submit();
        amstrad_set_audio_buffer((int8_t *)soundBuffer, sizeof(soundBuffer));

        common_emu_sound_sync(false);
    }
}
