/*
Gwenesis : Genesis & megadrive Emulator.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.
This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with
this program. If not, see <http://www.gnu.org/licenses/>.

__author__ = "bzhxx"
__contact__ = "https://github.com/bzhxx"
__license__ = "GPLv3"

*/

#include <odroid_system.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "main.h"
#include "gw_lcd.h"
#include "gw_linker.h"
#include "gw_buttons.h"
#include "gw_flash.h"
#include "gw_ofw.h"

/* TO move elsewhere */
#include "stm32h7xx_hal.h"

#include "common.h"
#include "rom_manager.h"
#include "appid.h"
#include "rg_i18n.h"
#include "odroid_settings.h"

/* Gwenesis Emulator */
#include "m68k.h"
#include "z80inst.h"
#include "ym2612.h"
#include "gwenesis_sn76489.h"
#include "gwenesis_bus.h"
#include "gwenesis_io.h"
#include "gwenesis_vdp.h"
#include "gwenesis_savestate.h"
#include "gwenesis_sram.h"
#include "gw_malloc.h"

#pragma GCC optimize("Ofast")

#define ENABLE_DEBUG_OPTIONS 0

static unsigned int gwenesis_show_debug_bar = 0;

static bool isZelda;

unsigned int gwenesis_audio_freq;
unsigned int gwenesis_audio_buffer_lenght;
static int gwenesis_vsync_mode = 0;
unsigned int gwenesis_refresh_rate;

unsigned int gwenesis_lcd_current_line;
#define GWENESIS_AUDIOSYNC_START_LCD_LINE 248

static int gwenesis_lpfilter = 0;
extern int gwenesis_H32upscaler;
static unsigned int gwenesis_audio_pll_sync = 0;

static int hori_screen_offset, vert_screen_offset;

/* GPGX-aligned H-INT counter (persists across frames, reloaded on last vblank line). */
static int hint_counter = 0xff;
static int skip_first_vint = 1;

#define VINT_H32_CYCLES 770u
#define VINT_H40_CYCLES 788u

static inline void gwenesis_run_cpus_to(unsigned target)
{
  m68k_run((int)target);
  z80_run((int)target);
  if (GWENESIS_AUDIO_ACCURATE == 0) {
    gwenesis_SN76489_run((int)target);
    ym2612_run((int)target);
  }
}

/* Clocks and synchronization */
/* system clock is video clock */
int system_clock;

extern int mode_pal;

/* shared variables with gwenesis_sn76589 */
int16_t gwenesis_sn76489_buffer[GWENESIS_AUDIO_BUFFER_CAPACITY];
int sn76489_index; /* sn78649 audio buffer index */
int sn76489_clock; /* sn78649 clock in video clock resolution */


/* shared variables with gwenesis_ym2612 */
int16_t gwenesis_ym2612_buffer[GWENESIS_AUDIO_BUFFER_CAPACITY];
int ym2612_index; /* ym2612 audio buffer index */
int ym2612_clock; /* ym2612 clock in video clock resolution */

/* keys inpus (hw & sw) */
static odroid_gamepad_state_t joystick;

#define NB_OF_COMBO 6

static char ODROID_INPUT_DEF_C;
static int ABCkeys_value = 5;
static int PAD_A_def = ODROID_INPUT_A;
static int PAD_B_def = ODROID_INPUT_B;
static int PAD_C_def;
static const char ABCkeys_combo_str[NB_OF_COMBO][10];
static char ABCkeys_str[10];

/* callback used by the meluator to capture keys */
void gwenesis_io_get_buttons()
{
  /* Keys mapping */
  /*
  * GAME is START (ignore)
  * TIME is SELECT
  * PAUSE/SET is VOLUME
  */

  odroid_gamepad_state_t host_joystick;

  odroid_input_read_gamepad(&host_joystick);

  /* shortcut is active ignore keys for the emulator */
  if (isZelda) {
    if ( host_joystick.values[ODROID_INPUT_VOLUME] ) return;
  } else {
    if ( host_joystick.values[ODROID_INPUT_SELECT] ) return;
  }

  button_state[0] = host_joystick.values[ODROID_INPUT_UP] << PAD_UP |
                    host_joystick.values[ODROID_INPUT_DOWN] << PAD_DOWN |
                    host_joystick.values[ODROID_INPUT_LEFT] << PAD_LEFT |
                    host_joystick.values[ODROID_INPUT_RIGHT] << PAD_RIGHT |
                    host_joystick.values[PAD_A_def] << PAD_A |
                    host_joystick.values[PAD_B_def] << PAD_B |
                    host_joystick.values[PAD_C_def] << PAD_C |
                    host_joystick.values[ODROID_INPUT_START] << PAD_S;

  button_state[0] = ~ button_state[0];

}

static void gwenesis_system_init() {

  gwenesis_audio_pll_sync = 0;

  if (mode_pal) {
    gwenesis_audio_freq = GWENESIS_AUDIO_FREQ_PAL;
    gwenesis_audio_buffer_lenght = GWENESIS_AUDIO_BUFFER_LENGTH_PAL;
    gwenesis_refresh_rate = GWENESIS_REFRESH_RATE_PAL;
  } else {
    gwenesis_audio_freq = GWENESIS_AUDIO_FREQ_NTSC;
    gwenesis_audio_buffer_lenght = GWENESIS_AUDIO_BUFFER_LENGTH_NTSC;
    gwenesis_refresh_rate = GWENESIS_REFRESH_RATE_NTSC;
  }

  memset(gwenesis_sn76489_buffer, 0, sizeof(gwenesis_sn76489_buffer));
  memset(gwenesis_ym2612_buffer, 0, sizeof(gwenesis_ym2612_buffer));

  odroid_audio_init(gwenesis_audio_freq);
  lcd_set_refresh_rate(gwenesis_refresh_rate);
}

static void gwenesis_sound_start()
{
  audio_start_playing(gwenesis_audio_buffer_lenght);
}

/* PLL Audio controller to synchonize with video clock */
// Center value is 4566
#define GWENESIS_FRACN_PAL_DOWN 4000
#define GWENESIS_FRACN_PAL_UP 5000

// Center value is 4566
#define GWENESIS_FRACN_NTSC_DOWN 4000
#define GWENESIS_FRACN_NTSC_UP 5000

#define GWENESIS_FRACN_DOWN 3000
#define GWENESIS_FRACN_UP 6000

#define GWENESIS_FRACN_CENTER 4566

/* AUDIO PLL controller */
static void gwenesis_audio_pll_stepdown() {

  __HAL_RCC_PLL2FRACN_DISABLE();
  __HAL_RCC_PLL2FRACN_CONFIG(GWENESIS_FRACN_DOWN);
  __HAL_RCC_PLL2FRACN_ENABLE();
  gwenesis_audio_pll_sync = 0;
}

static void gwenesis_audio_pll_stepup() {

  __HAL_RCC_PLL2FRACN_DISABLE();
  __HAL_RCC_PLL2FRACN_CONFIG(GWENESIS_FRACN_UP);
  __HAL_RCC_PLL2FRACN_ENABLE();
  gwenesis_audio_pll_sync = 0;
}

static void gwenesis_audio_pll_center() {
  if (gwenesis_audio_pll_sync == 0) {
    __HAL_RCC_PLL2FRACN_DISABLE();
    __HAL_RCC_PLL2FRACN_CONFIG(GWENESIS_FRACN_CENTER);
    __HAL_RCC_PLL2FRACN_ENABLE();
    gwenesis_audio_pll_sync = 1;
  }
}

/* single-pole low-pass filter (6 dB/octave) */
const uint32_t factora  = 0x1000; // todo as UI parameter
const uint32_t factorb  = 0x10000 - factora;

static void gwenesis_sound_submit() {
  if (common_emu_sound_loop_is_muted()) {
    return;
  }

  int16_t factor = common_emu_sound_get_volume();
  int16_t* sound_buffer = audio_get_active_buffer();
  uint16_t sound_buffer_length = audio_get_buffer_length();
  static int16_t gwenesis_audio_out = 0;

  if (gwenesis_lpfilter) {
    // filter on
    for (int i = 0; i < sound_buffer_length; i++) {
      // low pass filter
      int32_t gwenesis_audio_tmp = gwenesis_audio_out * factorb + (gwenesis_ym2612_buffer[i] + gwenesis_sn76489_buffer[i]) * factora;
      gwenesis_audio_out = gwenesis_audio_tmp >> 16;
      sound_buffer[i] = ((gwenesis_audio_out) * factor ) / 128;
    }
  } else {
    // filter off
    for (int i = 0; i < sound_buffer_length; i++) {
      // single mone left or right
      // sound_buffer[i] = ((gwenesis_ym2612_buffer[i]) * factor) / 256;
      sound_buffer[i] = ((gwenesis_ym2612_buffer[i] + gwenesis_sn76489_buffer[i]) * factor) / 512;
    }
  }
}

/************************ Debug function in overlay START *******************************/

/* performance monitoring */
/* Emulator loop monitoring
    ( unit is 1/systemcoreclock 1/280MHz or 1/312MHz or 1/340MHz)
    loop_cycles
        -measured duration of the loop.
    end_cycles
        - estimated duration of overall processing.
    */

static unsigned int loop_cycles = 1,end_cycles = 1;

static unsigned int overflow_count = 0;

static void gwenesis_debug_bar()
{

  static unsigned int loop_duration_us = 1;
  static unsigned int end_duration_us = 1;
  static unsigned int cpu_workload = 0;
  static const unsigned int SYSTEM_CORE_CLOCK_MHZ = 353; //340; //312; // 280;

  static bool debug_init_done = false;

  if (!debug_init_done) {
    common_emu_enable_dwt_cycles();
    debug_init_done = true;
  }

  char debugMsg[120];

  end_duration_us = end_cycles / SYSTEM_CORE_CLOCK_MHZ;
  loop_duration_us = loop_cycles / SYSTEM_CORE_CLOCK_MHZ;
  cpu_workload = 100 * end_cycles / loop_cycles;

  if (gwenesis_lcd_current_line == GWENESIS_AUDIOSYNC_START_LCD_LINE)
    sprintf(debugMsg, "%05dus %05dus%3d %6ld %3d SYNC Y%3d", loop_duration_us,
            end_duration_us, cpu_workload, frame_counter, overflow_count,
            gwenesis_lcd_current_line);
  else
    sprintf(debugMsg, "%05dus %05dus%3d %6ld %3d  ..  Y%3d", loop_duration_us,
            end_duration_us, cpu_workload, frame_counter, overflow_count,
            gwenesis_lcd_current_line);


  odroid_overlay_draw_text(0, 0, 320, debugMsg, C_GW_YELLOW, C_GW_RED);

}
/************************ Debug function in overlay END ********************************/
/**************************** */

unsigned int lines_per_frame = LINES_PER_FRAME_NTSC; //262; /* NTSC: 262, PAL: 313 */
unsigned int scan_line;
unsigned int drawFrame = 1;


// static char gwenesis_GameGenie_str[10]="....-....";
// static bool gwenesis_submenu_GameGenie(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
// {

//     // if (event == ODROID_DIALOG_PREV)
//     //     flag_lcd_deflicker_level = flag_lcd_deflicker_level > 0 ? flag_lcd_deflicker_level - 1 : max_flag_lcd_deflicker_level;

//     // if (event == ODROID_DIALOG_NEXT)
//     //     flag_lcd_deflicker_level = flag_lcd_deflicker_level < max_flag_lcd_deflicker_level ? flag_lcd_deflicker_level + 1 : 0;

//     // if (flag_lcd_deflicker_level == 0) strcpy(option->value, "0-none");
//     // if (flag_lcd_deflicker_level == 1) strcpy(option->value, "1-medium");
//     // if (flag_lcd_deflicker_level == 2) strcpy(option->value, "2-high");

//     return event == ODROID_DIALOG_ENTER;
// }

// static char gwenesis_GameGenie_reverse_str[10]="....-....";
// static bool gwenesis_submenu_GameGenie_reverse(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
// {


//     // if (event == ODROID_DIALOG_PREV)
//     //     flag_lcd_deflicker_level = flag_lcd_deflicker_level > 0 ? flag_lcd_deflicker_level - 1 : max_flag_lcd_deflicker_level;

//     // if (event == ODROID_DIALOG_NEXT)
//     //     flag_lcd_deflicker_level = flag_lcd_deflicker_level < max_flag_lcd_deflicker_level ? flag_lcd_deflicker_level + 1 : 0;

//     // if (flag_lcd_deflicker_level == 0) strcpy(option->value, "0-none");
//     // if (flag_lcd_deflicker_level == 1) strcpy(option->value, "1-medium");
//     // if (flag_lcd_deflicker_level == 2) strcpy(option->value, "2-high");

//     return event == ODROID_DIALOG_ENTER;
// }

static bool gwenesis_submenu_setABC(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{

    if (event == ODROID_DIALOG_PREV)
      ABCkeys_value = ABCkeys_value > 0 ? ABCkeys_value - 1 : NB_OF_COMBO-1;

    if (event == ODROID_DIALOG_NEXT)
      ABCkeys_value = ABCkeys_value < NB_OF_COMBO-1 ? ABCkeys_value + 1 : 0;

    strcpy(option->value, ABCkeys_combo_str[ABCkeys_value]);

    switch (ABCkeys_value) {
    case 0:
      PAD_A_def = ODROID_INPUT_B;
      PAD_B_def = ODROID_INPUT_A;
      PAD_C_def = ODROID_INPUT_DEF_C;
      break;
    case 1:
      PAD_A_def = ODROID_INPUT_A;
      PAD_B_def = ODROID_INPUT_B;
      PAD_C_def = ODROID_INPUT_DEF_C;
      break;
    case 2:
      PAD_A_def = ODROID_INPUT_B;
      PAD_B_def = ODROID_INPUT_DEF_C;
      PAD_C_def = ODROID_INPUT_A;
      break;
    case 3:
      PAD_A_def = ODROID_INPUT_A;
      PAD_B_def = ODROID_INPUT_DEF_C;
      PAD_C_def = ODROID_INPUT_B;
      break;
    case 4:
      PAD_A_def = ODROID_INPUT_DEF_C;
      PAD_B_def = ODROID_INPUT_A;
      PAD_C_def = ODROID_INPUT_B;
      break;
    case 5:
      PAD_A_def = ODROID_INPUT_DEF_C;
      PAD_B_def = ODROID_INPUT_B;
      PAD_C_def = ODROID_INPUT_A;
      break;
    default:
      PAD_A_def = ODROID_INPUT_A;
      PAD_B_def = ODROID_INPUT_B;
      PAD_C_def = ODROID_INPUT_DEF_C;
      break;
    }

    return event == ODROID_DIALOG_ENTER;
}

static bool gwenesis_submenu_setAudioFilter(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
    gwenesis_lpfilter = gwenesis_lpfilter == 0 ? 1 : 0;
    }

    if (gwenesis_lpfilter == 0) strcpy(option->value, curr_lang->s_Option_OFF);
    if (gwenesis_lpfilter == 1) strcpy(option->value, curr_lang->s_Option_ON);

    return event == ODROID_DIALOG_ENTER;
}

#if ENABLE_DEBUG_OPTIONS != 0

static bool gwenesis_submenu_debug_bar(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
  if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
      gwenesis_show_debug_bar = gwenesis_show_debug_bar == 0 ? 1 : 0;
    }
    if (gwenesis_show_debug_bar == 0) strcpy(option->value, curr_lang->s_Option_OFF);
    if (gwenesis_show_debug_bar == 1) strcpy(option->value, curr_lang->s_Option_ON);

    return event == ODROID_DIALOG_ENTER;
}

static bool gwenesis_submenu_setVideoUpscaler(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
  if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
    gwenesis_H32upscaler = gwenesis_H32upscaler == 0 ? 1 : 0;
  }

    if (gwenesis_H32upscaler == 0) strcpy(option->value, curr_lang->s_Option_OFF);
    if (gwenesis_H32upscaler == 1) strcpy(option->value, curr_lang->s_Option_ON);

    return event == ODROID_DIALOG_ENTER;
}

static bool gwenesis_submenu_sync_mode(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
  if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
    gwenesis_vsync_mode = gwenesis_vsync_mode == 0 ? 1 : 0;
  }

    if (gwenesis_vsync_mode == 0) strcpy(option->value, curr_lang->s_md_Synchro_Audio);
    if (gwenesis_vsync_mode == 1) strcpy(option->value, curr_lang->s_md_Synchro_Vsync);

    return event == ODROID_DIALOG_ENTER;
}

static char debug_bar_str[2];
static char VideoUpscaler_str[2];
static char gwenesis_sync_mode_str[8];
#endif

static char AudioFilter_str[2];
static char gwenesis_region_str[8];
/* Pending region selection in the menu (0=USA, 1=Europe, 2=Japan).
 * Initialised from gwenesis_detected_region at game start. */
static int gwenesis_pending_region;

static void gwenesis_region_code_to_str(int code, char *buf)
{
    switch (code) {
    case 1:  strcpy(buf, "Europe"); break;
    case 2:  strcpy(buf, "Japan");  break;
    default: strcpy(buf, "USA");    break;
    }
}

static bool gwenesis_submenu_reset(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_ENTER) {
        reset_emulation();
    }
    return event == ODROID_DIALOG_ENTER;
}

static bool gwenesis_submenu_region(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
    if (event == ODROID_DIALOG_PREV)
        gwenesis_pending_region = gwenesis_pending_region > 0 ? gwenesis_pending_region - 1 : 2;
    if (event == ODROID_DIALOG_NEXT)
        gwenesis_pending_region = gwenesis_pending_region < 2 ? gwenesis_pending_region + 1 : 0;

    gwenesis_region_code_to_str(gwenesis_pending_region, option->value);

    if (event == ODROID_DIALOG_ENTER) {
        gwenesis_apply_region_override(gwenesis_pending_region);
        gwenesis_system_init();
        power_on();
        reset_emulation();
        gwenesis_sound_start();
        common_emu_state.frame_time_10us =
            (uint16_t)(100000 / (mode_pal ? 50.0f : 60.0f) + 0.5f);
    }

    return event == ODROID_DIALOG_ENTER;
}

void gwenesis_save_local_data(FILE *file) {
  fwrite((unsigned char *)&ABCkeys_value, 4, 1, file);
  fwrite((unsigned char *)&PAD_A_def, 4, 1, file);
  fwrite((unsigned char *)&PAD_B_def, 4, 1, file);
  fwrite((unsigned char *)&PAD_C_def, 4, 1, file);
  fwrite((unsigned char *)&gwenesis_lpfilter, 4, 1, file);
  // v2: active region code (0=USA, 1=Europe, 2=Japan)
  fwrite((unsigned char *)&gwenesis_pending_region, 4, 1, file);
}

void gwenesis_load_local_data(FILE *file, int ss_version) {
  fread((unsigned char *)&ABCkeys_value, 4, 1, file);
  fread((unsigned char *)&PAD_A_def, 4, 1, file);
  fread((unsigned char *)&PAD_B_def, 4, 1, file);
  fread((unsigned char *)&PAD_C_def, 4, 1, file);
  if (ss_version == 0) {
    uint8_t legacy_audio_filter[16];
    fread(legacy_audio_filter, 1, sizeof(legacy_audio_filter), file);
    fread((unsigned char *)&gwenesis_lpfilter, 4, 1, file);
    switch (gwenesis_lpfilter) {
      case 1:
        strcpy(AudioFilter_str, curr_lang->s_Option_ON);
        break;
      default:
        strcpy(AudioFilter_str, curr_lang->s_Option_OFF);
        break;
    }
  } else {
    fread((unsigned char *)&gwenesis_lpfilter, 4, 1, file);
    switch (gwenesis_lpfilter) {
      case 1:
        strcpy(AudioFilter_str, curr_lang->s_Option_ON);
        break;
      default:
        strcpy(AudioFilter_str, curr_lang->s_Option_OFF);
        break;
    }
    if (ss_version >= 2) {
      int saved_region;
      fread((unsigned char *)&saved_region, 4, 1, file);
      if (saved_region >= 0 && saved_region <= 2 &&
          saved_region != gwenesis_pending_region) {
        gwenesis_pending_region = saved_region;
        gwenesis_apply_region_override(saved_region);
        gwenesis_system_init();
        gwenesis_sound_start();
        common_emu_state.frame_time_10us =
            (uint16_t)(100000 / (mode_pal ? 50.0f : 60.0f) + 0.5f);
      } else {
        gwenesis_pending_region = saved_region;
      }
      gwenesis_region_code_to_str(gwenesis_pending_region, gwenesis_region_str);
    }
  }
}

static bool gwenesis_system_SaveState(const char *savePathName) {
  FILE *file = fopen(savePathName, "wb");
  if (file == NULL) {
      return false;
  }
  gwenesis_savestate_write_file_header(file);
  gwenesis_save_state(file);
  gwenesis_save_local_data(file);
  fclose(file);

  return true;
}

static bool gwenesis_system_LoadState(const char *savePathName) {
  unsigned char header[GWENESIS_SAVESTATE_HEADER_SIZE];
  FILE *file = fopen(savePathName, "rb");
  if (file == NULL) {
      return false;
  }
  if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
      fclose(file);
      return false;
  }
  int ss_version = gwenesis_savestate_version_from_header(header);
  printf("GWENESIS: Loading state version %d\n", ss_version);
  gwenesis_load_state(file, ss_version);
  gwenesis_load_local_data(file, ss_version);
  fclose(file);

  gwenesis_sram_load();

  return true;
}

static void *gwenesis_system_Screenshot()
{
    lcd_wait_for_vblank();

    lcd_clear_active_buffer();
    unsigned short *data = (unsigned short *)lcd_get_active_buffer();

    gwenesis_vdp_set_buffer(&data[vert_screen_offset + hori_screen_offset]);
    for (int l = 0; l < lines_per_frame; l++)
    {
        gwenesis_vdp_render_line(l); /* render scan_line */
    }

    return (void *)data;
}

static void gwenesis_system_SramSave()
{
    gwenesis_sram_save();
}

/* gw_sleep() restores the *settings* OC level on wake, but Genesis forces the
 * maximum OC during gameplay when the user left the setting at 0.  Without this
 * the game keeps running at the (slower) settings clock after a sleep/wake
 * cycle.  Re-apply the boost and reinit audio (SystemClock_Config also
 * reprograms the audio PLL). */
static void gwenesis_sleep_wake_up()
{
    if (odroid_settings_cpu_oc_level_get() == 0) {
        SystemClock_Config(2);
        odroid_audio_init(odroid_audio_sample_rate_get());
        audio_start_playing_full_length(audio_get_buffer_full_length());
    }
}

/* Main */
int app_main_gwenesis(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{

    printf("Genesis start\n");

    ram_start = (uint32_t)&_OVERLAY_MD_BSS_END;

    // Set medium clock speed for better performance if CPU is not overclocked
    // Maximum speed could cause random crash so it should not be used
    if (odroid_settings_cpu_oc_level_get() == 0) {
      SystemClock_Config(2);
  }

    odroid_system_init(APPID_MD, GWENESIS_AUDIO_FREQ_NTSC);
    odroid_system_emu_init(&gwenesis_system_LoadState,
                           &gwenesis_system_SaveState,
                           &gwenesis_system_Screenshot,
                           NULL,
                           &gwenesis_sleep_wake_up,
                           &gwenesis_system_SramSave, NULL);
   // rg_app_desc_t *app = odroid_system_get_app();

    if (start_paused) {
      common_emu_state.pause_after_frames = 2;
      odroid_audio_mute(true);
    } else {
       common_emu_state.pause_after_frames = 0;
    }

    isZelda = !get_ofw_is_mario();
  
    // Set keys mapping
    if (isZelda) {
      ODROID_INPUT_DEF_C = ODROID_INPUT_X;
      static const char zelda_combos[NB_OF_COMBO][10] = {"B-A-START", "A-B-START","B-START-A","A-START-B","START-A-B","START-B-A"};
      memcpy(ABCkeys_combo_str, zelda_combos, sizeof(ABCkeys_combo_str));
      strcpy(ABCkeys_str, "START-B-A");
    } else {
      ODROID_INPUT_DEF_C = ODROID_INPUT_VOLUME;
      static const char mario_combos[NB_OF_COMBO][10] = {"B-A-PAUSE", "A-B-PAUSE","B-PAUSE-A","A-PAUSE-B","PAUSE-A-B","PAUSE-B-A"};
      memcpy(ABCkeys_combo_str, mario_combos, sizeof(ABCkeys_combo_str));
      strcpy(ABCkeys_str, "PAUSE-B-A");
    }
    PAD_C_def = ODROID_INPUT_DEF_C;

    /*** load ROM  */
    load_cartridge();

    /* Region is determined by the ROM header; initialise menu cursor and label. */
    gwenesis_pending_region = gwenesis_detected_region;
    gwenesis_region_code_to_str(gwenesis_pending_region, gwenesis_region_str);

    common_emu_state.frame_time_10us =
        (uint16_t)(100000 / (mode_pal ? 50.0f : 60.0f) + 0.5f);

    gwenesis_system_init();

    power_on();
    reset_emulation();
    hint_counter = 0xff;
    skip_first_vint = 1;

    /* Load battery-backed SRAM: one raw file per game (path basename, not slot) */
    if (gwenesis_sram_enabled) {
      char *sram_path = odroid_system_get_path(ODROID_PATH_SAVE_SRAM, ACTIVE_FILE->path);
      gwenesis_set_sram_path(sram_path);
      free(sram_path);
      gwenesis_sram_load();
    }

    unsigned short *screen = 0;

    screen = lcd_get_active_buffer();
    gwenesis_vdp_set_buffer(&screen[0]);
    extern unsigned char gwenesis_vdp_regs[0x20];
    extern unsigned short gwenesis_vdp_status;
    extern int screen_width, screen_height;
    extern int hint_pending;
    volatile unsigned int current_frame;

    if (load_state) {
        odroid_system_emu_load_state(save_slot);
        hint_counter = (int)gwenesis_vdp_regs[10];
        skip_first_vint = 0;
    } else {
        lcd_clear_buffers();
    }

    /* Start at the same time DMAs audio & video */
    /* Audio period and Video period are the same (almost at least 1 hour) */
    lcd_wait_for_vblank();
    gwenesis_sound_start();
    gwenesis_audio_pll_sync = 1;

    // gwenesis_init_position = 0xFFFF & lcd_get_pixel_position();
    while (true) {

      /* capture the frame processed by the LCD controller */
      current_frame = frame_counter;

      /* clear DWT counter used to monitor performances */
      common_emu_clear_dwt_cycles();

      /* reset watchdog */
      wdog_refresh();

      /* hardware keys */
      odroid_input_read_gamepad(&joystick);

      /* SWAP TIME & PAUSE/SET for MARIO G&W device */
      if (!isZelda) {
        unsigned int key_state = joystick.values[ODROID_INPUT_VOLUME];
        joystick.values[ODROID_INPUT_VOLUME] = joystick.values[ODROID_INPUT_SELECT];
        joystick.values[ODROID_INPUT_SELECT] = key_state;
      }

    odroid_dialog_choice_t options[] = {
        {300, curr_lang->s_Reset, NULL, 1, &gwenesis_submenu_reset},
        {301, curr_lang->s_md_keydefine, ABCkeys_str, 1, &gwenesis_submenu_setABC},
        {302, curr_lang->s_md_AudioFilter, AudioFilter_str, 1, &gwenesis_submenu_setAudioFilter},
        {305, curr_lang->s_md_Region, gwenesis_region_str, 1, &gwenesis_submenu_region},
#if ENABLE_DEBUG_OPTIONS != 0
        {303, curr_lang->s_md_VideoUpscaler, VideoUpscaler_str, 1, &gwenesis_submenu_setVideoUpscaler},
        {304, curr_lang->s_md_Synchro, gwenesis_sync_mode_str, 1, &gwenesis_submenu_sync_mode},
        {310, curr_lang->s_md_Debug_bar, debug_bar_str, 1, &gwenesis_submenu_debug_bar},
#endif
        //  {320, "+GameGenie", gwenesis_GameGenie_str, 0, &gwenesis_submenu_GameGenie},
        //  {330, "-GameGenie", gwenesis_GameGenie_reverse_str, 0, &gwenesis_submenu_GameGenie_reverse},

        ODROID_DIALOG_CHOICE_LAST};

      hint_pending = 0;

      screen_height = REG1_PAL ? 240 : 224;  /* game-selectable: 224 or 240 active lines */
      screen_width = REG12_MODE_H40 ? 320 : 256;
      lines_per_frame = mode_pal ? LINES_PER_FRAME_PAL : LINES_PER_FRAME_NTSC; /* hardware: 313 or 262 */
      vert_screen_offset = 320 * (240 - screen_height) / 2; /* centre 224 or 240 in 320×240 LCD */

      hori_screen_offset = 0; //REG12_MODE_H40 ? 0 : (320 - 256) / 2;

    void _repaint()
    {
        screen = lcd_get_active_buffer();
        gwenesis_vdp_set_buffer(&screen[vert_screen_offset + hori_screen_offset]);
        for (int l = 0; l < lines_per_frame; l++)
        {
            gwenesis_vdp_render_line(l); /* render scan_line */
        }
        common_ingame_overlay();
    }

    common_emu_input_loop(&joystick, options, &_repaint);
    common_emu_input_loop_handle_turbo(&joystick);

    // bool drawFrame =
    common_emu_frame_loop();

      /* Eumulator loop */
      screen = lcd_get_active_buffer();
      gwenesis_vdp_set_buffer(&screen[vert_screen_offset + hori_screen_offset]);

      gwenesis_vdp_render_config();

      /* Reset the difference clocks and audio index */
      system_clock = 0;
      zclk = 0;

      ym2612_clock = 0;
      ym2612_index = 0;

      sn76489_clock = 0;
      sn76489_index = 0;

    scan_line = 0;

    /* Frame loop aligned with Genesis Plus GX system_frame_gen():
     * vblank-first scan order, VINT delay, H-INT on active lines only. */
    {
      const unsigned vint_cycles =
          REG12_MODE_H40 ? VINT_H40_CYCLES : VINT_H32_CYCLES;
      int line;

      gwenesis_vdp_status =
          (unsigned short)((gwenesis_vdp_status & (unsigned short)~0x0112u) |
                           STATUS_VBLANK);
      gwenesis_vdp_status ^= STATUS_ODDFRAME;

      /* First vblank line (=VINT), with optional delay before IRQ. */
      scan_line = screen_height;
      if (!skip_first_vint) {
        gwenesis_run_cpus_to(system_clock + vint_cycles);
        gwenesis_vdp_status |= STATUS_VIRQPENDING;
        if (REG1_VBLANK_INTERRUPT != 0)
          m68k_set_irq(6);
        z80_irq_line(1);
      }
      gwenesis_run_cpus_to(system_clock + VDP_CYCLES_PER_LINE);
      system_clock += VDP_CYCLES_PER_LINE;
      z80_irq_line(0);

      /* Remaining vblank lines (no H-INT counter). */
      for (line = (int)screen_height + 1; line < (int)lines_per_frame - 1;
           line++) {
        scan_line = (unsigned int)line;
        gwenesis_run_cpus_to(system_clock + VDP_CYCLES_PER_LINE);
        system_clock += VDP_CYCLES_PER_LINE;
      }

      /* Last vblank line: reload H-INT counter, clear VBLANK (GPGX). */
      scan_line = lines_per_frame - 1;
      hint_counter = (int)REG10_LINE_COUNTER;
      gwenesis_vdp_status &= (unsigned short)~STATUS_VBLANK;
      gwenesis_run_cpus_to(system_clock + VDP_CYCLES_PER_LINE);
      system_clock += VDP_CYCLES_PER_LINE;

      /* Active display (H-INT before CPU run for each line). */
      for (line = 0; line < (int)screen_height; line++) {
        scan_line = (unsigned int)line;
        gwenesis_vdp_latch_line_scroll(line);
        if (hint_counter == 0) {
          hint_counter = (int)REG10_LINE_COUNTER;
          hint_pending = 1;
          if (REG0_LINE_INTERRUPT)
            m68k_update_irq(4);
        } else {
          hint_counter--;
        }
        gwenesis_run_cpus_to(system_clock + VDP_CYCLES_PER_LINE);
        if (drawFrame)
          gwenesis_vdp_render_line(line);
        system_clock += VDP_CYCLES_PER_LINE;
      }

      skip_first_vint = 0;
    }

      /* Audio
      * synchronize YM2612 and SN76489 to system_clock
      * it completes the missing audio sample for accurate audio mode
      */
      if (GWENESIS_AUDIO_ACCURATE == 1) {
        gwenesis_SN76489_run(system_clock);
        ym2612_run(system_clock);
      }

      // reset m68k cycles to the begin of next frame cycle
      m68k.cycles -= system_clock;

      /* copy audio samples for DMA */
      gwenesis_sound_submit();

      if (gwenesis_show_debug_bar == 1)
        gwenesis_debug_bar();

      if (drawFrame)
        common_ingame_overlay();

      end_cycles = common_emu_get_dwt_cycles();

      /* VSYNC mode */
      if (gwenesis_vsync_mode) {
        /* Check if we are still in the same frame as at the beginning of the
         * loop if it's different we are in overflow : skip next frame using
         * (drawFrame = 0) otherwise we are in the same frame. wait the end of
         * frame.
         */
        if (current_frame != frame_counter) {
          overflow_count++;
          drawFrame = 0;

        } else {
          lcd_swap();
          drawFrame = 1;
          lcd_sleep_while_swap_pending();
        }

        /* AUDIO SYNC mode */
        /* default as Audio/Video are synchronized */
      } else {

        lcd_swap();

        common_emu_state.pause_frames = 0;
        common_emu_state.skip_frames = 0;
        common_emu_sound_sync(gwenesis_show_debug_bar);
      }
      // Get current line LCD position to check A/V synchronization
      gwenesis_lcd_current_line = 0xFFFF & lcd_get_pixel_position();

      /*  SYNC A/V */
      if (gwenesis_lcd_current_line > GWENESIS_AUDIOSYNC_START_LCD_LINE)
        gwenesis_audio_pll_stepup();
      if (gwenesis_lcd_current_line < GWENESIS_AUDIOSYNC_START_LCD_LINE)
        gwenesis_audio_pll_stepdown();
      if (gwenesis_lcd_current_line == GWENESIS_AUDIOSYNC_START_LCD_LINE)
        gwenesis_audio_pll_center();

      /* get how cycles have been spent inside this loop */
      loop_cycles = common_emu_get_dwt_cycles();

    } // end of loop
}
