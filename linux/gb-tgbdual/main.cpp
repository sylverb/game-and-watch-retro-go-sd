#pragma GCC optimize("O0")

extern "C" {

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gw_lcd.h"

#include <SDL2/SDL.h>

#include "odroid_system.h"
#include "porting.h"
#include "crc32.h"

#include "gw_malloc.h"

#include "rom_manager.h"

#include "gw_malloc.h"

// ROM Data
extern const unsigned char ROM_DATA[];
extern unsigned int ROM_DATA_LENGTH;
}

#include <cstdio>
#include <cstddef>
#include <cassert>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <algorithm>
#include <cmath>
#include "gb_core/gb.h"
#include "linux_renderer.h"
#include "heap.hpp"

#define APP_ID 20

#define GB_WIDTH (160)
#define GB_HEIGHT (144)

#define WIDTH  GB_WIDTH
#define HEIGHT GB_HEIGHT
#define BPP      2
#define SCALE    4

#define VIDEO_BUFF_SIZE (256 * GB_HEIGHT * sizeof(uint16_t))
#define VIDEO_PITCH (160)
#define VIDEO_REFRESH_RATE 60

char index_palette = 0;

static uint16_t *video_buf;
gb *g_gb;
linux_renderer *render;


#define AUDIO_SAMPLE_RATE   (48000)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60)
uint16_t audio_buffer[5000];

// Use 60Hz for GB
#define AUDIO_BUFFER_LENGTH_GB (AUDIO_SAMPLE_RATE / 60)
#define AUDIO_BUFFER_LENGTH_DMA_GB ((2 * AUDIO_SAMPLE_RATE) / 60)

static uint skipFrames = 0;

// Resampling
/* There are 35112 stereo sound samples in a video frame */
#define SOUND_SAMPLES_PER_FRAME   35112
/* We request 2064 samples from each call of GB::runFor() */
#define SOUND_SAMPLES_PER_RUN     2064
/* Native GB/GBC hardware audio sample rate (~2 MHz) */
#define SOUND_SAMPLE_RATE_NATIVE  (VIDEO_REFRESH_RATE * (double)SOUND_SAMPLES_PER_FRAME)

#define SOUND_SAMPLE_RATE_CC      (SOUND_SAMPLE_RATE_NATIVE / CC_DECIMATION_RATE) /* 65835Hz */

#define SOUND_BUFF_SIZE         (SOUND_SAMPLES_PER_RUN + 2064)

static int16_t *audio_out_buffer     = NULL;
static size_t audio_out_buffer_size  = 0;
static size_t audio_out_buffer_pos   = 0;
static size_t audio_batch_frames_max = (1 << 16);

extern "C" void heap_itc_alloc(bool itc) {
    printf("heap_itc_alloc %d\n", itc);
}

extern "C" void *heap_alloc_mem(size_t s) {
    printf("heap_alloc_mem %zu\n", s);
    void *ptr = (void *)malloc(s);

    return ptr;
}

// SDL
SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *fb_texture;
uint16_t fb_data[WIDTH * HEIGHT * BPP];

SDL_AudioSpec wanted;
void fill_audio(void *udata, uint8_t *stream, int len);

int16_t global_audio_buffer[800*60]; // 1s of audio
uint16_t global_audio_offset = 0;

void gb_pcm_submit(int16_t *stream, int samples) {
//    printf("gb_pcm_submit %d\n",samples);
}


void blit(uint16_t *buffer) {
    // we want 60 Hz for NTSC
//    int wantedTime = 1000 / 60;
//    SDL_Delay(wantedTime); // rendering takes basically "0ms"

    for (int y = 0; y < GB_WIDTH; y++) {
        memcpy((void *)(&fb_data[y*160]),(void *)buffer,160*4);
        buffer+=VIDEO_PITCH;
    }

    SDL_UpdateTexture(fb_texture, NULL, fb_data, WIDTH * BPP);
    SDL_RenderCopy(renderer, fb_texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

uint32_t allocated_ram = 0;
uint32_t ahb_allocated_ram = 0;
uint32_t itc_allocated_ram = 0;

extern "C" void *itc_calloc(size_t count,size_t size)
{
   itc_allocated_ram+=size*count;
   printf("itc_calloc %zu bytes (new total = %u)\n",size*count,itc_allocated_ram);
   void *ret = calloc(count,size);
   return ret;
}

extern "C" void *itc_malloc(size_t size)
{
   itc_allocated_ram+=size;
   printf("itc_malloc %zu bytes (new total = %u)\n",size,itc_allocated_ram);
   void *ret = malloc(size);
   return ret;
}

#if 0
void * operator new(std::size_t n) throw(std::bad_alloc)
{
   printf("new %d\n",n);
   return malloc(n);
  //...
}
void operator delete(void * p) throw()
{
   printf("delete\n");
}
#endif

int init_window(int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO /*| SDL_INIT_AUDIO*/) != 0)
        return 0;

    window = SDL_CreateWindow("G&W GB TGBDUAL",
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
#if 0
    /* Set the audio format */
    wanted.freq = SOUND_SAMPLE_RATE_CC;// AUDIO_SAMPLE_RATE;
    wanted.format = AUDIO_S16;
    wanted.channels = 2; /* 1 = mono, 2 = stereo */
    wanted.samples = 256;
    wanted.callback = fillaudio;
    wanted.userdata = NULL;

    BufferSize = soundbufsize * AUDIO_SAMPLE_RATE / 1000;

    BufferSize -= wanted.samples * 2;		/* SDL uses at least double-buffering, so
                            multiply by 2. */

    if(BufferSize < wanted.samples) BufferSize = wanted.samples;

    Buffer = (volatile int *)malloc(sizeof(int) * BufferSize);
    BufferRead = BufferWrite = BufferIn = 0;

    /* Open the audio device, forcing the desired format */
    if (SDL_OpenAudio(&wanted, NULL) < 0) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        return(-1);
    }

    SDL_PauseAudio(0);
#endif
    return 0;
}

static bool SaveState(const char *savePathName)
{
    int total_size = g_gb->get_state_size();
    printf("SaveState %d\n",total_size);
    char *data = (char *)malloc(total_size);
    g_gb->save_state_mem(data);
    FILE *pFile = fopen("./save.bin","wb");
    if (pFile ){
        fwrite(data,1,total_size,pFile);
    }
    fclose(pFile);
    free(data);

    return 0;
}

static bool LoadState(const char *savePathName)
{
    FILE* pFile;
    int size = g_gb->get_state_size();
    printf("LoadState size = %d\n",size);
    uint8_t *buffer = (uint8_t*)malloc(size);

    pFile = fopen("./save.bin","r");
    if (pFile ){
        size = fread(buffer,1,size,pFile);
    }
    printf("Savestate rom name = %s\n",&buffer[34]);
    printf("Current rom name = %s\n",g_gb->get_rom()->get_info()->cart_name);
    if (strcmp((const char *)&buffer[34],g_gb->get_rom()->get_info()->cart_name) == 0)
        g_gb->restore_state_mem(buffer);

    return true;
}

static uint32_t joystick_state = 0; /* player input data, 1 byte per player (1-4) */

static bool run_loop = true;

int input_read_gamepad()
{
    SDL_Event event;
    uint8_t input_buf  = joystick_state;

    if (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN) {
            switch (event.key.keysym.sym) {
            case SDLK_x:
                input_buf |= 1<<0;
                break;
            case SDLK_z:
                input_buf |= 1<<1;
                break;
            case SDLK_LSHIFT:
                input_buf |= 1<<3;
                break;
            case SDLK_LCTRL:
                input_buf |= 1<<2;
                break;
            case SDLK_UP:
                input_buf |= 1<<5;
                break;
            case SDLK_DOWN:
                input_buf |= 1<<4;
                break;
            case SDLK_LEFT:
                input_buf |= 1<<6;
                break;
            case SDLK_RIGHT:
                input_buf |= 1<<7;
                break;
            case SDLK_ESCAPE:
                run_loop = false;
                break;
            case SDLK_F3:
                SaveState(NULL);
                break;
            case SDLK_F4:
                LoadState(NULL);
                break;
            case SDLK_p:
                index_palette++;
                if (index_palette == g_gb->get_lcd()->get_palette_count()) index_palette = 0;
                g_gb->get_lcd()->set_palette(index_palette);
                break;
            default:
                break;
            }
        }
        if (event.type == SDL_KEYUP) {
            switch (event.key.keysym.sym) {
            case SDLK_x:
                input_buf &= ~(1<<0);
                break;
            case SDLK_z:
                input_buf &= ~(1<<1);
                break;
            case SDLK_LSHIFT:
                input_buf &= ~(1<<3);
                break;
            case SDLK_LCTRL:
                input_buf &= ~(1<<2);
                break;
            case SDLK_UP:
                input_buf &= ~(1<<5);
                break;
            case SDLK_DOWN:
                input_buf &= ~(1<<4);
                break;
            case SDLK_LEFT:
                input_buf &= ~(1<<6);
                break;
            case SDLK_RIGHT:
                input_buf &= ~(1<<7);
                break;
            case SDLK_ESCAPE:
                run_loop = false;
                break;
            default:
                break;
            }
        }
    }

    joystick_state = input_buf;
    return input_buf;
}

int charToInt(char val) {
//    printf("input : %c\n",val);
    if (val >= '0' && val <= '9') {
//        printf("output : %d\n",val - '0');
        return val - '0';
    } else {
//        printf("output : %d\n",val - 'A' + 10);
        return val - 'A' + 10;
    }
}

void apply_cheat_code(const char *cheatcode) {
    char temp[256];

    strcpy(temp, cheatcode);
    char *codepart = strtok(temp, "+,;._ ");

    while (codepart)
    {
        size_t codepart_len = strlen(codepart);
        if (codepart_len == 8) {
            // gameshark format for "ABCDEFGH",
            // AB    External RAM bank number
            // CD    New Data
            // GHEF  Memory Address (internal or external RAM, A000-DFFF)
            cheat_dat *cheat = (cheat_dat *)itc_calloc(1,sizeof(cheat_dat));
            cheat->code = ((charToInt(*codepart))<<4) + charToInt(*(codepart+1));
            cheat->dat = (charToInt(*(codepart+2))<<4) + charToInt(*(codepart+3));
            cheat->adr = (charToInt(*(codepart+6))<<12) + (charToInt(*(codepart+7))<<8) +
                      (charToInt(*(codepart+4))<<4) + charToInt(*(codepart+5));
            g_gb->get_cheat()->add_cheat(cheat);
            printf("8 char string %s code %x dat %x adr %x\n",codepart,cheat->code,cheat->dat,cheat->adr);
        } else if (codepart_len == 9) {
            // game genie format: for "ABCDEFGHI",
            // AB   = New data
            // FCDE = Memory address, XORed by 0F000h
            // GIH  = Check data (can be ignored for our purposes)
            cheat_dat *cheat = (cheat_dat *)itc_calloc(1,sizeof(cheat_dat));
            word scramble;
            sscanf(codepart, "%2hhx%4hx", &cheat->dat, &scramble);
            cheat->code = 1;
            cheat->adr = (((scramble&0xF) << 12) ^ 0xF000) | (scramble >> 4);
            g_gb->get_cheat()->add_cheat(cheat);
            printf("9 char string %s code %x dat %x adr %x\n",codepart,cheat->code,cheat->dat,cheat->adr);
        }
        codepart = strtok(NULL,"+,;._ ");
    }
}

int main()
{
    int err = 0;
    uint64_t samples_count = 0;
    uint64_t frames_count = 0;

    unsigned flags = 0;
    printf("tgbdual-go\n");
	// sets framebuffer1 as active buffer
    odroid_system_init(APP_ID, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, NULL, NULL, NULL, NULL, NULL);

    bool headless = getenv("TAMA5_HEADLESS") != NULL;
    int headless_frames = headless ? 1200 : 0; /* ~20s at 60Hz */
    if (!headless)
        init_window(WIDTH, HEIGHT);
    else
        printf("headless mode (%d frames)\n", headless_frames);

//    odroid_gamepad_state_t joystick = {0};

    video_buf = (uint16_t *)malloc(VIDEO_BUFF_SIZE);

    render = new linux_renderer(0);
	printf("!!!!!!!!!!!! new gb\n");
    g_gb   = new gb(render, true, true);

    printf("load rom\n");
   if (!g_gb->load_rom((byte *)ROM_DATA, ROM_DATA_LENGTH, NULL, 0, true))
      return -1;

    printf("load rom done\n");

    if (g_gb->get_rom()->get_info()->gb_type == 1)
        g_gb->get_lcd()->set_palette(0);

/*************/
//apply_cheat_code("09C56BE6E+09C74AE6E");
//apply_cheat_code("346708FEC");
//apply_cheat_code("01095DDB+01995EDB");
//apply_cheat_code("00570EE69");
/*
    cheat_dat *tmp=new cheat_dat;

    tmp->enable = true;
    tmp->next = NULL;

    word scramble;
    sscanf("346708FEC", "%2hhx%4hx", &tmp->dat, &scramble);
    tmp->code = 1; // TODO: test if this is correct for ROM patching
    tmp->adr = (((scramble&0xF) << 12) ^ 0xF000) | (scramble >> 4);
    printf("Cheat code = 0x%x dat = 0x%x addr= 0x%x\n",tmp->code,tmp->dat,tmp->adr);

    g_gb->get_cheat()->add_cheat(tmp);

    sscanf("00570EE69", "%2hhx%4hx", &tmp->dat, &scramble);
    tmp->code = 1; // TODO: test if this is correct for ROM patching
    tmp->adr = (((scramble&0xF) << 12) ^ 0xF000) | (scramble >> 4);
    printf("Cheat code = 0x%x dat = 0x%x addr= 0x%x\n",tmp->code,tmp->dat,tmp->adr);

    g_gb->get_cheat()->add_cheat(tmp);*/
/*
    byte adrlo, adrhi;
    sscanf("01995EDB", "%2hhx%2hhx%2hhx%2hhx", &tmp->code, &tmp->dat, &adrlo, &adrhi);
    tmp->adr = (adrhi<<8) | adrlo;
    printf("Cheat code = 0x%x dat = 0x%x addr= 0x%x\n",tmp->code,tmp->dat,tmp->adr);
    g_gb->get_cheat()->add_cheat(tmp);

    tmp = new cheat_dat;

    tmp->enable = true;
    tmp->next = NULL;
    sscanf("01095DDB", "%2hhx%2hhx%2hhx%2hhx", &tmp->code, &tmp->dat, &adrlo, &adrhi);
    tmp->adr = (adrhi<<8) | adrlo;

    g_gb->get_cheat()->add_cheat(tmp);*/
/*************/

    int count = 0;
    uint32_t output_len;
    while (run_loop)
    {
//   printf("run loop\n");
//        odroid_input_read_gamepad(&joystick);
        uint startTime = get_elapsed_time();
        bool drawFrame = !skipFrames;

        if (!headless)
            input_read_gamepad();

        for (int line = 0;line < 154; line++) {
                g_gb->run();
        }
        if (headless) {
            if (--headless_frames <= 0)
                break;
        } else {
//        blit(video_buf);
        }
        // Tick before submitting audio/syncing
//        odroid_system_tick(!drawFrame, fullFrame, get_elapsed_time_since(startTime));
    }

    if (!headless)
        SDL_Quit();

    return 0;
}
