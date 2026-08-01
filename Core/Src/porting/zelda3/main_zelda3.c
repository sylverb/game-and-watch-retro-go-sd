#include <odroid_system.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "main.h"
#include "gw_lcd.h"
#include "gw_linker.h"
#include "gw_buttons.h"
#include "gw_malloc.h"
#include "gw_ofw.h"

#include "bq24072.h"

#include "stm32h7xx_hal.h"

#include "common.h"
#include "rom_manager.h"
#include "appid.h"
#include "rg_i18n.h"

#include "zelda3_borders.h"

#include "zelda3/assets.h"
#include "zelda3/config.h"
#include "zelda3/snes/ppu.h"
#include "zelda3/types.h"
#include "zelda3/zelda_rtl.h"
#include "zelda3/hud.h"
#include "zelda3/audio.h"

#pragma GCC optimize("Ofast")


#define STRINGIZE(x) #x
#define STRINGIZE_VALUE_OF(x) STRINGIZE(x)

static int g_ppu_render_flags = kPpuRenderFlags_NewRenderer;

static uint8 g_gamepad_buttons;
static int g_input1_state;

static uint32 frameCtr = 0;
static uint32 renderedFrameCtr = 0;

#define ZELDA3_AUDIO_SAMPLE_RATE   (16000)   // SAI Sample rate
#if LIMIT_30FPS != 0
#define FRAMERATE 30
#else
#define FRAMERATE 60
#endif /* LIMIT_30FPS */
#define ZELDA3_AUDIO_BUFFER_LENGTH 534  // When limited to 30 fps, audio is generated for two frames at once

static int16_t audiobuffer_zelda3[ZELDA3_AUDIO_BUFFER_LENGTH];

const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];

static int selected_language_index = 0;
static char display_language_value[10];

/* keys inputs (hw & sw) */
static odroid_gamepad_state_t joystick;


void NORETURN Die(const char *error) {
  printf("Error: %s\n", error);
  abort();
}


static void LoadAssetsChunk(size_t length, uint8* data) {
  uint32 offset = 88 + kNumberOfAssets * 4 + *(uint32 *)(data + 84);
  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    if ((uint64)offset + size > length)
      assert(!"Assets file corruption");
    g_asset_sizes[i] = size;
    g_asset_ptrs[i] = data + offset;
    offset += size;
  }
}

static void LoadAssets() {
  uint32_t zelda_assets_length = 0;
  uint8 *zelda_assets = odroid_overlay_cache_file_in_flash("/roms/homebrew/zelda3_assets.dat", &zelda_assets_length, false);
  static const char kAssetsSig[] = { kAssets_Sig };

  if (zelda_assets == NULL)
    Die("Missing /roms/homebrew/zelda3_assets.dat file");

  if (zelda_assets_length < 16 + 32 + 32 + 8 + kNumberOfAssets * 4 ||
      memcmp(zelda_assets, kAssetsSig, 48) != 0 ||
      *(uint32*)(zelda_assets + 80) != kNumberOfAssets)
    Die("Invalid assets file");

  // Load some assets with assets in extflash
  LoadAssetsChunk(zelda_assets_length, (uint8 *)zelda_assets);

  // Make sure all assets were loaded
  for (size_t i = 0; i < kNumberOfAssets; i++) {
    if (g_asset_ptrs[i] == 0) {
      assert(!"Missing asset");
    }
  }
}

MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk) { g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}

#define BORDER_COLOR_565 0x1082  // Dark Dark Gray

#define BORDER_HEIGHT 240
#define BORDER_WIDTH 32

#define BORDER_Y_OFFSET (((GW_LCD_HEIGHT) - (BORDER_HEIGHT)) / 2)

void draw_border_zelda3(pixel_t * fb){
    uint32_t start, bit_index;
    start = 0;
    bit_index = 0;
    for(uint16_t i=0; i < BORDER_HEIGHT; i++){
        uint32_t offset = start + i * GW_LCD_WIDTH;
        for(uint8_t j=0; j < BORDER_WIDTH; j++){
            fb[offset + j] = 
                (IMG_BORDER_ZELDA3[bit_index >> 3] << (bit_index & 0x07)) & 0x80 ? BORDER_COLOR_565 : 0x0000;
            bit_index++;
        }
    }
    start = 32 + 256;
    bit_index = 0;
    for(uint16_t i=0; i < BORDER_HEIGHT; i++){
        uint32_t offset = start + i * GW_LCD_WIDTH;
        for(uint8_t j=0; j < BORDER_WIDTH; j++){
            fb[offset + j] = 
                (IMG_BORDER_ZELDA3[bit_index >> 3] << (bit_index & 0x07)) & 0x80 ? BORDER_COLOR_565 : 0x0000;
            bit_index++;
        }
    }
}

int8_t current_scaling = ODROID_DISPLAY_SCALING_COUNT+1;
static void DrawPpuFrame(uint16_t* framebuffer) {
  wdog_refresh();
  odroid_display_scaling_t scaling = odroid_display_get_scaling_mode();
  int pitch = 320 * 2;
  uint8 *pixel_buffer;
  // Transition from SCALING_FULL to SCALING_OFF is causing crash
  // So we artificially make this transition impossible by
  // setting SCALING_CUSTOM the same as SCALING_FIT
  if (current_scaling != scaling) {
    current_scaling = scaling;
    switch (current_scaling) {
      case ODROID_DISPLAY_SCALING_OFF: // default screen size 256x224
        g_ppu_render_flags = kPpuRenderFlags_NewRenderer;
        g_zenv.ppu->extraLeftRight = 0;
        break;
      case ODROID_DISPLAY_SCALING_FIT: // full-height 256x240
      case ODROID_DISPLAY_SCALING_CUSTOM:
        g_ppu_render_flags = kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Height240;
        g_zenv.ppu->extraLeftRight = 0;
        break;
      case ODROID_DISPLAY_SCALING_FULL: // full screen 320x240
      default:
        g_ppu_render_flags = kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Height240;
        g_zenv.ppu->extraLeftRight = UintMin(32, kPpuExtraLeftRight);
        break;
    }
  }
  switch (scaling) {
    case ODROID_DISPLAY_SCALING_OFF:
      pixel_buffer = (uint8_t *)(framebuffer + 320*8 + 32);    // Start 8 rows from the top, 32 pixels from left
      ZeldaDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
      draw_border_zelda3(framebuffer);
      break;
    case ODROID_DISPLAY_SCALING_FIT:
    case ODROID_DISPLAY_SCALING_CUSTOM:
      pixel_buffer = (uint8_t *)(framebuffer + 32);    // Start 32 pixels from left
      ZeldaDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
      draw_border_zelda3(framebuffer);
      break;
    case ODROID_DISPLAY_SCALING_FULL:
    default:
      pixel_buffer = (uint8_t *)framebuffer;
      ZeldaDrawPpuFrame(pixel_buffer, pitch, g_ppu_render_flags);
      break;
  }
}


void ZeldaApuLock() {
}

void ZeldaApuUnlock() {
}

static void HandleCommand(uint32 j, bool pressed) {
  if (j <= kKeys_Controls_Last) {
    static const uint8 kKbdRemap[] = { 0, 4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11 };
    if (pressed)
      g_input1_state |= 1 << kKbdRemap[j];
    else
      g_input1_state &= ~(1 << kKbdRemap[j]);
    return;
  }

  /*if (j == kKeys_Turbo) {
    g_turbo = pressed;
    return;
  }*/
}

static FILE *savestate_file;
static char savestate_path[255];

void writeSaveStateInitImpl() {
  savestate_file = fopen(savestate_path, "wb");
}
void writeSaveStateImpl(uint8_t* data, size_t size) {
  if (savestate_file)
    fwrite(data, 1, size, savestate_file);
}
void writeSaveStateFinalizeImpl() {
  if (savestate_file) {
    fwrite(&selected_language_index, 1, sizeof(selected_language_index), savestate_file);
    fwrite(&g_wanted_zelda_features, 1, sizeof(g_wanted_zelda_features), savestate_file);
    fclose(savestate_file);
    savestate_file = NULL;
  }
}

void readSaveStateInitImpl() {
  savestate_file = fopen(savestate_path, "rb");
}
void readSaveStateImpl(uint8_t* data, size_t size) {
 if (savestate_file != NULL) {
    wdog_refresh();
    fread(data, 1, size, savestate_file);
  } else {
    memset(data, 0, size);
  }
}
void readSaveStateFinalizeImpl() {
  if (savestate_file) {
    fread(&selected_language_index, 1, sizeof(selected_language_index), savestate_file);
    fread(&g_wanted_zelda_features, 1, sizeof(g_wanted_zelda_features), savestate_file);
    fclose(savestate_file);
    savestate_file = NULL;
  }
}

static bool zelda3_system_SaveState(char *savePathName) {
  printf("Saving state...\n");
  odroid_audio_mute(true);

  // Save state
  strcpy(savestate_path, savePathName);
  SaveLoadSlot(kSaveLoad_Save, 0);

  odroid_audio_mute(false);
  return true;
}

static bool zelda3_system_LoadState(char *savePathName) {
  odroid_audio_mute(true);

  // Load state
  if (savePathName) {
    strcpy(savestate_path, savePathName);
    SaveLoadSlot(kSaveLoad_Load, 0);
  }

  odroid_audio_mute(false);
  return true;
}

static void *Screenshot()
{
    lcd_wait_for_vblank();

    lcd_clear_active_buffer();
    DrawPpuFrame(lcd_get_active_buffer());
    return lcd_get_active_buffer();
}

static void zelda3_system_SramSave()
{
  // SRAM
  char *sram_path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
  FILE *file = fopen(sram_path, "wb");
  if (file != NULL) {
    fwrite(ZeldaGetSram(), 1, 8192, file);
    fclose(file);
  }
  free(sram_path);
}

static void zelda3_sound_start()
{
  memset(audiobuffer_zelda3, 0, sizeof(audiobuffer_zelda3));
  audio_start_playing(ZELDA3_AUDIO_BUFFER_LENGTH);
}

static void zelda3_sound_submit() {
  if (common_emu_sound_loop_is_muted()) {
    return;
  }

  int16_t factor = common_emu_sound_get_volume();
  int16_t* sound_buffer = audio_get_active_buffer();
  uint16_t sound_buffer_length = audio_get_buffer_length();

  for (int i = 0; i < sound_buffer_length; i++) {
    int32_t sample = audiobuffer_zelda3[i];
    sound_buffer[i] = (sample * factor) >> 8;
  }
}

static bool reset_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
  if (event == ODROID_DIALOG_ENTER) {
    printf("Resetting\n");
    ZeldaReset(true);
  }
  return event == ODROID_DIALOG_ENTER;
}

/* Linker-defined start of .rodata_zelda3. NOT 0xCAFE0000 hardcoded — that
 * happens to be correct today only because .rodata_zelda3 is first in the
 * linker script's RODATA region, but is layout-dependent and would break
 * silently if a port with rodata is ever added before zelda3. */
extern void *__rodata_zelda3_start__[];

/**
 * PatchCodeRodataOffset - Adjusts the code rodata offset in memory.
 * @rodata: Pointer to the rodata section in external flash.
 */
static void PatchCodeRodataOffset(uint8 *rodata, uint32_t rodata_length)
{
  uint32_t *ptr = (uint32_t *)__RAM_EMU_START__;
  uint32_t *end = (uint32_t *)&_OVERLAY_ZELDA3_BSS_END;

  uint32_t rodata_base = (uint32_t)__rodata_zelda3_start__;
  int32_t offset = (uint32_t)rodata - rodata_base;

  printf("rodata = %p base = 0x%08lX offset = 0x%08lX\n", rodata, rodata_base, offset);
  while (ptr < end) {
    if ((ptr < (uint32_t *)&_ZELDA3_MAIN_CODE_START) || (ptr > (uint32_t *)&_ZELDA3_MAIN_CODE_END)) {
      uint32_t value = *ptr;

      if ((value >= rodata_base) && (value < rodata_base + rodata_length)) {
          *ptr = value + offset;
          wdog_refresh();
      }
    }
    ptr++;
  }
}

static bool update_language_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    int max_index = ZeldaGetLanguageCount() - 1;

    if (event == ODROID_DIALOG_PREV) {
        selected_language_index = selected_language_index > 0 ? selected_language_index - 1 : max_index;
    }
    if (event == ODROID_DIALOG_NEXT) {
        selected_language_index = selected_language_index < max_index ? selected_language_index + 1 : 0;
    }

    ZeldaGetLanguageAtIndex(selected_language_index, option->value);
    printf("option->value = %s\n", option->value);

    ZeldaSetLanguage(option->value);
    return event == ODROID_DIALOG_ENTER;
}

/* Main */
int app_main_zelda3(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
  printf("Zelda3 start\n");
  ram_start = (uint32_t)&_OVERLAY_ZELDA3_BSS_END;

  /* Store read only data into round robin flash cache memory */
  uint32_t zelda_rodata_length = 0;
  uint8 *zelda_rodata = odroid_overlay_cache_file_in_flash("/roms/homebrew/zelda3.ro", &zelda_rodata_length, false);
  if (zelda_rodata == NULL) {
    printf("Missing /roms/homebrew/zelda3.ro file\n");
  }

  /* Patch application in ram to point to real flash location of data*/
  PatchCodeRodataOffset(zelda_rodata, zelda_rodata_length);

  odroid_system_init(APPID_ZELDA3, ZELDA3_AUDIO_SAMPLE_RATE);
  odroid_system_emu_init(&zelda3_system_LoadState, &zelda3_system_SaveState, &Screenshot, NULL, NULL, &zelda3_system_SramSave, NULL);
  
  if (start_paused) {
    common_emu_state.pause_after_frames = 2;
    odroid_audio_mute(true);
  } else {
    common_emu_state.pause_after_frames = 0;
  }
  common_emu_state.frame_time_10us = (uint16_t)(100000 / FRAMERATE + 0.5f);

  unsigned short *screen = 0;
  screen = lcd_get_active_buffer();

  LoadAssets();
    
  ZeldaInitialize();

  g_wanted_zelda_features = FEATURES; // TODO : Add options to enable/disable some features

  ZeldaEnableMsu(false);

  g_zenv.ppu->extraLeftRight = 0;

  if (load_state) {
    odroid_system_emu_load_state(save_slot);
  } else {
    lcd_clear_buffers();
  }

  // Load SRAM
  char *sram_path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
  FILE *file = fopen(sram_path, "rb");
  if (file != NULL) {
    fread(ZeldaGetSram(), 1, 8192, file);
    fclose(file);
  }
  free(sram_path);

  ZeldaGetLanguageAtIndex(selected_language_index, display_language_value);
  ZeldaSetLanguage(display_language_value);

  /* Start at the same time DMAs audio & video */
  /* Audio period and Video period are the same (almost at least 1 hour) */
  lcd_wait_for_vblank();
  zelda3_sound_start();

  while (true) {

    /* reset watchdog */
    wdog_refresh();

    // Update battery level
    #if BATTERY_INDICATOR
    g_battery.level = bq24072_get_percent_filtered();
    g_battery.is_charging = (
        (bq24072_get_state() == BQ24072_STATE_CHARGING)
        || (bq24072_get_state() == BQ24072_STATE_FULL)
        );
    #endif

    /* hardware keys */
    odroid_input_read_gamepad(&joystick);


    odroid_dialog_choice_t options[] = {
            {300, curr_lang->s_LangUI, display_language_value, 1, &update_language_cb},
            {300, curr_lang->s_Reset, NULL, 1, &reset_cb},
            ODROID_DIALOG_CHOICE_LAST
    };
    void _repaint()
    {
      screen = lcd_get_active_buffer();
      DrawPpuFrame(screen);
      common_ingame_overlay();
    }
    common_emu_input_loop(&joystick, options, &_repaint);


    // Handle inputs
    /*
    Retro-Go controls:
      PAUSE/SET + GAME    Store a screenshot. (Disabled by default on 1MB flash builds)
      PAUSE/SET + TIME    Toggle speedup between 1x and the last non-1x speed. Defaults to 1.5x.
      PAUSE/SET + UP 	    Brightness up.
      PAUSE/SET + DOWN 	  Brightness down.
      PAUSE/SET + RIGHT 	Volume up.
      PAUSE/SET + LEFT 	  Volume down.
      PAUSE/SET + B 	    Load state.
      PAUSE/SET + A 	    Save state.
      PAUSE/SET + POWER 	Poweroff WITHOUT save-stating.
    Game controls for zelda console:
      A                   A button (Pegasus Boots / Interacting)
      B                   B button (Sword)
      TIME                X button (Show Map)
      SELECT              Y button (Use Item)
      GAME + TIME         Select button (Save Screen)
      START               Start button (Item Selection Screen)
      GAME + B            L button (Quick-swapping, if enabled)
      GAME + A            R button (Quick-swapping, if enabled)
    Game controls for mario console:
      A                   A button (Pegasus Boots / Interacting)
      B                   B button (Sword)
      GAME + B            X button (Show Map)
      TIME                Y button (Use Item)
      GAME + TIME         Select button (Save Screen)
      GAME + A            Start button (Item Selection Screen)
      ----                L button (Quick-swapping, if enabled)
      ----                R button (Quick-swapping, if enabled)
    */

    bool isPauseModifierPressed = joystick.values[ODROID_INPUT_VOLUME];
    bool isGameModifierPressed = joystick.values[ODROID_INPUT_START];

    HandleCommand(1, !isPauseModifierPressed && joystick.values[ODROID_INPUT_UP]);
    HandleCommand(2, !isPauseModifierPressed && joystick.values[ODROID_INPUT_DOWN]);
    HandleCommand(3, !isPauseModifierPressed && joystick.values[ODROID_INPUT_LEFT]);
    HandleCommand(4, !isPauseModifierPressed && joystick.values[ODROID_INPUT_RIGHT]);
    HandleCommand(7, !isPauseModifierPressed && !isGameModifierPressed && joystick.values[ODROID_INPUT_A]); // A == A (Pegasus Boots/Interacting)
    HandleCommand(8, !isPauseModifierPressed && !isGameModifierPressed && joystick.values[ODROID_INPUT_B]); // B == B (Sword)
    HandleCommand(5, !isPauseModifierPressed && isGameModifierPressed && joystick.values[ODROID_INPUT_SELECT]); // GAME + TIME == Select (Save Screen)

    if (!get_ofw_is_mario()) {
      HandleCommand(9, !isPauseModifierPressed && !isGameModifierPressed && joystick.values[ODROID_INPUT_SELECT]);  // TIME == X (Show Map)
      HandleCommand(10, !isPauseModifierPressed && !isGameModifierPressed && joystick.values[ODROID_INPUT_Y]);  // SELECT == Y (Use Item)
      HandleCommand(6, !isPauseModifierPressed && !isGameModifierPressed && joystick.values[ODROID_INPUT_X]); // START == Start (Item Selection Screen)
      // L & R aren't used in Zelda3, but we could enable item quick-swapping.
      HandleCommand(11, !isPauseModifierPressed && isGameModifierPressed && joystick.values[ODROID_INPUT_B]); // GAME + B == L
      HandleCommand(12, !isPauseModifierPressed && isGameModifierPressed && joystick.values[ODROID_INPUT_A]); // GAME + A == R
    } else {
      HandleCommand(9, !isPauseModifierPressed && isGameModifierPressed && joystick.values[ODROID_INPUT_B]);  // GAME + B == X (Show Map)
      HandleCommand(10, !isPauseModifierPressed && !isGameModifierPressed && joystick.values[ODROID_INPUT_SELECT]); // TIME == Y (Use Item)
      HandleCommand(6, !isPauseModifierPressed && isGameModifierPressed && joystick.values[ODROID_INPUT_A]); // GAME + A == Start (Item Selection Screen)
      // No button combinations available for L/R on Mario units...
    }


    // Clear gamepad inputs when joypad directional inputs to avoid wonkiness
    int inputs = g_input1_state;
    if (g_input1_state & 0xf0)
      g_gamepad_buttons = 0;
    inputs |= g_gamepad_buttons;


    bool drawFrame = common_emu_frame_loop();

    ZeldaRunFrame(inputs);

    frameCtr++;

    #if LIMIT_30FPS != 0
    // Render audio to DMA buffer
    ZeldaRenderAudio(audiobuffer_zelda3, ZELDA3_AUDIO_BUFFER_LENGTH / 2, 1);
    // Render two frames worth of gameplay / audio for each screen render
    ZeldaRunFrame(inputs);
    ZeldaRenderAudio(audiobuffer_zelda3 + (ZELDA3_AUDIO_BUFFER_LENGTH / 2), ZELDA3_AUDIO_BUFFER_LENGTH / 2, 1);
    #else
    ZeldaRenderAudio(audiobuffer_zelda3, ZELDA3_AUDIO_BUFFER_LENGTH, 1);
    #endif /* LIMIT_30FPS*/


    if (drawFrame) {

      /* copy audio samples for DMA */
      zelda3_sound_submit();

      ZeldaDiscardUnusedAudioFrames();

      screen = lcd_get_active_buffer();

      DrawPpuFrame(screen);

      common_ingame_overlay();

      lcd_swap();

      renderedFrameCtr++;
    }

    common_emu_sound_sync(false);
  }

}
