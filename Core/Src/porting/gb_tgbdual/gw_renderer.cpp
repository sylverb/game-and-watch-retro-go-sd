/*--------------------------------------------------
   TGB Dual - Gameboy Emulator -
   Copyright (C) 2023  Sylver Bruneau

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

// G&W implementation of the renderer
extern "C" {
#include <odroid_system.h>
#include "common.h"
#include "gw_lcd.h"
#include "main_gb_tgbdual.h"
#include "rg_rtc.h"
}

#include <string.h>
#include <math.h>
#include <time.h>

#include "gw_renderer.h"
#include "gb_core/gb.h"

#define SAMPLES_PER_FRAME (44100/60)

static int16_t stream[SAMPLES_PER_FRAME*2];

extern void gb_blit(uint16_t *buffer);
extern void gb_pcm_submit(int16_t *stream, int samples);

gw_renderer::gw_renderer(int which)
{
   which_gb = which;
   fixed_time = 0;
   cur_time = 0;
   cal_valid = false;
}

word gw_renderer::map_color(word gb_col)
{
   return ((gb_col&0x001f) << 11) |
      ((gb_col&0x03e0) <<  1) |
      ((gb_col&0x0200) >>  4) |
      ((gb_col&0x7c00) >> 10);
}

void gw_renderer::refresh() {
   this->snd_render->render(stream, SAMPLES_PER_FRAME);
   gb_pcm_submit(stream, SAMPLES_PER_FRAME);
   /* ~1 Hz wall clock is enough for MBC3; avoid time(NULL)/mktime @60Hz. */
   static uint8_t sync_div;
   if (!cal_valid || ++sync_div >= 60) {
      sync_div = 0;
      struct tm tm;
      GW_GetUnixTM(&tm);
      fixed_time = mktime(&tm);
      cal_year = (byte)(tm.tm_year % 100);
      cal_month = (byte)(tm.tm_mon + 1);
      cal_day = (byte)tm.tm_mday;
      cal_hour = (byte)tm.tm_hour;
      cal_minute = (byte)tm.tm_min;
      cal_second = (byte)tm.tm_sec;
      cal_valid = true;
   }
}

int gw_renderer::check_pad()
{
   odroid_gamepad_state_t joystick;
   odroid_input_read_gamepad(&joystick);

   if (joystick.values[ODROID_INPUT_VOLUME]) {
      // if Pause button is pressed, ignore
      // pressed keys as it is used for some
      // shortcuts (contrasts, volume, ...)
      return 0;
   }

   int joystick_state = 0;
   if (joystick.values[ODROID_INPUT_LEFT]) {
      joystick_state |= 1<<6;
   }
   if (joystick.values[ODROID_INPUT_RIGHT]) {
      joystick_state |= 1<<7;
   }
   if (joystick.values[ODROID_INPUT_UP]) {
      joystick_state |= 1<<5;
   }
   if (joystick.values[ODROID_INPUT_DOWN]) {
      joystick_state |= 1<<4;
   }
   if (joystick.values[ODROID_INPUT_A]) {
      joystick_state |= 1<<0;
   }
   if (joystick.values[ODROID_INPUT_B]) {
      joystick_state |= 1<<1;
   }
   // Game button on G&W
   if (joystick.values[ODROID_INPUT_START]) {
      joystick_state |= 1<<3;
   }
   // Time button on G&W
   if (joystick.values[ODROID_INPUT_SELECT]) {
      joystick_state |= 1<<2;
   }
   // Start button on Zelda G&W
   if (joystick.values[ODROID_INPUT_X]) {
      joystick_state |= 1<<3;
   }
   // Select button on Zelda G&W
   if (joystick.values[ODROID_INPUT_Y]) {
      joystick_state |= 1<<2;
   }

   // update pad state: a,b,select,start,down,up,left,right
   return joystick_state;
}

void gw_renderer::render_screen(byte *buf,int width,int height,int depth)
{
   if (tgb_drawFrame) {
      gb_blit((uint16_t *)buf);
      lcd_swap();
   }
}

byte gw_renderer::get_time(int type)
{
   dword now = fixed_time-cur_time;

   switch(type)
   {
      case 8: // second
         return (byte)(now%60);
      case 9: // minute
         return (byte)((now/60)%60);
      case 10: // hour
         return (byte)((now/(60*60))%24);
      case 11: // day (L)
         return (byte)((now/(24*60*60))&0xff);
      case 12: // day (H)
         return (byte)((now/(256*24*60*60))&1);
   }
   return 0;
}

void gw_renderer::set_time(int type,byte dat)
{
   dword now = fixed_time;
   dword adj = now - cur_time;

   switch(type)
   {
      case 8: // second
         adj = (adj/60)*60+(dat%60);
         break;
      case 9: // minute
         adj = (adj/(60*60))*60*60+(dat%60)*60+(adj%60);
         break;
      case 10: // hour
         adj = (adj/(24*60*60))*24*60*60+(dat%24)*60*60+(adj%(60*60));
         break;
      case 11: // day (L)
         adj = (adj/(256*24*60*60))*256*24*60*60+(dat*24*60*60)+(adj%(24*60*60));
         break;
      case 12: // day (H)
         adj = (dat&1)*256*24*60*60+(adj%(256*24*60*60));
         break;
   }
   cur_time = now - adj;
}

void gw_renderer::get_calendar_time(byte *year,byte *month,byte *day,
                                    byte *hour,byte *minute,byte *second)
{
   if (!cal_valid) {
      struct tm tm;
      GW_GetUnixTM(&tm);
      cal_year = (byte)(tm.tm_year % 100);
      cal_month = (byte)(tm.tm_mon + 1);
      cal_day = (byte)tm.tm_mday;
      cal_hour = (byte)tm.tm_hour;
      cal_minute = (byte)tm.tm_min;
      cal_second = (byte)tm.tm_sec;
      cal_valid = true;
   }
   if (year) *year = cal_year;
   if (month) *month = cal_month;
   if (day) *day = cal_day;
   if (hour) *hour = cal_hour;
   if (minute) *minute = cal_minute;
   if (second) *second = cal_second;
}

