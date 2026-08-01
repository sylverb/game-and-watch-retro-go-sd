#include <odroid_system.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>


#include "main.h"
#include "appid.h"

#include "stm32h7xx_hal.h"

#include "common.h"
#include "rom_manager.h"
#include "odroid_overlay.h"
#include "gw_lcd.h"
#include "gw_ofw.h"
#include "rg_i18n.h"

#include <assert.h>
#ifndef GNW_DISABLE_COMPRESSION
#include "lzma.h"
#endif

#include "MSX.h"
#include "Properties.h"
#include "ArchFile.h"
#include "VideoRender.h"
#include "AudioMixer.h"
#include "SlotManager.h"
#include "Casette.h"
#include "PrinterIO.h"
#include "UartIO.h"
#include "MidiIO.h"
#include "Machine.h"
#include "Board.h"
#include "Emulator.h"
#include "FileHistory.h"
#include "Actions.h"
#include "Language.h"
#include "LaunchFile.h"
#include "Disk.h"
#include "ArchEvent.h"
#include "ArchSound.h"
#include "ArchNotifications.h"
#include "JoystickPort.h"
#include "InputEvent.h"
#include "R800.h"
#include "VDP_MSX.h"
#include "save_msx.h"
#include "gw_malloc.h"
#include "gw_linker.h"
#include "main_msx.h"
#include "msx_database.h"

extern BoardInfo boardInfo;
static Properties* properties;
static Machine *msxMachine;
static Mixer* mixer;

enum{
   FREQUENCY_VDP_AUTO = 0,
   FREQUENCY_VDP_50HZ,
   FREQUENCY_VDP_60HZ
};

enum{
   MSX_GAME_ROM = 0,
   MSX_GAME_DISK,
   MSX_GAME_HDIDE
};

static RomInfo game_info;

static int msx_game_type = MSX_GAME_ROM;
// Default is MSX2+
static int selected_msx_index = 2;
// Default is Automatic
static int selected_frequency_index = FREQUENCY_VDP_AUTO;

static odroid_gamepad_state_t previous_joystick_state;
int msx_button_left_key = EC_LEFT;
int msx_button_right_key = EC_RIGHT;
int msx_button_up_key = EC_UP;
int msx_button_down_key = EC_DOWN;
int msx_button_a_key = EC_SPACE;
int msx_button_b_key = EC_CTRL;
int msx_button_game_key = EC_RETURN;
int msx_button_time_key = EC_CTRL;
int msx_button_start_key = EC_RETURN;
int msx_button_select_key = EC_CTRL;

static bool show_disk_icon = false;
static char current_disk_path[PROP_MAXPATH] = {0};
#define MSX_DISK_EXTENSION "dsk"
#define MSX_DISK_EXTENSION_COMPRESSED "cdk"

static int selected_key_index = 0;

/* strings for options */
static char disk_name[128];
static char msx_name[11];
static char key_name[10];
static char frequency_name[10];
static char a_button_name[10];
static char b_button_name[10];

/* Volume management */
static int8_t currentVolume = -1;
static const uint8_t volume_table[ODROID_AUDIO_VOLUME_MAX + 1] = {
    0,
    6,
    12,
    19,
    25,
    35,
    46,
    60,
    80,
    100,
};

#ifndef GNW_DISABLE_COMPRESSION
/* Compression management */
size_t msx_rom_decompress_size;
#endif

/* Framebuffer management */
uint8_t msx_framebuffer[272*240];
static unsigned image_buffer_base_width;
static unsigned image_buffer_current_width;
static unsigned image_buffer_height;
static unsigned width = 272;
static unsigned height = 240;
static int double_width;
static bool use_overscan = true;
static int msx2_dif = 0;
static uint16_t palette565[256];

#define FPS_NTSC  60
#define FPS_PAL   50
static int8_t msx_fps = FPS_PAL;

#define AUDIO_MSX_SAMPLE_RATE 18000

static const uint8_t IMG_DISKETTE[] = {
    0x00, 0x00, 0x00, 0x3F, 0xFF, 0xE0, 0x7C, 0x00, 0x70, 0x7C, 0x03, 0x78,
    0x7C, 0x03, 0x7C, 0x7C, 0x03, 0x7E, 0x7C, 0x00, 0x7E, 0x7F, 0xFF, 0xFE,
    0x7F, 0xFF, 0xFE, 0x7F, 0xFF, 0xFE, 0x7F, 0xFF, 0xFE, 0x7F, 0xFF, 0xFE,
    0x7F, 0xFF, 0xFE, 0x7E, 0x00, 0x7E, 0x7C, 0x00, 0x3E, 0x7C, 0x00, 0x3E,
    0x7D, 0xFF, 0xBE, 0x7C, 0x00, 0x3E, 0x7C, 0x00, 0x3E, 0x7D, 0xFF, 0xBE,
    0x7C, 0x00, 0x3E, 0x7C, 0x00, 0x3E, 0x3F, 0xFF, 0xFC, 0x00, 0x00, 0x00,
};

static Int32 soundWrite(void* dummy, Int16 *buffer, UInt32 count);
static void createMsxMachine(int msxType);
static void setPropertiesMsx(Machine *machine, int msxType);
static void setupEmulatorRessources(int msxType);
static void createProperties();
static void blit(uint8_t *msx_fb, uint16_t *framebuffer);
static void draw_disk_icon(void);

void msxLedSetFdd1(int state) {
    show_disk_icon = state;
}

static bool msx_system_LoadState(const char *savePathName)
{
    loadMsxState((char *)savePathName);
    return true;
}

static bool msx_system_SaveState(const char *savePathName)
{
    // Show disk icon when saving state
    uint16_t *dest = lcd_get_inactive_buffer();
    uint16_t idx = 0;
    for (uint8_t i = 0; i < 24; i++) {
        for (uint8_t j = 0; j < 24; j++) {
        if (IMG_DISKETTE[idx / 8] & (1 << (7 - idx % 8))) {
            dest[272 + j + GW_LCD_WIDTH * (2 + i)] = 0xFFFF;
        }
        idx++;
        }
    }
    saveMsxState((char *)savePathName);
    return true;
}

void save_gnw_msx_data() {
    SaveState* state;
    state = saveStateOpenForWrite("main_msx");
    saveStateSet(state, "selected_msx_index", selected_msx_index);
    saveStateSet(state, "selected_disk_index", 0); // for compatibility with previous savestates
    saveStateSet(state, "msx_button_a_key", msx_button_a_key);
    saveStateSet(state, "msx_button_b_key", msx_button_b_key);
    saveStateSet(state, "selected_frequency_index", selected_frequency_index);
    saveStateSet(state, "selected_key_index", selected_key_index);
    saveStateSet(state, "msx_fps", msx_fps);
    saveStateSetBuffer(state, "current_disk_path", current_disk_path, sizeof(current_disk_path));
    saveStateClose(state);
}

void load_gnw_msx_data() {
    SaveState* state;
    state = saveStateOpenForRead("main_msx");
    selected_msx_index = saveStateGet(state, "selected_msx_index", 0);
    saveStateGet(state, "selected_disk_index", 0); // for compatibility with previous savestates
    msx_button_a_key = saveStateGet(state, "msx_button_a_key", 0);
    msx_button_b_key = saveStateGet(state, "msx_button_b_key", 0);
    selected_frequency_index = saveStateGet(state, "selected_frequency_index", 0);
    selected_key_index = saveStateGet(state, "selected_key_index", 0);
    msx_fps = saveStateGet(state, "msx_fps", 0);
    saveStateGetBuffer(state, "current_disk_path", current_disk_path, sizeof(current_disk_path));
    saveStateClose(state);
}

static void *msx_screenshot()
{

    if ((vdpGetScreenMode() != 10) && (vdpGetScreenMode() != 11) && (vdpGetScreenMode() != 12)) {
        lcd_wait_for_vblank();
        lcd_clear_active_buffer();
        blit(msx_framebuffer, lcd_get_active_buffer());
        return lcd_get_active_buffer();
    }

    return NULL;
}

static bool msx_apply_cpu_clock(void)
{
    /* MSX needs boost OC when the user left settings at 0. gw_sleep restores
     * settings OC on wake (280 MHz / 64 MHz OSPI) while gameplay runs at level
     * 2 (~420 MHz / 100 MHz OSPI) — HDD titles then stutter until restart. */
    if (odroid_settings_cpu_oc_level_get() == 0) {
        SystemClock_Config(2);
        return true;
    }
    return false;
}

static void msx_sleep_wake_up()
{
    /* gw_sleep inits audio before this hook, still at settings OC. Reinit SAI/DMA
     * after boosting the PLL so sample timing matches gameplay again. */
    if (msx_apply_cpu_clock()) {
        odroid_audio_init(AUDIO_MSX_SAMPLE_RATE);
        audio_clear_buffers();
        emulatorRestartSound();
    }

    if (strlen(current_disk_path) == 0)
        return;

    if (msx_game_type != MSX_GAME_DISK && msx_game_type != MSX_GAME_HDIDE)
        return;

    const int drive = (msx_game_type == MSX_GAME_HDIDE) ? 1 : 0;

    /* SD was unmounted: reopen the FILE* only. insertDiskette() would also
     * reset the mixer and re-probe the image (disk icon flash). */
#if SD_CARD == 1
    if (!diskReopenDrive(drive))
        insertDiskette(properties, drive, current_disk_path, NULL, -1);
#endif
}

/* Called before sleep/quit unmount: fclose disk/HDD while FatFs is still live. */
static void msx_sram_save_cb(void)
{
#if SD_CARD == 1
    if (msx_game_type == MSX_GAME_DISK)
        diskCloseDrive(0);
    else if (msx_game_type == MSX_GAME_HDIDE)
        diskCloseDrive(1);
#endif
}

/* Core stubs */
void frameBufferDataDestroy(FrameBufferData* frameData){}
void frameBufferSetActive(FrameBufferData* frameData){}
void frameBufferSetMixMode(FrameBufferMixMode mode, FrameBufferMixMode mask){}
void frameBufferClearDeinterlace(){}
void archTrap(UInt8 value){}
void videoUpdateAll(Video* video, Properties* properties){}

/* framebuffer */

static void update_fb_info() {
    width  = use_overscan ? 272 : (272 - 16);
    height = use_overscan ? 240 : (240 - 48 + (msx2_dif * 2));
}

Pixel* frameBufferGetLine(FrameBuffer* frameBuffer, int y)
{
   return (msx_framebuffer +  (y * image_buffer_current_width));
}

Pixel16* frameBufferGetLine16(FrameBuffer* frameBuffer, int y)
{
   return (lcd_get_active_buffer() + sizeof(Pixel16) * (y * GW_LCD_WIDTH + 24));
}

FrameBuffer* frameBufferGetDrawFrame(void)
{
   return (void*)msx_framebuffer;
}

FrameBuffer* frameBufferFlipDrawFrame(void)
{
   return (void*)msx_framebuffer;
}

static int fbScanLine = 0;

void frameBufferSetScanline(int scanline)
{
   fbScanLine = scanline;
}

int frameBufferGetScanline(void)
{
   return fbScanLine;
}

FrameBufferData* frameBufferDataCreate(int maxWidth, int maxHeight, int defaultHorizZoom)
{
   return (void*)msx_framebuffer;
}

FrameBufferData* frameBufferGetActive()
{
    return (void*)msx_framebuffer;
}

void frameBufferSetLineCount(FrameBuffer* frameBuffer, int val)
{
    image_buffer_height = val;
}

int frameBufferGetLineCount(FrameBuffer* frameBuffer) {
    return image_buffer_height;
}

int frameBufferGetMaxWidth(FrameBuffer* frameBuffer)
{
    return FB_MAX_LINE_WIDTH;
}

int frameBufferGetDoubleWidth(FrameBuffer* frameBuffer, int y)
{
    return double_width;
}

void frameBufferSetDoubleWidth(FrameBuffer* frameBuffer, int y, int val)
{
    double_width = val;
}

/** GuessROM() ***********************************************/
/** Guess MegaROM mapper of a ROM.                          **/
/*************************************************************/
int GuessROM(const uint8_t *buf,int size)
{
    int i;
    int counters[6] = { 0, 0, 0, 0, 0, 0 };

    int mapper;

    /* No result yet */
    mapper = ROM_UNKNOWN;

    if (size >= 0x18 && memcmp(buf + 0x10, "ASCII16X", 8) == 0) {
        return ROM_ASCII16X;
    }

    if (size >= 0x18 && memcmp(buf + 0x10, "ROM_NE16", 8) == 0) {
        return ROM_NEO16;
    }

    if (size <= 0x10000) {
        if (size == 0x10000) {
            if (buf[0x4000] == 'A' && buf[0x4001] == 'B') mapper = ROM_PLAIN;
            else mapper = ROM_ASCII16;
            return mapper;
        } 
        
        if (size <= 0x4000 && buf[0] == 'A' && buf[1] == 'B') {
            UInt16 text = buf[8] + 256 * buf[9];
            if ((text & 0xc000) == 0x8000) {
                return ROM_BASIC;
            }
        }
        return ROM_PLAIN;
    }

    /* Count occurences of characteristic addresses */
    for (i = 0; i < size - 3; i++) {
        if (buf[i] == 0x32) {
            UInt32 value = buf[i + 1] + ((UInt32)buf[i + 2] << 8);

            switch(value) {
            case 0x4000: 
            case 0x8000: 
            case 0xa000: 
                counters[3]++;
                break;

            case 0x5000: 
            case 0x9000: 
            case 0xb000: 
                counters[2]++;
                break;

            case 0x6000: 
                counters[3]++;
                counters[4]++;
                counters[5]++;
                break;

            case 0x6800: 
            case 0x7800: 
                counters[4]++;
                break;

            case 0x7000: 
                counters[2]++;
                counters[4]++;
                counters[5]++;
                break;

            case 0x77ff: 
                counters[5]++;
                break;
            }
        }
    }

    /* Find which mapper type got more hits */
    mapper = 0;

    counters[4] -= counters[4] ? 1 : 0;

    for (i = 0; i <= 5; i++) {
        if (counters[i] > 0 && counters[i] >= counters[mapper]) {
            mapper = i;
        }
    }

    if (mapper == 5 && counters[0] == counters[5]) {
        mapper = 0;
    }

    switch (mapper) {
        default:
        case 0: mapper = ROM_STANDARD; break;
        case 1: mapper = ROM_MSXDOS2; break;
        case 2: mapper = ROM_KONAMI5; break;
        case 3: mapper = ROM_KONAMI4; break;
        case 4: mapper = ROM_ASCII8; break;
        case 5: mapper = ROM_ASCII16; break;
    }

    /* Return the most likely mapper type */
    return(mapper);
}

static bool update_disk_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    char new_game_name[PROP_MAXPATH];

    if (event == ODROID_DIALOG_PREV) {
        rg_storage_get_adjacent_files(current_disk_path, new_game_name, NULL);
        strcpy(current_disk_path, new_game_name);
        emulatorSuspend();
        insertDiskette(properties, 0, current_disk_path, NULL, -1);
        emulatorResume();
    }
    if (event == ODROID_DIALOG_NEXT) {
        rg_storage_get_adjacent_files(current_disk_path, NULL, new_game_name);
        strcpy(current_disk_path, new_game_name);
        emulatorSuspend();
        insertDiskette(properties, 0, current_disk_path, NULL, -1);
        emulatorResume();
    }
    strcpy(option->value, rg_basename(current_disk_path));
    return event == ODROID_DIALOG_ENTER;
}

static void gw_sound_restart()
{
    audio_stop_playing();
    audio_start_playing(AUDIO_MSX_SAMPLE_RATE / msx_fps);
}

static bool update_frequency_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = 2;

    if (event == ODROID_DIALOG_PREV) {
        selected_frequency_index = selected_frequency_index > 0 ? selected_frequency_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        selected_frequency_index = selected_frequency_index < max_index ? selected_frequency_index + 1 : 0;
    }

    switch (selected_frequency_index) {
        case FREQUENCY_VDP_AUTO:
            strcpy(option->value, curr_lang->s_msx_Freq_Auto);
            break;
        case FREQUENCY_VDP_50HZ: // Force 50Hz
            strcpy(option->value, curr_lang->s_msx_Freq_50);
            break;
        case FREQUENCY_VDP_60HZ: // Force 60Hz
            strcpy(option->value, curr_lang->s_msx_Freq_60);
            break;
    }

    if (event == ODROID_DIALOG_ENTER) {
        switch (selected_frequency_index) {
            case FREQUENCY_VDP_AUTO:
                // Frequency update will be done at next loop if needed
                vdpSetSyncMode(VDP_SYNC_AUTO);
                break;
            case FREQUENCY_VDP_50HZ: // Force 50Hz;
                msx_fps = FPS_PAL;
                common_emu_state.frame_time_10us = (uint16_t)(100000 / msx_fps + 0.5f);
                gw_sound_restart();
                emulatorRestartSound();
                vdpSetSyncMode(VDP_SYNC_50HZ);
                break;
            case FREQUENCY_VDP_60HZ: // Force 60Hz;
                msx_fps = FPS_NTSC;
                common_emu_state.frame_time_10us = (uint16_t)(100000 / msx_fps + 0.5f);
                gw_sound_restart();
                emulatorRestartSound();
                vdpSetSyncMode(VDP_SYNC_60HZ);
                break;
        }
    }
    return event == ODROID_DIALOG_ENTER;
}

static bool update_msx_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = 2;

    if (event == ODROID_DIALOG_PREV) {
        selected_msx_index = selected_msx_index > 0 ? selected_msx_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        selected_msx_index = selected_msx_index < max_index ? selected_msx_index + 1 : 0;
    }

    switch (selected_msx_index) {
        case 0: // MSX1;
            msx2_dif = 0;
            strcpy(option->value, curr_lang->s_msx_MSX1_EUR);
            break;
        case 1: // MSX2;
            msx2_dif = 10;
            strcpy(option->value, curr_lang->s_msx_MSX2_EUR);
            break;
        case 2: // MSX2+;
            msx2_dif = 10;
            strcpy(option->value, curr_lang->s_msx_MSX2_JP);
            break;
    }

    if (event == ODROID_DIALOG_ENTER) {
        boardInfo.destroy();
        boardDestroy();
        ahb_init();
        itc_init();
        setupEmulatorRessources(selected_msx_index);
    }
    return event == ODROID_DIALOG_ENTER;
}

struct msx_key_info {
    int  key_id;
    const char *name;
    bool auto_release;
};

struct msx_key_info msx_keyboard[] = {
    {EC_F1,"F1",true}, // Index 0
    {EC_F2,"F2",true},
    {EC_F3,"F3",true},
    {EC_F4,"F4",true},
    {EC_F5,"F5",true},
    {EC_SPACE,"Space",true},
    {EC_LSHIFT,"Shift",false},
    {EC_CTRL,"Control",false},
    {EC_GRAPH,"Graph",true},
    {EC_BKSPACE,"BS",true},
    {EC_TAB,"Tab",true}, // Index 10
    {EC_CAPS,"CapsLock",true},
    {EC_CODE,"Code",true},
    {EC_SELECT,"Select",true},
    {EC_RETURN,"Return",true},
    {EC_CLS,"Home",true},
    {EC_DEL,"Delete",true},
    {EC_INS,"Insert",true},
    {EC_STOP,"Stop",true},
    {EC_ESC,"Esc",true},
    {EC_1,"1/!",true},
    {EC_2,"2/@",true}, // Index 20
    {EC_3,"3/#",true},
    {EC_4,"4/$",true},
    {EC_5,"5/\%",true},
    {EC_6,"6/^",true},
    {EC_7,"7/&",true},
    {EC_8,"8/*",true},
    {EC_9,"9/(",true},
    {EC_0,"0/)",true},
    {EC_NUM0,"0",true},
    {EC_NUM1,"1",true}, // Index 30
    {EC_NUM2,"2",true},
    {EC_NUM3,"3",true},
    {EC_NUM4,"4",true},
    {EC_NUM5,"5",true},
    {EC_NUM6,"6",true},
    {EC_NUM7,"7",true},
    {EC_NUM8,"8",true},
    {EC_NUM9,"9",true},
    {EC_A,"a",true},
    {EC_B,"b",true}, // Index 40
    {EC_C,"c",true},
    {EC_D,"d",true},
    {EC_E,"e",true},
    {EC_F,"f",true},
    {EC_G,"g",true},
    {EC_H,"h",true},
    {EC_I,"i",true},
    {EC_J,"j",true},
    {EC_K,"k",true},
    {EC_L,"l",true}, // Index 50
    {EC_M,"m",true},
    {EC_N,"n",true},
    {EC_O,"o",true},
    {EC_P,"p",true},
    {EC_Q,"q",true},
    {EC_R,"r",true},
    {EC_S,"s",true},
    {EC_T,"t",true},
    {EC_U,"u",true},
    {EC_V,"v",true}, // Index 60
    {EC_W,"w",true},
    {EC_X,"x",true},
    {EC_Y,"y",true},
    {EC_Z,"z",true},
    {EC_COLON,":",true},
    {EC_UNDSCRE,"_",true},
    {EC_DIV,"/",true},
    {EC_LBRACK,"[",true},
    {EC_RBRACK,"]",true}, // Index 70
    {EC_COMMA,",",true},
    {EC_PERIOD,".",true},
    {EC_CLS,"CLS",true},
    {EC_NEG,"-",true},
    {EC_CIRCFLX,"^",true},
    {EC_BKSLASH,"\\",true},
    {EC_AT,"AT",true},
    {EC_TORIKE,"Torike",true},
    {EC_JIKKOU,"Jikkou",true},
    {EC_UP,"UP",true},
    {EC_DOWN,"DOWN",true},
    {EC_LEFT,"LEFT",true},
    {EC_RIGHT,"RIGHT",true},
    {EC_JOY1_UP,"JOY1 UP"},
    {EC_JOY1_DOWN,"JOY1 DOWN"},
    {EC_JOY1_LEFT,"JOY1 LEFT"},
    {EC_JOY1_RIGHT,"JOY1 RIGHT"},
    {EC_JOY1_BUTTON1,"JOY1 A"},
    {EC_JOY1_BUTTON2,"JOY1 B"},
};

#define RELEASE_KEY_DELAY 5
static struct msx_key_info *pressed_key = NULL;
static struct msx_key_info *release_key = NULL;
static int release_key_delay = RELEASE_KEY_DELAY;
static bool update_keyboard_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(msx_keyboard)/sizeof(msx_keyboard[0])-1;

    if (event == ODROID_DIALOG_PREV) {
        selected_key_index = selected_key_index > 0 ? selected_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        selected_key_index = selected_key_index < max_index ? selected_key_index + 1 : 0;
    }

    if (eventMap[msx_keyboard[selected_key_index].key_id]) {
        // If key is pressed, add a * in front of key name
        option->value[0] = '*';
        strcpy(option->value+1, msx_keyboard[selected_key_index].name);
    } else {
        strcpy(option->value, msx_keyboard[selected_key_index].name);
    }

    if (event == ODROID_DIALOG_ENTER) {
        pressed_key = &msx_keyboard[selected_key_index];
    }
    return event == ODROID_DIALOG_ENTER;
}

static bool update_a_button_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(msx_keyboard)/sizeof(msx_keyboard[0])-1;
    int msx_button_key_index = 0;

    // find index for current key
    for (int i = 0; i <= max_index; i++) {
        if (msx_keyboard[i].key_id == msx_button_a_key) {
            msx_button_key_index = i;
            break;
        }
    }

    if (event == ODROID_DIALOG_PREV) {
        msx_button_key_index = msx_button_key_index > 0 ? msx_button_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        msx_button_key_index = msx_button_key_index < max_index ? msx_button_key_index + 1 : 0;
    }

    msx_button_a_key = msx_keyboard[msx_button_key_index].key_id;
    strcpy(option->value, msx_keyboard[msx_button_key_index].name);
    return event == ODROID_DIALOG_ENTER;
}

static bool update_b_button_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = sizeof(msx_keyboard)/sizeof(msx_keyboard[0])-1;
    int msx_button_key_index = 0;

    // find index for current key
    for (int i = 0; i <= max_index; i++) {
        if (msx_keyboard[i].key_id == msx_button_b_key) {
            msx_button_key_index = i;
            break;
        }
    }

    if (event == ODROID_DIALOG_PREV) {
        msx_button_key_index = msx_button_key_index > 0 ? msx_button_key_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        msx_button_key_index = msx_button_key_index < max_index ? msx_button_key_index + 1 : 0;
    }

    msx_button_b_key = msx_keyboard[msx_button_key_index].key_id;
    strcpy(option->value, msx_keyboard[msx_button_key_index].name);
    return event == ODROID_DIALOG_ENTER;
}

static void msxInputUpdate(odroid_gamepad_state_t *joystick)
{
    if ((joystick->values[ODROID_INPUT_LEFT]) && !previous_joystick_state.values[ODROID_INPUT_LEFT]) {
        eventMap[msx_button_left_key]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_LEFT]) && previous_joystick_state.values[ODROID_INPUT_LEFT]) {
        eventMap[msx_button_left_key]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_RIGHT]) && !previous_joystick_state.values[ODROID_INPUT_RIGHT]) {
        eventMap[msx_button_right_key]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_RIGHT]) && previous_joystick_state.values[ODROID_INPUT_RIGHT]) {
        eventMap[msx_button_right_key]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_UP]) && !previous_joystick_state.values[ODROID_INPUT_UP]) {
        eventMap[msx_button_up_key]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_UP]) && previous_joystick_state.values[ODROID_INPUT_UP]) {
        eventMap[msx_button_up_key]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_DOWN]) && !previous_joystick_state.values[ODROID_INPUT_DOWN]) {
        eventMap[msx_button_down_key]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_DOWN]) && previous_joystick_state.values[ODROID_INPUT_DOWN]) {
        eventMap[msx_button_down_key]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_A]) && !previous_joystick_state.values[ODROID_INPUT_A]) {
        eventMap[msx_button_a_key]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_A]) && previous_joystick_state.values[ODROID_INPUT_A]) {
        eventMap[msx_button_a_key]  = 0;
    }
    if ((joystick->values[ODROID_INPUT_B]) && !previous_joystick_state.values[ODROID_INPUT_B]) {
        eventMap[msx_button_b_key]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_B]) && previous_joystick_state.values[ODROID_INPUT_B]) {
        eventMap[msx_button_b_key]  = 0;
    }
    // Game button on G&W
    if ((joystick->values[ODROID_INPUT_START]) && !previous_joystick_state.values[ODROID_INPUT_START]) {
        eventMap[msx_button_game_key]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_START]) && previous_joystick_state.values[ODROID_INPUT_START]) {
        eventMap[msx_button_game_key]  = 0;
    }
    // Time button on G&W
    if ((joystick->values[ODROID_INPUT_SELECT]) && !previous_joystick_state.values[ODROID_INPUT_SELECT]) {
        eventMap[msx_button_time_key]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_SELECT]) && previous_joystick_state.values[ODROID_INPUT_SELECT]) {
        eventMap[msx_button_time_key]  = 0;
    }
    // Start button on Zelda G&W
    if ((joystick->values[ODROID_INPUT_X]) && !previous_joystick_state.values[ODROID_INPUT_X]) {
        eventMap[msx_button_start_key]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_X]) && previous_joystick_state.values[ODROID_INPUT_X]) {
        eventMap[msx_button_start_key]  = 0;
    }
    // Select button on Zelda G&W
    if ((joystick->values[ODROID_INPUT_Y]) && !previous_joystick_state.values[ODROID_INPUT_Y]) {
        eventMap[msx_button_select_key]  = 1;
    } else if (!(joystick->values[ODROID_INPUT_Y]) && previous_joystick_state.values[ODROID_INPUT_Y]) {
        eventMap[msx_button_select_key]  = 0;
    }

    // Handle keyboard emulation
    if (pressed_key != NULL) {
        eventMap[pressed_key->key_id] = (eventMap[pressed_key->key_id] + 1) % 2;
        if (pressed_key->auto_release) {
            release_key = pressed_key;
        }
        pressed_key = NULL;
    } else if (release_key != NULL) {
        if (release_key_delay == 0) {
            eventMap[release_key->key_id] = 0;
            release_key = NULL;
            release_key_delay = RELEASE_KEY_DELAY;
        } else {
            release_key_delay--;
        }
    }

    memcpy(&previous_joystick_state,joystick,sizeof(odroid_gamepad_state_t));
}

static void createOptionMenu(odroid_dialog_choice_t *options) {
    int index=0;
    if (msx_game_type == MSX_GAME_DISK) {
        options[index].id = 100;
        options[index].label = curr_lang->s_msx_Change_Dsk;
        options[index].value = disk_name;
        options[index].enabled = 1;
        options[index].update_cb = &update_disk_cb;
        index++;
    }
    options[index].id = 100;
    options[index].label = curr_lang->s_msx_Select_MSX;
    options[index].value = msx_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_msx_cb;
    index++;
    options[index].id = 100;
    options[index].label = curr_lang->s_msx_Frequency;
    options[index].value = frequency_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_frequency_cb;
    index++;
    options[index].id = 100;
    options[index].label = curr_lang->s_msx_A_Button;
    options[index].value = a_button_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_a_button_cb;
    index++;
    options[index].id = 100;
    options[index].label = curr_lang->s_msx_B_Button;
    options[index].value = b_button_name;
    options[index].enabled = 1;
    options[index].update_cb = &update_b_button_cb;
    index++;
    options[index].id = 100;
    options[index].label = curr_lang->s_msx_Press_Key;
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

static void setPropertiesMsx(Machine *machine, int msxType) {
    int i = 0;
    uint8_t ctrl_needed = game_info.ctrl_required;
    msx2_dif = 0;
    switch(msxType) {
        case 0: // MSX1
            machine->board.type = BOARD_MSX;
            machine->video.vdpVersion = VDP_TMS9929A;
            machine->video.vramSize = 16 * 1024;
            machine->cmos.enable = 0;

            machine->slot[0].subslotted = 0;
            machine->slot[1].subslotted = 0;
            machine->slot[2].subslotted = 0;
            machine->slot[3].subslotted = 1;
            machine->cart[0].slot = 1;
            machine->cart[0].subslot = 0;
            machine->cart[1].slot = 2;
            machine->cart[1].subslot = 0;

            machine->slotInfo[i].slot = 3;
            machine->slotInfo[i].subslot = 0;
            machine->slotInfo[i].startPage = 0;
            machine->slotInfo[i].pageCount = 8; // 64kB of RAM
            machine->slotInfo[i].romType = RAM_NORMAL;
            strcpy(machine->slotInfo[i].name, "");
            i++;

            machine->slotInfo[i].slot = 0;
            machine->slotInfo[i].subslot = 0;
            machine->slotInfo[i].startPage = 0;
            machine->slotInfo[i].pageCount = 4;
            machine->slotInfo[i].romType = ROM_CASPATCH;
            strcpy(machine->slotInfo[i].name, "/bios/msx/MSX.rom");
            i++;

            if (msx_game_type == MSX_GAME_DISK) {
                machine->slotInfo[i].slot = 3;
                machine->slotInfo[i].subslot = 1;
                machine->slotInfo[i].startPage = 2;
                machine->slotInfo[i].pageCount = 4;
                machine->slotInfo[i].romType = ROM_TC8566AF;
                // If game requires extended ram by disabling second floppy
                // controller, we need to load a modified disk bios that is
                // forcing disabling second floppy controller without having
                // to press ctrl key at boot
                if (ctrl_needed) {
                    strcpy(machine->slotInfo[i].name, "/bios/msx/PANASONICDISK_.rom");
                } else {
                    strcpy(machine->slotInfo[i].name, "/bios/msx/PANASONICDISK.rom");
                }
                i++;
            }

            machine->slotInfoCount = i;
            break;

        case 1: // MSX2
            msx2_dif = 10;

            machine->board.type = BOARD_MSX_S3527;
            machine->video.vdpVersion = VDP_V9938;
            machine->video.vramSize = 128 * 1024;
            machine->cmos.enable = 1;

            machine->slot[0].subslotted = 0;
            machine->slot[1].subslotted = 0;
            machine->slot[2].subslotted = 1;
            machine->slot[3].subslotted = 1;
            machine->cart[0].slot = 1;
            machine->cart[0].subslot = 0;
            machine->cart[1].slot = 2;
            machine->cart[1].subslot = 0;

            machine->slotInfo[i].slot = 3;
            machine->slotInfo[i].subslot = 2;
            machine->slotInfo[i].startPage = 0;
            if (msx_game_type == MSX_GAME_ROM) {
                machine->slotInfo[i].pageCount = 16; // 128kB of RAM
            } else {
                machine->slotInfo[i].pageCount = 32; // 256kB of RAM
            }
            machine->slotInfo[i].romType = RAM_MAPPER;
            strcpy(machine->slotInfo[i].name, "");
            i++;

            machine->slotInfo[i].slot = 0;
            machine->slotInfo[i].subslot = 0;
            machine->slotInfo[i].startPage = 0;
            machine->slotInfo[i].pageCount = 4;
            machine->slotInfo[i].romType = ROM_CASPATCH;
            strcpy(machine->slotInfo[i].name, "/bios/msx/MSX2.rom");
            i++;

            machine->slotInfo[i].slot = 3;
            machine->slotInfo[i].subslot = 1;
            machine->slotInfo[i].startPage = 0;
            machine->slotInfo[i].pageCount = 2;
            machine->slotInfo[i].romType = ROM_NORMAL;
            strcpy(machine->slotInfo[i].name, "/bios/msx/MSX2EXT.rom");
            i++;

            if (msx_game_type == MSX_GAME_DISK) {
                machine->slotInfo[i].slot = 3;
                machine->slotInfo[i].subslot = 1;
                machine->slotInfo[i].startPage = 2;
                machine->slotInfo[i].pageCount = 4;
                machine->slotInfo[i].romType = ROM_TC8566AF;
                // If game requires extended ram by disabling second floppy
                // controller, we need to load a modified disk bios that is
                // forcing disabling second floppy controller without having
                // to press ctrl key at boot
                if (ctrl_needed) {
                    strcpy(machine->slotInfo[i].name, "/bios/msx/PANASONICDISK_.rom");
                } else {
                    strcpy(machine->slotInfo[i].name, "/bios/msx/PANASONICDISK.rom");
                }
                i++;
            } else if (msx_game_type == MSX_GAME_HDIDE) {
                machine->slotInfo[i].slot = 1;
                machine->slotInfo[i].subslot = 0;
                machine->slotInfo[i].startPage = 0;
                machine->slotInfo[i].pageCount = 8;
                machine->slotInfo[i].romType = ROM_SUNRISEIDE;
                strcpy(machine->slotInfo[i].name, "/bios/msx/Nextor.rom");
                i++;
            }

            machine->slotInfo[i].slot = 3;
            machine->slotInfo[i].subslot = 0;
            machine->slotInfo[i].startPage = 2;
            machine->slotInfo[i].pageCount = 2;
            machine->slotInfo[i].romType = ROM_MSXMUSIC; // FMPAC
            strcpy(machine->slotInfo[i].name, "/bios/msx/MSX2PMUS.rom");
            i++;

            machine->slotInfoCount = i;
            break;

        case 2: // MSX2+
            msx2_dif = 10;

            machine->board.type = BOARD_MSX_T9769B;
            machine->video.vdpVersion = VDP_V9958;
            machine->video.vramSize = 128 * 1024;
            machine->cmos.enable = 1;

            machine->slot[0].subslotted = 1;
            machine->slot[1].subslotted = 0;
            machine->slot[2].subslotted = 1;
            machine->slot[3].subslotted = 1;
            machine->cart[0].slot = 1;
            machine->cart[0].subslot = 0;
            machine->cart[1].slot = 2;
            machine->cart[1].subslot = 0;

            machine->slotInfo[i].slot = 3;
            machine->slotInfo[i].subslot = 0;
            machine->slotInfo[i].startPage = 0;
            if (msx_game_type == MSX_GAME_ROM) {
                machine->slotInfo[i].pageCount = 16; // 128kB of RAM
            } else {
                machine->slotInfo[i].pageCount = 32; // 256kB of RAM
            }
            machine->slotInfo[i].romType = RAM_MAPPER;
            strcpy(machine->slotInfo[i].name, "");
            i++;

            machine->slotInfo[i].slot = 0;
            machine->slotInfo[i].subslot = 0;
            machine->slotInfo[i].startPage = 0;
            machine->slotInfo[i].pageCount = 0;
            machine->slotInfo[i].romType = ROM_F4INVERTED;
            strcpy(machine->slotInfo[i].name, "");
            i++;

            machine->slotInfo[i].slot = 0;
            machine->slotInfo[i].subslot = 0;
            machine->slotInfo[i].startPage = 0;
            machine->slotInfo[i].pageCount = 4;
            machine->slotInfo[i].romType = ROM_CASPATCH;
            strcpy(machine->slotInfo[i].name, "/bios/msx/MSX2P.rom");
            i++;

            machine->slotInfo[i].slot = 3;
            machine->slotInfo[i].subslot = 1;
            machine->slotInfo[i].startPage = 0;
            machine->slotInfo[i].pageCount = 2;
            machine->slotInfo[i].romType = ROM_NORMAL;
            strcpy(machine->slotInfo[i].name, "/bios/msx/MSX2PEXT.rom");
            i++;

            if (msx_game_type == MSX_GAME_DISK) {
                machine->slotInfo[i].slot = 3;
                machine->slotInfo[i].subslot = 2;
                machine->slotInfo[i].startPage = 2;
                machine->slotInfo[i].pageCount = 4;
                machine->slotInfo[i].romType = ROM_TC8566AF;
                // If game requires extended ram by disabling second floppy
                // controller, we need to load a modified disk bios that is
                // forcing disabling second floppy controller without having
                // to press ctrl key at boot
                if (ctrl_needed) {
                    strcpy(machine->slotInfo[i].name, "/bios/msx/PANASONICDISK_.rom");
                } else {
                    strcpy(machine->slotInfo[i].name, "/bios/msx/PANASONICDISK.rom");
                }
                i++;
            } else if (msx_game_type == MSX_GAME_HDIDE) {
                machine->slotInfo[i].slot = 1;
                machine->slotInfo[i].subslot = 0;
                machine->slotInfo[i].startPage = 0;
                machine->slotInfo[i].pageCount = 8;
                machine->slotInfo[i].romType = ROM_SUNRISEIDE;
                strcpy(machine->slotInfo[i].name, "/bios/msx/Nextor.rom");
                i++;
            }

            machine->slotInfo[i].slot = 0;
            machine->slotInfo[i].subslot = 2;
            machine->slotInfo[i].startPage = 2;
            machine->slotInfo[i].pageCount = 2;
            machine->slotInfo[i].romType = ROM_MSXMUSIC; // FMPAC
            strcpy(machine->slotInfo[i].name, "/bios/msx/MSX2PMUS.rom");
            i++;

            machine->slotInfo[i].slot = 3;
            machine->slotInfo[i].subslot = 1;
            machine->slotInfo[i].startPage = 2;
            machine->slotInfo[i].pageCount = 4;
            machine->slotInfo[i].romType = ROM_0x4000;
            strcpy(machine->slotInfo[i].name, "/bios/msx/MSXKANJI.rom");
            i++;

            machine->slotInfoCount = i;
            break;
    }
}

static void createMsxMachine(int msxType) {
    msxMachine = calloc(1,sizeof(Machine));

    msxMachine->cpu.freqZ80 = 3579545;
    msxMachine->cpu.freqR800 = 7159090;
    msxMachine->fdc.count = 1;
    msxMachine->cmos.batteryBacked = 1;
    msxMachine->audio.psgstereo = 0;
    msxMachine->audio.psgpan[0] = 0;
    msxMachine->audio.psgpan[1] = -1;
    msxMachine->audio.psgpan[2] = 1;

    msxMachine->cpu.hasR800 = 0;
    msxMachine->fdc.enabled = 1;

    // We need to know which kind of media we will load to
    // load correct configuration
    if (0 == strcmp(ACTIVE_FILE->ext, MSX_DISK_EXTENSION_COMPRESSED)) {
        strcpy(current_disk_path, ACTIVE_FILE->path);
        uint8_t cdk_header[8];
        FILE *file = fopen(ACTIVE_FILE->path, "rb");
        if (file == NULL) {
            printf("Failed to open disk image file\n");
            return;
        }
        fread(cdk_header, 1, sizeof(cdk_header), file);
        fclose(file);
        printf("cdk_header: %02X %02X %02X %02X %02X %02X %02X %02X\n", cdk_header[0], cdk_header[1], cdk_header[2], cdk_header[3], cdk_header[4], cdk_header[5], cdk_header[6], cdk_header[7]);
        printf("count %x\n", cdk_header[4]+(cdk_header[5]<<8)+(cdk_header[6]<<16)+(cdk_header[7]<<24));
        if (cdk_header[4]+(cdk_header[5]<<8)+(cdk_header[6]<<16)+(cdk_header[7]<<24) <= 0x288) {
            printf("Compressed MSX_GAME_DISK\n");
            msx_game_type = MSX_GAME_DISK;
        } else {
            printf("Compressed MSX_GAME_HDIDE\n");
            msx_game_type = MSX_GAME_HDIDE;
        }
    } else if (0 == strcasecmp(ACTIVE_FILE->ext, MSX_DISK_EXTENSION)) {
        strcpy(current_disk_path, ACTIVE_FILE->path);
        // Find if file is disk image or IDE HDD image
        if (ACTIVE_FILE->size <= 720*1024) {
            printf("MSX_GAME_DISK\n");
            msx_game_type = MSX_GAME_DISK;
        } else {
            printf("MSX_GAME_HDIDE\n");
            msx_game_type = MSX_GAME_HDIDE;
        }
    } else {
        msx_game_type = MSX_GAME_ROM;
    }
    setPropertiesMsx(msxMachine,msxType);
}

static void insertGame() {
    bool controls_found = true;
    uint16_t mapper = game_info.mapper;

    // default config
    msx_button_right_key = EC_RIGHT;
    msx_button_left_key = EC_LEFT;
    msx_button_up_key = EC_UP;
    msx_button_down_key = EC_DOWN;
    msx_button_a_key = EC_SPACE;
    msx_button_b_key = EC_CTRL;
    msx_button_game_key = EC_RETURN;
    msx_button_time_key = EC_CTRL;
    msx_button_start_key = EC_RETURN;
    msx_button_select_key = EC_CTRL;

    uint8_t controls_profile = game_info.button_profile;

    switch (controls_profile) {
        case 1: // Konami
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_N;
            msx_button_game_key = EC_F4;
            msx_button_time_key = EC_F3;
            msx_button_start_key = EC_F1;
            msx_button_select_key = EC_F2;
        break;
        case 2: // Compile
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_LSHIFT;
            msx_button_game_key = EC_STOP;
            msx_button_time_key = EC_Z;
            msx_button_start_key = EC_STOP;
            msx_button_select_key = EC_Z;
        break;
        case 3: // YS I
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_RETURN;
            msx_button_game_key = EC_S; // Status
            msx_button_time_key = EC_I; // Inventory
            msx_button_start_key = EC_S; // Return
            msx_button_select_key = EC_I; // Inventory
        break;
        case 4: // YS II
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_RETURN;
            msx_button_game_key = EC_E; // Equipment
            msx_button_time_key = EC_I; // Inventory
            msx_button_start_key = EC_RETURN;
            msx_button_select_key = EC_S; // Status
        break;
        case 5: // YS III
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_X;
            msx_button_game_key = EC_RETURN;
            msx_button_time_key = EC_I; // Inventory
            msx_button_start_key = EC_RETURN;
            msx_button_select_key = EC_S; // Status
        break;
        case 6: // H.E.R.O.
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_SPACE;
            msx_button_game_key = EC_1; // Start level 1
            msx_button_time_key = EC_2; // Start level 5
            msx_button_start_key = EC_1; // Start level 1
            msx_button_select_key = EC_2; // Start level 5
        break;
        case 7: // SD Snatcher, Arsene Lupin 3rd, ...
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_GRAPH;
            msx_button_game_key = EC_F1; // Pause
            msx_button_time_key = EC_F1; // Pause
            msx_button_start_key = EC_F1; // Pause
            msx_button_select_key = EC_F1; // Pause
        break;
        case 8: // Konami key 2 = Keyboard
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_N;
            msx_button_game_key = EC_2;
            msx_button_time_key = EC_STOP;
            msx_button_start_key = EC_2;
            msx_button_select_key = EC_STOP;
        break;
        case 9: // Konami key 3 = Keyboard
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_N;
            msx_button_game_key = EC_3;
            msx_button_time_key = EC_STOP;
            msx_button_start_key = EC_3;
            msx_button_select_key = EC_STOP;
        break;
        case 10: // Konami Green Beret
            msx_button_a_key = EC_LSHIFT;
            msx_button_b_key = EC_LSHIFT;
            msx_button_game_key = EC_STOP;
            msx_button_time_key = EC_STOP;
            msx_button_start_key = EC_STOP;
            msx_button_select_key = EC_STOP;
        break;
        case 11: // Dragon Slayer 4
            msx_button_a_key = EC_LSHIFT; // Jump
            msx_button_b_key = EC_Z; // Magic
            msx_button_game_key = EC_RETURN; // Menu selection
            msx_button_time_key = EC_ESC; // Inventory
            msx_button_start_key = EC_RETURN;
            msx_button_select_key = EC_ESC;
        break;
        case 12: // Dragon Slayer 6
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_LSHIFT;
            msx_button_game_key = EC_RETURN;
            msx_button_time_key = EC_ESC;
            msx_button_start_key = EC_RETURN;
            msx_button_select_key = EC_ESC;
        break;
        case 13: // Dunk Shot
            msx_button_a_key = EC_LBRACK;
            msx_button_b_key = EC_RETURN;
            msx_button_game_key = EC_SPACE;
            msx_button_time_key = EC_SPACE;
            msx_button_start_key = EC_SPACE;
            msx_button_select_key = EC_SPACE;
        break;
        case 14: //Eggerland Mystery
            msx_button_a_key = EC_SPACE;
            msx_button_a_key = EC_SPACE;
            msx_button_game_key = EC_SPACE;
            msx_button_time_key = EC_STOP; // Suicide
            msx_button_start_key = EC_SPACE;
            msx_button_select_key = EC_STOP;
        break;
        case 15: // Famicle Parodic, 1942
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_GRAPH;
            msx_button_game_key = EC_STOP; // Pause
            msx_button_time_key = EC_STOP; // Pause
            msx_button_start_key = EC_STOP; // Pause
            msx_button_select_key = EC_STOP; // Pause
        break;
        case 16: // Laydock 2
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_LSHIFT;
            msx_button_game_key = EC_RETURN;
            msx_button_time_key = EC_STOP;
            msx_button_start_key = EC_RETURN;
            msx_button_select_key = EC_STOP;
        break;
        case 17: // Fray - In Magical Adventure
            msx_button_a_key = EC_C;
            msx_button_b_key = EC_X;
            msx_button_game_key = EC_LSHIFT;
            msx_button_time_key = EC_SPACE;
            msx_button_start_key = EC_LSHIFT;
            msx_button_select_key = EC_SPACE;
        break;
        case 18: // XAK 1
            // Note : If you press Space + Return, you enter main menu
            // which is allowing to go to Equipment/Items/System menu
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_RETURN;
            msx_button_game_key = EC_F2; // Equipment
            msx_button_time_key = EC_F3; // Items
            msx_button_start_key = EC_RETURN;
            msx_button_select_key = EC_F4; // System Menu
        break;
        case 19: // XAK 2/3
            // Note : If you press Space + Return, you enter main menu
            // which is allowing to go to Equipment/Items/System menu
            msx_button_a_key = EC_C;
            msx_button_b_key = EC_X;
            msx_button_game_key = EC_F2; // System Menu
            msx_button_time_key = EC_F3; // Speed Menu
            msx_button_start_key = EC_SPACE;
            msx_button_select_key = EC_RETURN;
        break;
        case 20: // Ghost
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_GRAPH;
            msx_button_game_key = EC_F5; // Continue
            msx_button_time_key = EC_F1; // Menu
            msx_button_start_key = EC_F5; // Continue
            msx_button_select_key = EC_F1; // Menu
        break;
        case 21: // Golvellius II
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_LSHIFT;
            msx_button_game_key = EC_STOP; // Continue
            msx_button_time_key = EC_STOP; // Menu
            msx_button_start_key = EC_STOP; // Continue
            msx_button_select_key = EC_STOP; // Menu
        break;
        case 22: // R-TYPE, Double Dragon
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_GRAPH;
            msx_button_game_key = EC_ESC;
            msx_button_time_key = EC_ESC;
            msx_button_start_key = EC_ESC;
            msx_button_select_key = EC_ESC;
        break;
        case 23: // Super Lode Runner
            msx_button_a_key = EC_UNDSCRE;
            msx_button_b_key = EC_DIV;
            msx_button_game_key = EC_SPACE;
            msx_button_time_key = EC_STOP;
            msx_button_start_key = EC_SPACE;
            msx_button_select_key = EC_STOP;
        break;
        case 24: // Lode Runner 1 & 2
            msx_button_a_key = EC_X;
            msx_button_b_key = EC_Z;
            msx_button_game_key = EC_SPACE;
            msx_button_time_key = EC_STOP;
            msx_button_start_key = EC_SPACE;
            msx_button_select_key = EC_STOP;
        break;
        case 25: // Dynamite Go Go
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_C;
            msx_button_game_key = EC_STOP;
            msx_button_time_key = EC_STOP;
            msx_button_start_key = EC_STOP;
            msx_button_select_key = EC_STOP;
        break;
        case 26: // Moon Patrol
            msx_button_a_key = EC_SPACE; // Fire
            msx_button_b_key = EC_RIGHT; // Jump
            msx_button_game_key = EC_1;
            msx_button_time_key = EC_STOP;
            msx_button_start_key = EC_1;
            msx_button_select_key = EC_STOP;
            msx_button_right_key = EC_X;
        break;
        case 27: // Pyro-Man
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_LSHIFT;
            msx_button_game_key = EC_1;
            msx_button_time_key = EC_STOP;
            msx_button_start_key = EC_1;
            msx_button_select_key = EC_STOP;
        break;
        case 28: // Roller Ball
            msx_button_a_key = EC_RETURN;
            msx_button_b_key = EC_RETURN;
            msx_button_game_key = EC_STOP;
            msx_button_time_key = EC_STOP;
            msx_button_start_key = EC_STOP;
            msx_button_select_key = EC_STOP;
            msx_button_left_key = EC_ESC;
        break;
        case 29: // Robo Rumble
            if (get_ofw_is_mario()) {
                msx_button_a_key = EC_P; // Right Magnet UP
                msx_button_b_key = EC_L; // Right Magnet Down
                msx_button_game_key = EC_SPACE; // Start game
                msx_button_time_key = EC_SPACE; // Start game
                msx_button_up_key = EC_Q; // Left Magnet UP
                msx_button_down_key = EC_A; // Left Magnet Down
                msx_button_right_key = EC_Q; // Left Magnet UP
                msx_button_left_key = EC_A; // Left Magnet Down
            } else {
                msx_button_a_key = EC_L; // Right Magnet Down
                msx_button_b_key = EC_L; // Right Magnet Down
                msx_button_game_key = EC_SPACE; // Start game
                msx_button_time_key = EC_SPACE; // Start game
                msx_button_start_key = EC_P; // Right Magnet UP
                msx_button_select_key = EC_P; // Right Magnet UP
                msx_button_up_key = EC_Q; // Left Magnet UP
                msx_button_down_key = EC_A; // Left Magnet Down
            }
        break;
        case 30: // Brunilda
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_SPACE;
            msx_button_game_key = EC_SPACE;
            msx_button_time_key = EC_R;
            msx_button_start_key = EC_SPACE;
            msx_button_select_key = EC_R;
        break;
        case 31: // Castle Excellent
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_GRAPH;
            msx_button_game_key = EC_F5;
            msx_button_time_key = EC_F1;
            msx_button_start_key = EC_F5;
            msx_button_select_key = EC_F1;
        break;
        case 32: // Doki Doki Penguin Land
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_GRAPH;
            msx_button_game_key = EC_1;
            msx_button_time_key = EC_STOP;
            msx_button_start_key = EC_1;
            msx_button_select_key = EC_STOP;
        break;
        case 33: // Angelic Warrior Deva
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_UP;
            msx_button_game_key = EC_F1; // Pause
            msx_button_time_key = EC_F1; // Pause
            msx_button_start_key = EC_F1; // Pause
            msx_button_select_key = EC_F1; // Pause
        break;
        case 34: // Konami MSX1 Game Collection
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_N;
            msx_button_game_key = EC_3;
            msx_button_time_key = EC_2;
            msx_button_start_key = EC_3;
            msx_button_select_key = EC_2;
        break;
        case 35: // Penguin-Kun Wars 2
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_SPACE;
            msx_button_game_key = EC_F1;
            msx_button_time_key = EC_F1;
            msx_button_start_key = EC_F1;
            msx_button_select_key = EC_F1;
        break;
        case 36: // Stevedore
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_SPACE;
            msx_button_game_key = EC_STOP;
            msx_button_time_key = EC_STOP;
            msx_button_start_key = EC_STOP;
            msx_button_select_key = EC_STOP;
        break;
        case 37: // The Castle
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_CTRL;
            msx_button_game_key = EC_F5;
            msx_button_time_key = EC_F1;
            msx_button_start_key = EC_F5;
            msx_button_select_key = EC_F1;
        break;
        case 38: // Youkai Yashiki
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_GRAPH;
            msx_button_game_key = EC_STOP;
            msx_button_time_key = EC_F1;
            msx_button_start_key = EC_STOP;
            msx_button_select_key = EC_F1;
        break;
        case 39: // Xevious
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_Z;
            msx_button_game_key = EC_STOP;
            msx_button_time_key = EC_LSHIFT;
            msx_button_start_key = EC_STOP;
            msx_button_select_key = EC_LSHIFT;
        break;
        case 40: // Undeadline
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_LSHIFT;
            msx_button_game_key = EC_ESC;
            msx_button_time_key = EC_ESC;
            msx_button_start_key = EC_ESC;
            msx_button_select_key = EC_ESC;
        break;
        case 41: // Valis 2
            msx_button_a_key = EC_X;
            msx_button_b_key = EC_Z;
            msx_button_game_key = EC_RETURN;
            msx_button_time_key = EC_F1;
            msx_button_start_key = EC_RETURN;
            msx_button_select_key = EC_F1;
        break;
        case 42: // Mr Ghost
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_LSHIFT;
            msx_button_game_key = EC_F1;
            msx_button_time_key = EC_F1;
            msx_button_start_key = EC_F1;
            msx_button_select_key = EC_F1;
        break;
        case 43: // Firebird
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_N;
            msx_button_game_key = EC_F1;
            msx_button_time_key = EC_F5;
            msx_button_start_key = EC_F1;
            msx_button_select_key = EC_F5;
        break;
        case 44: // Super Mario World
            if (get_ofw_is_mario()) {
                msx_button_a_key = EC_X; // Jump
                msx_button_b_key = EC_C; // Run/Grab/Fire
                msx_button_game_key = EC_SPACE; // Start
                msx_button_time_key = EC_Z; // Spin Jump
            } else {
                msx_button_a_key = EC_X; // Jump
                msx_button_b_key = EC_C; // Run/Grab/Fire
                msx_button_game_key = EC_ESC; // Menu
                msx_button_time_key = EC_SPACE; // Pause
                msx_button_start_key = EC_Z; // Spin Jump
                msx_button_select_key = EC_V; // Drop power reserve
                }
        break;
        case 45: // Tiny Magic
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_N;
            if (get_ofw_is_mario()) {
                msx_button_game_key = EC_BKSPACE; // Show Map
                msx_button_time_key =  EC_F5; // Restart
            } else {
                msx_button_game_key = EC_F2; // Show Password
                msx_button_time_key = EC_F5; // Restart
                msx_button_start_key = EC_F3; // Show Map
                msx_button_select_key = EC_BKSPACE; // Cancel move
            }
        break;
        case 46: // Final Fantasy
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_RETURN;
            msx_button_game_key = EC_F1; // Menu
            msx_button_time_key = EC_F2; // Team
            msx_button_start_key = EC_F1; // Menu
            msx_button_select_key = EC_F2; // Team
        break;
        case 47: // Binary Land
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_SPACE;
            msx_button_game_key = EC_ESC; // Title Screen
            msx_button_time_key = EC_F2; // Music On/Off
            msx_button_start_key = EC_F1; // Pause
            msx_button_select_key = EC_F1; // Pause
        break;
        case 48: // Boulder Dash
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_SPACE;
            msx_button_game_key = EC_ESC; // Retry
            msx_button_time_key = EC_ESC; // Retry
            msx_button_start_key = EC_RETURN; // Pause
            msx_button_select_key = EC_ESC; // Retry
        break;
        case 49: // Bruce Lee
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_SPACE;
            msx_button_game_key = EC_F4;
            msx_button_time_key = EC_F4;
            msx_button_start_key = EC_F4;
            msx_button_select_key = EC_F4;
        break;
        case 50: // Cloud Master
            msx_button_a_key = EC_SPACE; // Primary Weapon
            msx_button_b_key = EC_GRAPH; // Secondary Weapon
            msx_button_game_key = EC_SPACE;
            msx_button_time_key = EC_GRAPH;
            msx_button_start_key = EC_SPACE;
            msx_button_select_key = EC_GRAPH;
        break;
        case 51: // Ghostly Manor
            msx_button_a_key = EC_SPACE; // Block Spell
            msx_button_b_key = EC_GRAPH; // Jump
            msx_button_game_key = EC_SELECT; // 50/60Hz Toggle
            msx_button_time_key = EC_GRAPH;
            msx_button_start_key = EC_SELECT; // 50/60Hz Toggle
            msx_button_select_key = EC_GRAPH;
        break;
        case 52: // Pampas & Selene
            msx_button_a_key = EC_SPACE; // Main Attack
            msx_button_b_key = EC_M; // Special Attack 1 (Bow / Fireball)
            // EC_N : Special Attack 2 (mine/wave) & or release an enemy soul
            msx_button_game_key = EC_F1; // Character screen & Inventory
            msx_button_time_key = EC_F2; // Maps
            msx_button_start_key = EC_F3; // Gods & Quests
            msx_button_select_key = EC_F4; // Achievements
        break;
        case 53: // Snatcher
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_GRAPH;
            msx_button_game_key = EC_0;
            msx_button_time_key = EC_1;
            msx_button_start_key = EC_3;
            msx_button_select_key = EC_2;
        break;
        case 54: // Konami with F5 to continue
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_N;
            msx_button_game_key = EC_F4;
            msx_button_time_key = EC_F3;
            msx_button_start_key = EC_F5;
            msx_button_select_key = EC_F2;
        break;
        case 55: // Brain Dead
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_ESC;
            msx_button_game_key = EC_SPACE;
            msx_button_time_key = EC_ESC;
            msx_button_start_key = EC_SPACE;
            msx_button_select_key = EC_ESC;
        break;
        case 56: // Black Cyclon
            msx_button_a_key = EC_SPACE; // Fire
            msx_button_b_key = EC_LSHIFT; // Action
            msx_button_game_key = EC_F2; // Use equipment
            msx_button_time_key = EC_F3; // Lock control
            msx_button_start_key = EC_STOP; // Pause
            msx_button_select_key = EC_F5; // Lose life
        break;
        case 57: // Gyro Adventure
            msx_button_a_key = EC_Z; // Fire
            msx_button_b_key = EC_X; // Action
            msx_button_game_key = EC_SPACE;
            msx_button_time_key = EC_ESC;
            msx_button_start_key = EC_SPACE;
            msx_button_select_key = EC_ESC;
        break;
        case 58: // River Raid
            msx_button_a_key = EC_SPACE; // Fire
            msx_button_b_key = EC_SPACE; // Fire
            msx_button_game_key = EC_1;
            msx_button_time_key = EC_3;
            msx_button_start_key = EC_7;
            msx_button_select_key = EC_5;
        break;
        case 59: // Sokoban
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_SPACE;
            msx_button_game_key = EC_F1; // Restart
            msx_button_time_key = EC_F2; // Color Menu
            msx_button_start_key = EC_F2; // Color Menu
            msx_button_select_key = EC_F1; // Restart
        break;
        case 60: // Takahasi Meijin no Boukenjima - Wonder Boy
            msx_button_a_key = EC_Z;
            msx_button_b_key = EC_X;
            msx_button_game_key = EC_SPACE;
            msx_button_time_key = EC_SPACE;
            msx_button_start_key = EC_SPACE;
            msx_button_select_key = EC_SPACE;
        break;
        case 61: // Magnar
            msx_button_a_key = EC_SPACE;
            msx_button_b_key = EC_GRAPH;
            msx_button_game_key = EC_F1; // Main menu
            msx_button_time_key = EC_F2; // Weapons
            msx_button_start_key = EC_F4; // System
            msx_button_select_key = EC_F3; // Equipment
        break;
        case 62: // Dragon Slayer 5 : Sorcerian
            msx_button_a_key = EC_GRAPH; // Valid
            msx_button_b_key = EC_SPACE; // Cancel
            msx_button_game_key = EC_E; // Equipment
            msx_button_time_key = EC_I; // Inventory
            msx_button_start_key = EC_S; // Status
            msx_button_select_key = EC_M; // Monster Info
        break;
        case 63: // Dragon Slayer 3 : Romancia
            msx_button_a_key = EC_LSHIFT;
            msx_button_b_key = EC_Z;
            msx_button_game_key = EC_LSHIFT;
            msx_button_time_key = EC_Z;
            msx_button_start_key = EC_LSHIFT;
            msx_button_select_key = EC_Z;
        break;
        case 0:    // Default configuration
        case 0x7F: // No configuration
        default:
            controls_found = false;
        break;
    }

    switch (msx_game_type) {
        case MSX_GAME_ROM:
        {
            printf("Rom Mapper %d\n",mapper);
            if (mapper == ROM_UNKNOWN) {
#ifndef GNW_DISABLE_COMPRESSION
                if(strcmp(ACTIVE_FILE->ext, "lzma") == 0) {
                    mapper = GuessROM((unsigned char *)&_MSX_ROM_UNPACK_BUFFER,msx_rom_decompress_size);
                }
                else
#endif
                {
                    uint32_t rom_size;
                    uint8_t *rom_data = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &rom_size, false);
                    if (rom_data == NULL) {
                        return;
                    }
                    if (rom_data != NULL) {
                        mapper = GuessROM(rom_data, rom_size);
                    }
                }
            }
            if (!controls_found) {
                // If game is using konami mapper, we setup a Konami key mapping
                // elseway we use joystick control
                switch (mapper)
                {
                    case ROM_KONAMI5:
                    case ROM_KONAMI4:
                    case ROM_KONAMI4NF:
                        msx_button_a_key = EC_SPACE;
                        msx_button_b_key = EC_N;
                        msx_button_game_key = EC_F4;
                        msx_button_time_key = EC_F3;
                        msx_button_start_key = EC_F1;
                        msx_button_select_key = EC_F2;
                        break;
                    default:
                        msx_button_up_key = EC_JOY1_UP;
                        msx_button_down_key = EC_JOY1_DOWN;
                        msx_button_right_key = EC_JOY1_RIGHT;
                        msx_button_left_key = EC_JOY1_LEFT;
                        msx_button_a_key = EC_JOY1_BUTTON1;
                        msx_button_b_key = EC_JOY1_BUTTON2;
                        break;
                }
            }
            printf("insertCartridge msx mapper %d\n",mapper);
            insertCartridge(properties, 0, ACTIVE_FILE->path, NULL, mapper, -1);
            break;
        }
        case MSX_GAME_DISK:
        {
            printf("insertDiskette msx game type %d path %s\n", msx_game_type, current_disk_path);
            insertDiskette(properties, 0, current_disk_path, NULL, -1);

            // We load SCC-I cartridge for disk games requiring it
            switch (mapper) {
                case ROM_SNATCHER:
                case ROM_SDSNATCHER:
                    insertCartridge(properties, 0, CARTNAME_SNATCHER, NULL, ROM_SNATCHER, -1);
                    if (!controls_found) {
                        msx_button_a_key = EC_SPACE;
                        msx_button_b_key = EC_N;
                        msx_button_game_key = EC_F4;
                        msx_button_time_key = EC_F3;
                        msx_button_start_key = EC_F1;
                        msx_button_select_key = EC_F2;
                        controls_found = true;
                    }
                    break;
                case ROM_SCC:
                    insertCartridge(properties, 0, CARTNAME_SCC, NULL, ROM_SCC, -1);
                    if (!controls_found) {
                        msx_button_a_key = EC_SPACE;
                        msx_button_b_key = EC_N;
                        msx_button_game_key = EC_F4;
                        msx_button_time_key = EC_F3;
                        msx_button_start_key = EC_F1;
                        msx_button_select_key = EC_F2;
                        controls_found = true;
                    }
                    break;
            }
            if (!controls_found) {
                // If game name contains konami, we setup a Konami key mapping
                if (strcasestr(ACTIVE_FILE->name,"konami")) {
                    msx_button_a_key = EC_SPACE;
                    msx_button_b_key = EC_N;
                    msx_button_game_key = EC_F4;
                    msx_button_time_key = EC_F3;
                    msx_button_start_key = EC_F1;
                    msx_button_select_key = EC_F2;
                } else {
                    msx_button_up_key = EC_JOY1_UP;
                    msx_button_down_key = EC_JOY1_DOWN;
                    msx_button_right_key = EC_JOY1_RIGHT;
                    msx_button_left_key = EC_JOY1_LEFT;
                    msx_button_a_key = EC_JOY1_BUTTON1;
                    msx_button_b_key = EC_JOY1_BUTTON2;
                }
            }
            break;
        }
        case MSX_GAME_HDIDE:
        {
            insertCartridge(properties, 1, CARTNAME_SNATCHER, NULL, ROM_SNATCHER, -1);
            insertDiskette(properties, 1, current_disk_path, NULL, -1);
            break;
        }
    }
}

static void createProperties() {
    properties = propCreate(1, EMU_LANG_ENGLISH, P_KBD_EUROPEAN, P_EMU_SYNCNONE, "");
    properties->sound.stereo = 0;
    if (selected_frequency_index == FREQUENCY_VDP_AUTO) {
        properties->emulation.vdpSyncMode = P_VDP_SYNCAUTO;
    } else if (selected_frequency_index == FREQUENCY_VDP_60HZ) {
        properties->emulation.vdpSyncMode = P_VDP_SYNC60HZ;
    } else {
        properties->emulation.vdpSyncMode = P_VDP_SYNC50HZ;
    }
    properties->emulation.enableFdcTiming = 0;
    properties->emulation.noSpriteLimits = 0;
    properties->sound.masterVolume = 0;

    currentVolume = -1;
    // Default : enable SCC and disable MSX-MUSIC
    // This will be changed dynamically if the game use MSX-MUSIC
    properties->sound.mixerChannel[MIXER_CHANNEL_SCC].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_MSXMUSIC].enable = 1;
    properties->sound.mixerChannel[MIXER_CHANNEL_PSG].pan = 0;
    properties->sound.mixerChannel[MIXER_CHANNEL_MSXMUSIC].pan = 0;
    properties->sound.mixerChannel[MIXER_CHANNEL_SCC].pan = 0;

    // Joystick Configuration
    properties->joy1.typeId = JOYSTICK_PORT_JOYSTICK;
}

static void setupEmulatorRessources(int msxType)
{
    int i;
    msxYjkColorInit();
    mixer = mixerCreate();
    createProperties();
    createMsxMachine(msxType);
    emulatorInit(properties, mixer);
    insertGame();
    emulatorRestartSound();

    for (i = 0; i < MIXER_CHANNEL_TYPE_COUNT; i++)
    {
        mixerSetChannelTypeVolume(mixer, i, properties->sound.mixerChannel[i].volume);
        mixerSetChannelTypePan(mixer, i, properties->sound.mixerChannel[i].pan);
        mixerEnableChannelType(mixer, i, properties->sound.mixerChannel[i].enable);
    }

    mixerSetMasterVolume(mixer, properties->sound.masterVolume);
    mixerEnableMaster(mixer, properties->sound.masterEnable);

    boardSetFdcTimingEnable(properties->emulation.enableFdcTiming);
    boardSetY8950Enable(0/*properties->sound.chip.enableY8950*/);
    boardSetYm2413Enable(1/*properties->sound.chip.enableYM2413*/);
    boardSetMoonsoundEnable(0/*properties->sound.chip.enableMoonsound*/);
    boardSetVideoAutodetect(1/*properties->video.detectActiveMonitor*/);

    emulatorStartMachine(NULL, msxMachine);
    // Enable SCC and disable MSX-MUSIC as G&W is not powerfull enough to handle both at same time
    // If a game wants to play MSX-MUSIC sound, the mapper will detect it and it will disable SCC
    // and enable MSX-MUSIC
    mixerEnableChannelType(boardGetMixer(), MIXER_CHANNEL_SCC, 1);
    mixerEnableChannelType(boardGetMixer(), MIXER_CHANNEL_MSXMUSIC, 0);
}

// No scaling
__attribute__((optimize("unroll-loops")))
static inline void blit_normal(uint8_t *msx_fb, uint16_t *framebuffer) {
    const int w1 = image_buffer_current_width;
    const int w2 = GW_LCD_WIDTH;
    const int h2 = GW_LCD_HEIGHT;
    const int hpad = 27;
    uint8_t  *src_row;
    uint16_t *dest_row;

    for (int y = 0; y < h2; y++) {
        src_row  = &msx_fb[y*w1];
        dest_row = &framebuffer[y * w2 + hpad];
        for (int x = 0; x < w1; x++) {
            dest_row[x] = palette565[src_row[x]];
        }
    }
}

__attribute__((optimize("unroll-loops")))
static inline void screen_blit_nn(uint8_t *msx_fb, uint16_t *framebuffer/*int32_t dest_width, int32_t dest_height*/)
{
    int w1 = width;
    int h1 = height;
    int w2 = GW_LCD_WIDTH;
    int h2 = GW_LCD_HEIGHT;
    int src_x_offset = 8;
    int src_y_offset = 24 - (msx2_dif);

    int x_ratio = (int)((w1<<16)/w2) +1;
    int y_ratio = (int)((h1<<16)/h2) +1;
    int hpad = 0;
    int wpad = 0;

    int x2;
    int y2;

    for (int i=0;i<h2;i++) {
        for (int j=0;j<w2;j++) {
            x2 = ((j*x_ratio)>>16) ;
            y2 = ((i*y_ratio)>>16) ;
            uint8_t b2 = msx_fb[((y2+src_y_offset)*image_buffer_current_width)+x2+src_x_offset];
            framebuffer[((i+wpad)*WIDTH)+j+hpad] = palette565[b2];
        }
    }
}

static void draw_disk_icon(void)
{
    odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();
    uint16_t offset;
    uint16_t *dest;
    uint16_t idx = 0;

    if (!show_disk_icon)
        return;

    switch (scaling) {
    case ODROID_DISPLAY_SCALING_OFF:
        offset = 272;
        break;
    case ODROID_DISPLAY_SCALING_FIT:
    case ODROID_DISPLAY_SCALING_FULL:
    case ODROID_DISPLAY_SCALING_CUSTOM:
        offset = GW_LCD_WIDTH - 26;
        break;
    default:
        return;
    }

    dest = lcd_get_active_buffer();
    for (uint8_t i = 0; i < 24; i++) {
        for (uint8_t j = 0; j < 24; j++) {
            if (IMG_DISKETTE[idx / 8] & (1 << (7 - idx % 8))) {
                dest[offset + j + GW_LCD_WIDTH * (2 + i)] = 0xFFFF;
            }
            idx++;
        }
    }
}

static void blit(uint8_t *msx_fb, uint16_t *framebuffer)
{
    odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();

    switch (scaling) {
    case ODROID_DISPLAY_SCALING_OFF:
        use_overscan = true;
        update_fb_info();
        blit_normal(msx_fb, framebuffer);
        break;
    // Full height, borders on the side
    case ODROID_DISPLAY_SCALING_FIT:
    case ODROID_DISPLAY_SCALING_FULL:
    case ODROID_DISPLAY_SCALING_CUSTOM:
        use_overscan = false;
        update_fb_info();
        screen_blit_nn(msx_fb, framebuffer);
        break;
    default:
        printf("Unsupported scaling mode %d\n", scaling);
        break;
    }
}

size_t msx_getromdata(uint8_t **data, uint8_t *src_data, size_t src_size, const char *ext)
{
    /* src pointer to the ROM data in the external flash (raw or LZ4) */
#ifndef GNW_DISABLE_COMPRESSION
    unsigned char *dest = (unsigned char *)&_MSX_ROM_UNPACK_BUFFER;
    uint32_t available_size = (uint32_t)&_MSX_ROM_UNPACK_BUFFER_SIZE;
    wdog_refresh();
    if(strcmp(ext, "lzma") == 0){
        size_t n_decomp_bytes;
        n_decomp_bytes = lzma_inflate(dest, available_size, src_data, src_size);
        *data = dest;
        return n_decomp_bytes;
    }
    else
#endif
    {
        *data = (unsigned char *)src_data;
        return src_size;
    }
}

void app_main_msx(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    odroid_gamepad_state_t joystick;
    odroid_dialog_choice_t options[10];
    bool drawFrame;

    // Set maximum clock speed for better performance if CPU is not overclocked
    msx_apply_cpu_clock();

    show_disk_icon = false;

    // Create RGB8 to RGB565 table
    for (int i = 0; i < 256; i++)
    {
        // RGB 8bits to RGB 565 (RRR|GGG|BB -> RRRRR|GGGGGG|BBBBB)
        palette565[i] = (((i>>5)*31/7)<<11) |
                         ((((i&0x1C)>>2)*63/7)<<5) |
                         ((i&0x3)*31/3);
    }

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }
    common_emu_state.frame_time_10us = (uint16_t)(100000 / msx_fps + 0.5f);

    odroid_system_init(APPID_MSX, AUDIO_MSX_SAMPLE_RATE);
#if CHEAT_CODES == 1
    odroid_system_emu_init(&msx_system_LoadState, &msx_system_SaveState, &msx_screenshot, NULL, &msx_sleep_wake_up, &msx_sram_save_cb, &update_cheats_msx);
#else
    odroid_system_emu_init(&msx_system_LoadState, &msx_system_SaveState, &msx_screenshot, NULL, &msx_sleep_wake_up, &msx_sram_save_cb, NULL);
#endif

    image_buffer_base_width    =  272;
    image_buffer_current_width =  image_buffer_base_width;
    image_buffer_height        =  240;

    memset(msx_framebuffer, 0, sizeof(msx_framebuffer));
    
    audio_clear_buffers();

    // Get game info from sha1 and database file
    if (!msx_get_game_info(ACTIVE_FILE, &game_info)) {
        game_info.mapper = ROM_UNKNOWN;
        game_info.button_profile = 0x7F;
        game_info.ctrl_required = false;
    } else {
        printf("Game info found mapper %d profile %d ctrl %d\n", game_info.mapper, game_info.button_profile, game_info.ctrl_required);
    }

/* To reserve correct amount of RAM for decompressing game   */
    /* We have to decompress game ROM in ram now (if compressed) */
    /* It will allow to correctly dynamically allocate ram for   */
    /* different usages (like MSX RAM)                           */

    setupEmulatorRessources(selected_msx_index);

    createOptionMenu(options);

    if (load_state) {
        odroid_system_emu_load_state(save_slot);

        /* Remount the disk image only for floppy (.dsk) games. The HDD is on
         * drive 1 (Sunrise/Nextor IDE); remounting drive 0 used to mount the
         * HD image on the floppy slot and desync the IDE state restored from
         * the save. For IDE games the image is already open from insertGame()
         * before load — remounting would flush disk caches mid-transfer. */
        if (msx_game_type == MSX_GAME_DISK && strlen(current_disk_path) > 0) {
            emulatorSuspend();
            insertDiskette(properties, 0, current_disk_path, NULL, -1);
            emulatorResume();
        }
    } else {
        lcd_clear_buffers();
    }

#if CHEAT_CODES == 1
    update_cheats_msx();
#endif
    while (1) {
        // Frequency change check if in automatic mode
        if ((selected_frequency_index == FREQUENCY_VDP_AUTO) && (msx_fps != boardInfo.getRefreshRate())) {
            // Update ressources to switch system frequency
            msx_fps = boardInfo.getRefreshRate();
            lcd_set_refresh_rate(msx_fps);

            common_emu_state.frame_time_10us = (uint16_t)(100000 / msx_fps + 0.5f);
            gw_sound_restart();
            emulatorRestartSound();
        }

        wdog_refresh();

        drawFrame = common_emu_frame_loop();

        void _blit_frame(void)
        {
            // Modes 10/11/12: RGB565 written directly by the VDP (no palette blit)
            if ((vdpGetScreenMode() != 10) && (vdpGetScreenMode() != 11) && (vdpGetScreenMode() != 12)) {
                blit(msx_framebuffer, lcd_get_active_buffer());
            }
        }

        void _blit(void)
        {
            _blit_frame();
            common_ingame_overlay();
        }

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &_blit);
        common_emu_input_loop_handle_turbo(&joystick);

        msxInputUpdate(&joystick);

        // Render 1 frame
        ((R800*)boardInfo.cpuRef)->terminate = 0;
        boardInfo.run(boardInfo.cpuRef);

        /* Modes 10/11/12 render RGB565 straight into the LCD back buffer
         * (frameBufferGetLine16). Gwenesis always lcd_swap() even when skipping
         * line render; MSX must do the same or the panel stays on a stale front
         * buffer while emulation keeps updating the back buffer. */
        if (drawFrame)
            _blit_frame();
        /* Overlay after emulation: run() overwrites any HUD drawn in input_loop,
         * and _blit_frame() is skipped on frameskip — still show volume/brightness. */
        if (common_emu_state.overlay != INGAME_OVERLAY_NONE)
            common_ingame_overlay();
        draw_disk_icon();
        lcd_swap();

        // Render audio
        mixerSyncGNW(mixer,(AUDIO_MSX_SAMPLE_RATE/msx_fps));

        common_emu_sound_sync(false);
    }
}

static Int32 soundWrite(void* dummy, Int16 *buffer, UInt32 count)
{
    uint8_t volume = odroid_audio_volume_get();
    if (volume != currentVolume) {
        if (volume == 0) {
            mixerSetEnable(mixer,0);
        } else {
            mixerSetEnable(mixer,1);
            mixerSetMasterVolume(mixer,volume_table[volume]);
        }
        currentVolume = volume;
    }

    memcpy(audio_get_active_buffer(), buffer, audio_get_buffer_size());
    return 0;
}

void archSoundCreate(Mixer* mixer, UInt32 sampleRate, UInt32 bufferSize, Int16 channels) {
    // Init Sound
    gw_sound_restart();
    mixerSetStereo(mixer, 0);
    mixerSetWriteCallback(mixer, soundWrite, NULL, (AUDIO_MSX_SAMPLE_RATE/msx_fps));
}

void archSoundDestroy(void) {}

#if CHEAT_CODES == 1
void update_cheats_msx() {
    unsigned int temp, addr,data, size;

    slotManagerResetCheat();

    for(int i=0; i<MAX_CHEAT_CODES && i<ACTIVE_FILE->cheat_count; i++) {
        if (odroid_settings_ActiveGameGenieCodes_is_enabled(ACTIVE_FILE->path, i)) {
            // MFC format description is available @ https://www.msxblue.com/manual/trainermcf_c.htm
            // Old MFC format : a,b,c,d,e (memtype,addr,value,enabled,description)
            // enabled and description are optional
            if(sscanf(ACTIVE_FILE->cheat_codes[i], "%u,%u,%u",&temp,&addr,&data)==3) {
                slotManagerAddCheat(addr, data, 1);
            }
            // New MFC format : A:B:C:D:E (addr_hex,value_hex,size,displaytype,description)
            // displaytype and description are optional
            else if(sscanf(ACTIVE_FILE->cheat_codes[i], "%x:%x:%d", &addr, &data, &size) == 3) {
                slotManagerAddCheat(addr, data, size == 0 ? 1 : 2); // 0=8bit, 1=16bit
            }
        }
    }
}
#endif

#ifdef MSX_NO_FILESYSTEM
const char* boardGetBaseDirectory(void)
{
    return "";
}
#endif
