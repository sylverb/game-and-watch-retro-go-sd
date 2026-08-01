#include <odroid_system.h>
             
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "porting.h"
#include "crc32.h"
#include "gw_lcd.h"

#include "audio.h"
#include "o2em_config.h"
#include "cpu.h"
#include "keyboard.h"
#include "score.h"
#include "vdc.h"
#include "vmachine.h"
#include "voice.h"
#include "vpp.h"
#include "vkeyb/vkeyb.h"

#include "wrapalleg.h"

#define APP_ID 20

#define BPP      2
#define SCALE    2

#define AUDIO_SAMPLE_RATE   (48000)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60)

#define EMUWIDTH 320
#define EMUHEIGHT 240

#define TEX_WIDTH 400
#define TEX_HEIGHT 300

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *fb_texture;
uint16_t fb_data[EMUWIDTH * EMUHEIGHT * BPP];

static int current_height, current_width;

extern unsigned char cart_rom[];
extern unsigned int cart_rom_len;

//The frames per second cap timer
int capTimer;

//#define RGB565(r, g, b)   ((((r) << 8) &  0xf800) | (((g) << 3) & 0x7e0) | (((b) >> 3) & 0x1f))
//#define ABGR1555(r, g, b) ((((b) << 7) &  0x7C00) | (((g) << 2) & 0x3e0) | (((r) >> 3) & 0x1f))

// Data used by O2EM
uint8_t soundBuffer[SOUND_BUFFER_LEN];

int RLOOP=0;
int joystick_data[2][5]={{0,0,0,0,0},{0,0,0,0,0}};
uint16_t *mbmp = fb_data;
//uint16_t mbmp[TEX_WIDTH * TEX_HEIGHT];

void update_joy(void)
{
}

///////////
uint32_t allocated_ram = 0;
uint32_t ahb_allocated_ram = 0;
uint32_t itc_allocated_ram = 0;

void *ahb_calloc(size_t count,size_t size)
{
   ahb_allocated_ram+=size*count;
//   printf("ahb_calloc %zu bytes (new total = %u)\n",size*count,ahb_allocated_ram);
   void *ret = calloc(count,size);
   return ret;
}

void *ahb_malloc(size_t size)
{
   ahb_allocated_ram+=size;
//   printf("ahb_malloc %zu bytes (new total = %u)\n",size,ahb_allocated_ram);
   void *ret = malloc(size);
   return ret;
}

void *itc_calloc(size_t count,size_t size)
{
   itc_allocated_ram+=size*count;
//   printf("itc_calloc %zu bytes (new total = %u)\n",size*count,itc_allocated_ram);
   void *ret = calloc(count,size);
   return ret;
}

void *itc_malloc(size_t size)
{
   itc_allocated_ram+=size;
//   printf("itc_malloc %zu bytes (new total = %u)\n",size,itc_allocated_ram);
   void *ret = malloc(size);
   return ret;
}

int init_window(int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return 0;

    window = SDL_CreateWindow("emulator",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width * SCALE, height * SCALE,
        0);
    if (!window)
        return 0;

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
        return 0;

    fb_texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if (!fb_texture)
        return 0;

    return 0;
}

static bool LoadState(char *savePathName, char *sramPathName)
{
    printf("Loading state from %s...\n", savePathName);

	return 0;
}

static bool SaveState(char *savePathName, char *sramPathName)
{
    printf("Saving state to %s...\n", savePathName);

	return 0;  
}

void pcm_submit(void)
{

}

void init(void)
{
    printf("init()\n");
    odroid_system_init(APP_ID, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, NULL, NULL, NULL, NULL, NULL);

    // Video
    memset(fb_data, 0, sizeof(fb_data));
}

static bool load_bios(const char *biosname)
{
   FILE *bios_file = NULL;
   int64_t bytes_read;
   uint32_t crc;
   size_t i;

   printf("load_bios \n");
   if (!biosname)
      return false;

   bios_file = fopen(biosname,"rb");

   if (!bios_file)
   {
      printf("[O2EM]: Error loading BIOS ROM (%s).\n", biosname);
      return false;
   }

   bytes_read = fread(rom_table[0], 1, 1024, bios_file);
   fclose(bios_file);

   if (bytes_read != 1024)
   {
      printf("[O2EM]: Error loading BIOS ROM (%s).\n", biosname);
      return false;
   }

   for (i=1; i<8; i++)
      memcpy(rom_table[i],rom_table[0],1024);

   crc = crc32_le(0, rom_table[0], 1024);

   switch (crc)
   {
      case 0x8016A315:
         printf("[O2EM]: Magnavox Odyssey2 BIOS ROM loaded (G7000 model)\n");
         app_data.vpp  = 0;
         app_data.bios = ROM_O2;
         break;
      case 0xE20A9F41:
         printf("[O2EM]: Philips Videopac+ European BIOS ROM loaded (G7400 model)\n");
         app_data.vpp  = 1;
         app_data.bios = ROM_G7400;
         break;
      case 0xA318E8D6:
         printf("[O2EM]: Philips Videopac+ French BIOS ROM loaded (G7000 model)\n");
         app_data.vpp  = 0;
         app_data.bios = ROM_C52;
         break;
      case 0x11647CA5:
         printf("[O2EM]: Philips Videopac+ French BIOS ROM loaded (G7400 model)\n");
         app_data.vpp  = 1;
         app_data.bios = ROM_JOPAC;
         break;
      default:
         printf("[O2EM]: BIOS ROM loaded (unknown version)\n");
         app_data.vpp  = 0;
         app_data.bios = ROM_UNKNOWN;
         break;
   }

   return true;
}

static bool load_cart(const uint8_t *data, size_t size)
{
   int i, nb;

   /* Get ROM CRC */
   app_data.crc = crc32_le(0, data, size);

   if (app_data.crc == 0xAFB23F89)
      app_data.exrom = 1;  /* Musician */
   if (app_data.crc == 0x3BFEF56B)
      app_data.exrom = 1;  /* Four in 1 Row! */
   if (app_data.crc == 0x9B5E9356)
      app_data.exrom = 1;  /* Four in 1 Row! (french) */

   if (((app_data.crc == 0x975AB8DA) || (app_data.crc == 0xE246A812)))
   {
      printf("[O2EM]: Loaded content is an incomplete ROM dump.\n");
      return false;
   }

   if ((size % 1024) != 0)
   {
      printf("[O2EM]: Error: Loaded content is an invalid ROM dump.\n");
      return false;
   }

   /* special MegaCART design by Soeren Gust */
   if ((size ==   32768) ||
       (size ==   65536) ||
       (size ==  131072) ||
       (size ==  262144) ||
       (size ==  524288) ||
       (size == 1048576))
   {
      app_data.megaxrom = 1;
      app_data.bank     = 1;
      megarom           = malloc(1048576);

      if (!megarom)
      {
         printf("[O2EM]: Out of memory while processing loaded content.\n");
         return false;
      }

      memcpy(megarom, data, size);

      /* mirror shorter files into full megabyte */
      if (size <   65536)
         memcpy(megarom +  32768, megarom,  32768);
      if (size <  131072)
         memcpy(megarom +  65536, megarom,  65536);
      if (size <  262144)
         memcpy(megarom + 131072, megarom, 131072);
      if (size <  524288)
         memcpy(megarom + 262144, megarom, 262144);
      if (size < 1048576)
         memcpy(megarom + 524288, megarom, 524288);

      /* start in bank 0xff */
      memcpy(&rom_table[0][1024], megarom + 4096*255 + 1024, 3072);

      printf("[O2EM]: MegaCart %luK\n", (unsigned long)(size / 1024));
      nb = 1;
   }
   else if (((size % 3072) == 0))
   {
      app_data.three_k = 1;
      nb               = size / 3072;

      for (i = (nb - 1); i >= 0; i--)
      {
         memcpy(&rom_table[i][1024], data, 3072);
         data += 3072;
      }

      printf("[O2EM]: %uK\n", (unsigned)(nb * 3));
   }
   else
   {
      nb = size / 2048;

      if ((nb == 2) && (app_data.exrom))
      {
         memcpy(&extROM[0], data, 1024);
         data += 1024;

         memcpy(&rom_table[0][1024], data, 3072);
         data += 3072;

         printf("[O2EM]: 3K EXROM\n");
      }
      else
      {
         for (i = (nb - 1); i >= 0; i--)
         {
            memcpy(&rom_table[i][1024], data, 2048);
            data += 2048;

            /* simulate missing A10 */
            memcpy(&rom_table[i][3072], &rom_table[i][2048], 1024);
         }

         printf("[O2EM]: %uK\n", (unsigned)(nb * 2));
      }
   }

   o2em_rom = rom_table[0];
   if (nb == 1)
      app_data.bank = 1;
   else if (nb == 2)
      app_data.bank = app_data.exrom ? 1 : 2;
   else if (nb == 4)
      app_data.bank = 3;
   else
      app_data.bank = 4;

   if ((rom_table[nb-1][1024+12]=='O') &&
       (rom_table[nb-1][1024+13]=='P') &&
       (rom_table[nb-1][1024+14]=='N') &&
       (rom_table[nb-1][1024+15]=='B'))
      app_data.openb=1;

   return true;
}

void load_data()
{
    char bios_file_path[255];
    const uint8_t *rom_data              = NULL;
    size_t rom_size                      = 0;

    rom_data = (const uint8_t *)cart_rom;
    rom_size = cart_rom_len;

    app_data.stick[0] = app_data.stick[1] = 1;
    app_data.sticknumber[0] = app_data.sticknumber[1] = 0;
    set_defjoykeys(0,0);
    set_defjoykeys(1,1);
    set_defsystemkeys();
    app_data.bank = 0;
    app_data.limit = 1;
    app_data.sound_en = 1;
    app_data.speed = 100;
    app_data.wsize = 2;
    app_data.scanlines = 0;
    app_data.voice = 1;
    /* These volume settings have no effect
        * (they are allegro-specific) */
    app_data.svolume = 100;
    app_data.vvolume = 100;
    /* Internal audio filter is worthless,
        * disable it and use our own */
    app_data.filter = 0;
    app_data.exrom = 0;
    app_data.three_k = 0;
    app_data.crc = 0;
    app_data.openb = 0;
    app_data.vpp = 0;
    app_data.bios = 0;
    app_data.scoretype = 0;
    app_data.scoreaddress = 0;
    app_data.default_highscore = 0;
    app_data.breakpoint = 65535;
    app_data.megaxrom = 0;

    init_audio();

   if (!load_bios("./bios.bin"))
      return;
   if (!load_cart(rom_data, rom_size))
      return;

}

extern unsigned char key[256*2];
#define RETROK_RETURN 13
void odroid_input_read_gamepad_o2em(odroid_gamepad_state_t* out_state)
{
    SDL_Event event;
    static SDL_Event last_down_event;

    if (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN) {
            // printf("Press %d\n", event.key.keysym.sym);
            switch (event.key.keysym.sym) {
            case SDLK_LSHIFT:
                key[RETROK_RETURN] = 1;
                break;
            case SDLK_LCTRL:
//                key[RETROK_RETURN] = 1;
                break;
            case SDLK_x:
                joystick_data[0][4] = 1;
                break;
            case SDLK_z:
                joystick_data[0][4] = 1;
                break;
            case SDLK_UP:
                joystick_data[0][0] = 1;
                break;
            case SDLK_DOWN:
                joystick_data[0][1] = 1;
                break;
            case SDLK_LEFT:
                joystick_data[0][2] = 1;
                break;
            case SDLK_RIGHT:
                joystick_data[0][3] = 1;
                break;
            case SDLK_ESCAPE:
                exit(1);
                break;
            default:
                break;
            }
            last_down_event = event;
        } else if (event.type == SDL_KEYUP) {
            // printf("Release %d\n", event.key.keysym.sym);
            switch (event.key.keysym.sym) {
            case SDLK_LSHIFT:
                key[RETROK_RETURN] = 0;
                break;
            case SDLK_x:
                joystick_data[0][4] = 0;
                break;
            case SDLK_z:
                joystick_data[0][4] = 0;
                break;
            case SDLK_UP:
                joystick_data[0][0] = 0;
                break;
            case SDLK_DOWN:
                joystick_data[0][1] = 0;
                break;
            case SDLK_LEFT:
                joystick_data[0][2] = 0;
                break;
            case SDLK_RIGHT:
                joystick_data[0][3] = 0;
                break;
            case SDLK_F1:
                if (last_down_event.key.keysym.sym == SDLK_F1)
                    SaveStateStm("save_o2em.bin");
                break;
            case SDLK_F4:
                if (last_down_event.key.keysym.sym == SDLK_F4)
                    LoadStateStm("save_o2em.bin");
                break;
            default:
                break;
            }
        }
    }
}

void blit(uint16_t *buffer) {
    // we want 60 Hz for NTSC
//    int wantedTime = 1000 / 60;
//    SDL_Delay(wantedTime); // rendering takes basically "0ms"

/*    for (int y = 0; y < EMUWIDTH; y++) {
        memcpy((void *)(&fb_data[y*EMUHEIGHT]),(void *)buffer,EMUHEIGHT*4);
        buffer+=TEX_WIDTH;
    }*/

    SDL_UpdateTexture(fb_texture, NULL, fb_data, EMUWIDTH * BPP);
    SDL_RenderCopy(renderer, fb_texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

int main(int argc, char *argv[])
{
    RLOOP=1;
    init_window(EMUWIDTH, EMUHEIGHT);

    init();
    odroid_gamepad_state_t joystick = {0};

    load_data();

    init_display();
    init_cpu();
    init_system();

    set_score(app_data.scoretype, app_data.scoreaddress, app_data.default_highscore);


    app_data.euro = 1;

    while (true)
    {

        //Start cap timer
        capTimer = SDL_GetTicks();
        //wdog_refresh();
        bool drawFrame = true;// common_emu_frame_loop();

        odroid_input_read_gamepad_o2em(&joystick);

//   update_input();

        RLOOP=1;
        cpu_exec();
        blit(mbmp);

        printf("%02x %02x \n",soundBuffer[0],soundBuffer[1]);
/*        if (blend_frames)
            blend_frames();

        if (vkb_show)
            vkb_show_virtual_keyboard();

        if (crop_overscan)
        {
            uint16_t *mbmp_cropped = mbmp + (TEX_WIDTH * CROPPED_OFFSET_Y) + CROPPED_OFFSET_X;
            video_cb(mbmp_cropped, CROPPED_WIDTH, CROPPED_HEIGHT, TEX_WIDTH << 1);
        }
        else
            video_cb(mbmp, EMUWIDTH, EMUHEIGHT, TEX_WIDTH << 1);
*/
            if(drawFrame) {
//                upate_audio();
            }
    }

    SDL_Quit();

    return 0;
}
