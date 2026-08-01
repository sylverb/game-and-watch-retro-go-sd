#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#include <odroid_system.h>

#include "porting.h"
#include "crc32.h"

#include <string.h>
#include "fceu.h"
#include "fceu-state.h"
#include "driver.h"
#include "video.h"
#include "fds.h"
#include "rom_manager.h"
#include "streams/memory_stream.h"

#define WIDTH  320
#define HEIGHT 240
#define BPP      2
#define SCALE    2


#define APP_ID 30

#define AUDIO_SAMPLE_RATE   (48000)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60)

uint8 *UNIFchrrama = 0;
const rom_manager_t rom_mgr;

const rom_system_t *rom_manager_system(const rom_manager_t *mgr, char *name) {
    for(int i=0; i < mgr->systems_count; i++) {
        if(strcmp(mgr->systems[i]->system_name, name) == 0) {
            return mgr->systems[i];
        }
    }
    return NULL;
}

const retro_emulator_file_t *rom_manager_get_file(const rom_system_t *system, const char *name)
{
    for(int i=0; i < system->roms_count; i++) {
        if (strlen(name) == (strlen(system->roms[i].name) + strlen(system->roms[i].ext) + 1)) {
            if((strncmp(system->roms[i].name, name,strlen(system->roms[i].name)) == 0) &&
               (name[strlen(system->roms[i].name)] == '.') &&
               (strncmp(system->roms[i].ext, name+strlen(system->roms[i].name)+1,strlen(system->roms[i].ext)) == 0)) {
                return &system->roms[i];
            }
        }
    }
    return NULL;
}

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *fb_texture;
uint16_t fb_data[WIDTH * HEIGHT * BPP];
SDL_AudioSpec wanted;
int16_t audio_buffer[30000];
int16_t audio_buffer_index;


static uint8_t nes_framebuffer[(256+16)*240];

static uint samplesPerFrame;
static uint32_t vsync_wait_ms = 0;

static uint8_t save_slot_load = 0;

static int32_t *sound = 0;
int32_t ssize = 0;

unsigned overclock_enabled = -1;
unsigned overclocked = 0;
unsigned skip_7bit_overclocking = 1; /* 7-bit samples have priority over overclocking */
unsigned totalscanlines = 0;
unsigned normal_scanlines = 240;
unsigned extrascanlines = 0;
unsigned vblankscanlines = 0;
unsigned dendy = 0;
unsigned swapDuty = 0;

static bool crop_overscan_h = 0;
static bool crop_overscan_v = 0;
static bool use_raw_palette;
static int aspect_ratio_par;

#define RED_SHIFT 11
#define GREEN_SHIFT 5
#define BLUE_SHIFT 0
#define RED_EXPAND 3
#define GREEN_EXPAND 2
#define BLUE_EXPAND 3
#define RED_MASK 0xF800
#define GREEN_MASK 0x7e0
#define BLUE_MASK 0x1f

uint32 allocated_ram = 0;
uint32 ahb_allocated_ram = 0;
uint32 itc_allocated_ram = 0;

#define BUILD_PIXEL_RGB565(R,G,B) (((int) ((R)&0x1f) << RED_SHIFT) | ((int) ((G)&0x3f) << GREEN_SHIFT) | ((int) ((B)&0x1f) << BLUE_SHIFT))
static uint16_t retro_palette[1024];

void FCEUD_SetPalette(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= 256)
        return;
   retro_palette[index] = BUILD_PIXEL_RGB565(r >> RED_EXPAND, g >> GREEN_EXPAND, b >> BLUE_EXPAND);
}

void FCEUD_PrintError(char *c)
{
    printf("%s", c);
}

void FCEUD_DispMessage(enum retro_log_level level, unsigned duration, const char *str)
{
    printf("%s", str);
}

void FCEUD_Message(char *s)
{
    printf("%s", s);
}

void *ahb_calloc(size_t count,size_t size)
{
   ahb_allocated_ram+=size*count;
   printf("ahb_calloc %zu bytes (new total = %u)\n",size*count,ahb_allocated_ram);
   void *ret = calloc(count,size);
   return ret;
}

void *ahb_malloc(size_t size)
{
   ahb_allocated_ram+=size;
   printf("ahb_malloc %zu bytes (new total = %u)\n",size,ahb_allocated_ram);
   void *ret = malloc(size);
   return ret;
}

void *itc_calloc(size_t count,size_t size)
{
   itc_allocated_ram+=size*count;
   printf("itc_calloc %zu bytes (new total = %u)\n",size*count,itc_allocated_ram);
   void *ret = calloc(count,size);
   return ret;
}

void *itc_malloc(size_t size)
{
   itc_allocated_ram+=size;
   printf("itc_malloc %zu bytes (new total = %u)\n",size,itc_allocated_ram);
   void *ret = malloc(size);
   return ret;
}

void *FCEU_gmalloc(uint32 size)
{
   allocated_ram+=size;
   printf("FCEU_gmalloc %d bytes (new total = %d)\n",size,allocated_ram);
   void *ret = calloc(1,size);
   return ret;
}

void *FCEU_malloc(uint32 size)
{
   allocated_ram+=size;
   printf("FCEU_malloc %d bytes (new total = %d)\n",size,allocated_ram);
   void *ret = (void*)calloc(1,size);
   return ret;
}

void FCEU_free(void *ptr)
{
    printf("FCEU_free\n");
    free(ptr);
}

void FCEU_gfree(void *ptr)
{
    printf("FCEU_gfree\n");
    free(ptr);
}

/*-------------------------------*/

static bool SaveState(const char *savePathName)
{
    FCEUSS_Save_Fs(savePathName);
    return true;
}

static bool LoadState(const char *savePathName)
{
    FCEUSS_Load_Fs(savePathName);
    /* Rebuild the RGB565 LUT (built-in default palette) after load so colors and
     * deemphasis match a fresh start, mirroring the G&W port. The linux harness
     * has no custom palette support, so this always restores the default. */
    FCEUI_SetPaletteArray(NULL, 0);
    return true;
}

extern unsigned char cart_rom[];
extern unsigned int cart_rom_len;

static uint32_t fceu_joystick; /* player input data, 1 byte per player (1-4) */

static bool run_loop = true;
void input_read_gamepad()
{
    SDL_Event event;
    uint8_t input_buf  = fceu_joystick;

    if (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
            case SDLK_x:
                input_buf |= JOY_A;
                break;
            case SDLK_z:
                input_buf |= JOY_B;
                break;
            case SDLK_LSHIFT:
                input_buf |= JOY_START;
                break;
            case SDLK_LCTRL:
                input_buf |= JOY_SELECT;
                break;
            case SDLK_UP:
                input_buf |= JOY_UP;
                break;
            case SDLK_DOWN:
                input_buf |= JOY_DOWN;
                break;
            case SDLK_LEFT:
                input_buf |= JOY_LEFT;
                break;
            case SDLK_RIGHT:
                input_buf |= JOY_RIGHT;
                break;
            case SDLK_ESCAPE:
                run_loop = false;
                break;
            case SDLK_F1:
                FCEU_FDSInsert(-1);
                break;
            case SDLK_F2:
                FCEU_FDSSelect();
                break;
            case SDLK_F3:
                SaveState("./save.bin");
                break;
            case SDLK_F4:
                LoadState("./save.bin");
                break;
            default:
                break;
            }
        }
        if (event.type == SDL_KEYUP) {
            switch (event.key.keysym.sym) {
            case SDLK_x:
                input_buf &= ~JOY_A;
                break;
            case SDLK_z:
                input_buf &= ~JOY_B;
                break;
            case SDLK_LSHIFT:
                input_buf &= ~JOY_START;
                break;
            case SDLK_LCTRL:
                input_buf &= ~JOY_SELECT;
                break;
            case SDLK_UP:
                input_buf &= ~JOY_UP;
                break;
            case SDLK_DOWN:
                input_buf &= ~JOY_DOWN;
                break;
            case SDLK_LEFT:
                input_buf &= ~JOY_LEFT;
                break;
            case SDLK_RIGHT:
                input_buf &= ~JOY_RIGHT;
                break;
            case SDLK_ESCAPE:
                run_loop = false;
                break;
            default:
                break;
            }
        }
    }

    fceu_joystick = input_buf;
}

void odroid_display_force_refresh(void)
{
    // forceVideoRefresh = true;
}

void nes_blitscreen(uint8_t *buffer)
{
    static uint32_t lastFPSTime = 0;
    static uint32_t lastTime = 0;
    static uint32_t frames = 0;

    frames++;
    uint32_t currentTime = SDL_GetTicks();
    float delta = currentTime - lastFPSTime;
    if (delta >= 1000) {
//        printf("FPS: %f\n", ((float)frames / (delta / 1000.0f)));
        frames = 0;
        lastFPSTime = currentTime;
    }

    // we want 60 Hz for NTSC
    int wantedTime = 1000 / 60;
//    SDL_Delay(wantedTime); // rendering takes basically "0ms"
    lastTime = currentTime;

    unsigned x, y;
    unsigned incr   = 0;
    unsigned width  = 256;
    unsigned height = 240;
    unsigned offset_x  = (WIDTH - width) / 2;
    unsigned offset_y  = crop_overscan_v?8:0;

    incr   += (crop_overscan_h ? 16 : 0);
    width  -= (crop_overscan_h ? 16 : 0);
    height -= (crop_overscan_v ? 16 : 0);
    buffer += (crop_overscan_v ? ((crop_overscan_h ? 8 : 0) + 256 * 8) : (crop_overscan_h ? 8 : 0));
    offset_x += (crop_overscan_h ? 8 : 0);

    for (y = 0; y < height; y++, buffer += incr) {
        for (x = 0; x < width; x++, buffer++) {
            fb_data[(y+offset_y) * WIDTH + offset_x + x] = retro_palette[*buffer];
        }
    }

    SDL_UpdateTexture(fb_texture, NULL, fb_data, WIDTH * BPP);
    SDL_RenderCopy(renderer, fb_texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

static volatile int *Buffer = 0;
static unsigned int BufferSize;
static unsigned int BufferRead;
static unsigned int BufferWrite;
static volatile unsigned int BufferIn;
long soundbufsize=240;

static void fillaudio(void *udata, uint8 *stream, int len)
{
 int16 *tmps = (int16*)stream;
 len >>= 1;

 while(len)
 {
  int16 sample = 0;
  if(BufferIn)
  {
   sample = Buffer[BufferRead];
   BufferRead = (BufferRead + 1) % BufferSize;
   BufferIn--;
  }
  else sample = 0;

  *tmps = sample;
  tmps++;
  len--;
 }
}

uint32 GetMaxSound(void)
{
 return(BufferSize);
}

uint32 GetWriteSound(void)
{
 return(BufferSize - BufferIn);
}

void WriteSound(int32 *buf, int Count)
{
 while(Count)
 {
  while(BufferIn == BufferSize) SDL_Delay(1);
  Buffer[BufferWrite] = *buf;
  Count--;
  BufferWrite = (BufferWrite + 1) % BufferSize;
  BufferIn++;
  buf++;
 }
}

int init_window(int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
        return 0;

    window = SDL_CreateWindow("emulator",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width * SCALE, height * SCALE,
        0);
    if (!window)
        return 0;

    renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        return 0;

    fb_texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if (!fb_texture)
        return 0;

    /* Set the audio format */
    wanted.freq = AUDIO_SAMPLE_RATE;
    wanted.format = AUDIO_S16;
    wanted.channels = 1; /* 1 = mono, 2 = stereo */
    wanted.samples = 256;
    wanted.callback = fillaudio;
    wanted.userdata = NULL;

    BufferSize = soundbufsize * AUDIO_SAMPLE_RATE / 1000;

    BufferSize -= wanted.samples * 2;		/* SDL uses at least double-buffering, so
                            multiply by 2. */

    if(BufferSize < wanted.samples) BufferSize = wanted.samples;

    Buffer = malloc(sizeof(int) * BufferSize);
    BufferRead = BufferWrite = BufferIn = 0;

    /* Open the audio device, forcing the desired format */
    if (SDL_OpenAudio(&wanted, NULL) < 0) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        return(-1);
    }

    SDL_PauseAudio(0);
    return 1;
}

static int checkGG(char c)
{
   static const char lets[16] = { 'A', 'P', 'Z', 'L', 'G', 'I', 'T', 'Y', 'E', 'O', 'X', 'U', 'K', 'S', 'V', 'N' };
   int x;

   for (x = 0; x < 16; x++)
      if (lets[x] == toupper(c))
         return 1;
   return 0;
}

static int GGisvalid(const char *code)
{
   size_t len = strlen(code);
   uint32 i;

   if (len != 6 && len != 8)
      return 0;

   for (i = 0; i < len; i++)
      if (!checkGG(code[i]))
         return 0;
   return 1;
}

int main(int argc, char *argv[])
{
    uint8_t *gfx;

    FCEUGI *GameInfo = NULL;
    uint32_t sndquality = 0;
    uint32_t sndvolume = 150;

    audio_buffer_index = 0;
    XBuf = nes_framebuffer;

    init_window(WIDTH, HEIGHT);

    odroid_system_init(APP_ID, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, NULL, NULL, NULL, NULL, NULL);

    FCEUI_Initialize();

#if CHEAT_CODES == 1
    FCEUI_SetGameGenie(1);
#endif
    FCEUI_SetSoundVolume(sndvolume);

    GameInfo = (FCEUGI*)FCEUI_LoadGame("", cart_rom, cart_rom_len,
                                        NULL);
    fceu_joystick = 0;
    FCEUI_Sound(AUDIO_SAMPLE_RATE);
#if 1 // No overclocking
    skip_7bit_overclocking = 1;
    extrascanlines         = 0;
    vblankscanlines        = 0;
    overclock_enabled      = 0;
#endif
#if 0 // 2x-Postrender
    skip_7bit_overclocking = 1;
    extrascanlines         = 266;
    vblankscanlines        = 0;
    overclock_enabled      = 1;
#endif
#if 0 // 2x-VBlank
    skip_7bit_overclocking = 1;
    extrascanlines         = 0;
    vblankscanlines        = 266;
    overclock_enabled      = 1;
#endif

    PowerNES();

#if 0
//    char *ggcode = "AVSANLAP+ASSOGYTS+ASSOGLTS+SZUOEASA+SZSOEAGA+AZSOOEOK+ZASOXEAE+EPSOUAEL+SASOSESX+OZVPEAES+IAVPOEGA+SAVPXESX+EIVPUAIV+IAVPVATZ+IAVOEEAA";
    char *ggcode = "004A:40";
    {
        uint16 a;
        uint8  v;
        int    c;
        int    type = 1;
        char temp[256];
        char *codepart;

        strcpy(temp, ggcode);
        codepart = strtok(temp, "+,;._ ");

        while (codepart)
        {
            size_t codepart_len = strlen(codepart);
            if ((codepart_len == 7) && (codepart[4]==':'))
            {
                /* raw code in xxxx:xx format */
                printf("Cheat code added: '%s' (Raw)\n", codepart);
                codepart[4] = '\0';
                a = strtoul(codepart, NULL, 16);
                v = strtoul(codepart + 5, NULL, 16);
                c = -1;
                /* Zero-page addressing modes don't go through the normal read/write handlers in FCEU, so
                * we must do the old hacky method of RAM cheats. */
                if (a < 0x0100) type = 0;
                FCEUI_AddCheat(NULL, a, v, c, type);
            }
            else if ((codepart_len == 10) && (codepart[4] == '?') && (codepart[7] == ':'))
            {
                /* raw code in xxxx?xx:xx */
                printf("Cheat code added: '%s' (Raw)\n", codepart);
                codepart[4] = '\0';
                codepart[7] = '\0';
                a = strtoul(codepart, NULL, 16);
                v = strtoul(codepart + 8, NULL, 16);
                c = strtoul(codepart + 5, NULL, 16);
                /* Zero-page addressing modes don't go through the normal read/write handlers in FCEU, so
                * we must do the old hacky method of RAM cheats. */
                if (a < 0x0100) type = 0;
                FCEUI_AddCheat(NULL, a, v, c, type);
            }
            else if (GGisvalid(codepart) && FCEUI_DecodeGG(codepart, &a, &v, &c))
            {
                FCEUI_AddCheat(NULL, a, v, c, type);
                printf("Cheat code added: '%s' (GG)\n", codepart);
            }
            else if (FCEUI_DecodePAR(codepart, &a, &v, &c, &type))
            {
                FCEUI_AddCheat(NULL, a, v, c, type);
                printf("Cheat code added: '%s' (PAR)\n", codepart);
            }
            codepart = strtok(NULL,"+,;._ ");
        }
    }
#endif

    FCEUI_SetInput(0, SI_GAMEPAD, &fceu_joystick, 0);

    FCEUI_DisableSpriteLimitation(1);
 
    int i = 0;
    while(run_loop) {
        input_read_gamepad();
        FCEUI_Emulate(&gfx, &sound, &ssize, 0);
        nes_blitscreen(gfx);
        WriteSound(sound,ssize);
    }

    SDL_Quit();

    return 0;
}
