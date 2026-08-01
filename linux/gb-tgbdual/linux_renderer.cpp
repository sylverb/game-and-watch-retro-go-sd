/*--------------------------------------------------
   TGB Dual - Gameboy Emulator -
   Copyright (C) 2001  Hii

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

// libretro implementation of the renderer, should probably be renamed from dmy.

extern "C" {
#include <odroid_system.h>
}

#include <string.h>
#include <math.h>
#include <time.h>

#include "linux_renderer.h"
#include "gb_core/gb.h"

#define SAMPLES_PER_FRAME (44100/60)

extern gb *g_gb;
static int16_t stream[SAMPLES_PER_FRAME*2];

extern void blit(uint16_t *buffer);
extern void gb_pcm_submit(int16_t *stream, int samples);
extern int input_read_gamepad();
linux_renderer::linux_renderer(int which)
{
   which_gb = which;
}

word linux_renderer::map_color(word gb_col)
{
   return ((gb_col&0x001f) << 11) |
      ((gb_col&0x03e0) <<  1) |
      ((gb_col&0x0200) >>  4) |
      ((gb_col&0x7c00) >> 10);
}

void linux_renderer::refresh() {
   this->snd_render->render(stream, SAMPLES_PER_FRAME);
   gb_pcm_submit(stream, SAMPLES_PER_FRAME);
   fixed_time = time(NULL);
}

int linux_renderer::check_pad()
{
   // update pad state: a,b,select,start,down,up,left,right
   return input_read_gamepad();
}

void linux_renderer::render_screen(byte *buf,int width,int height,int depth)
{
   blit((uint16_t *)buf);
}

byte linux_renderer::get_time(int type)
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

void linux_renderer::set_time(int type,byte dat)
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

void linux_renderer::get_calendar_time(byte *year,byte *month,byte *day,
                                       byte *hour,byte *minute,byte *second)
{
   time_t t = fixed_time ? (time_t)fixed_time : time(NULL);
   struct tm *tm = localtime(&t);
   if (!tm) {
      if (year) *year = 0;
      if (month) *month = 1;
      if (day) *day = 1;
      if (hour) *hour = 0;
      if (minute) *minute = 0;
      if (second) *second = 0;
      return;
   }
   if (year) *year = (byte)(tm->tm_year % 100);
   if (month) *month = (byte)(tm->tm_mon + 1);
   if (day) *day = (byte)tm->tm_mday;
   if (hour) *hour = (byte)tm->tm_hour;
   if (minute) *minute = (byte)tm->tm_min;
   if (second) *second = (byte)tm->tm_sec;
}

