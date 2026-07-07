// Atari Lynx (Handy) — SDL host build.
// Same core sources as the device overlay (external/handy-go).

extern "C"
{
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <SDL.h>

#include "porting.h"
#include "rom_manager.h"
}

#include <handy.h>

extern "C" {
extern int linux_savestate_req;
extern int linux_loadstate_req;
}

#define LYNX_FPS                60
#define AUDIO_LYNX_SAMPLE_RATE  HANDY_AUDIO_SAMPLE_FREQ
#define GW_LCD_WIDTH            320
#define GW_LCD_HEIGHT           240
#define DISPLAY_SCALE           2

static CSystem *lynx = NULL;
static uint16_t lynx_framebuffer[HANDY_SCREEN_WIDTH * HANDY_SCREEN_HEIGHT];
static SWORD    lynx_audio_buffer[HANDY_AUDIO_BUFFER_LENGTH];
static uint16_t fb_data[GW_LCD_WIDTH * GW_LCD_HEIGHT];

static SDL_Window        *window;
static SDL_Renderer      *renderer;
static SDL_Texture       *fb_texture;
static SDL_AudioDeviceID  audio_device;

static bool run_loop = true;
static ULONG buttons = 0;

static unsigned char *rom_copy = NULL;
static char lynx_linux_rom_path[512];

#define AUDIO_RING_FRAMES 4096
static int16_t audio_ring[AUDIO_RING_FRAMES * 2];
static int audio_write = 0;
static int audio_read = 0;
static int audio_available = 0;

static void audio_push(const int16_t *samples, int stereo_frames)
{
    for (int i = 0; i < stereo_frames; i++)
    {
        audio_ring[audio_write * 2]     = samples[i * 2];
        audio_ring[audio_write * 2 + 1] = samples[i * 2 + 1];
        audio_write = (audio_write + 1) % AUDIO_RING_FRAMES;
        if (audio_available < AUDIO_RING_FRAMES)
            audio_available++;
        else
            audio_read = (audio_read + 1) % AUDIO_RING_FRAMES;
    }
}

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    int16_t *out = (int16_t *)stream;
    int samples = len / (int)sizeof(int16_t);

    for (int i = 0; i < samples; i += 2)
    {
        if (audio_available > 0)
        {
            out[i]     = audio_ring[audio_read * 2];
            out[i + 1] = audio_ring[audio_read * 2 + 1];
            audio_read = (audio_read + 1) % AUDIO_RING_FRAMES;
            audio_available--;
        }
        else
        {
            out[i] = out[i + 1] = 0;
        }
    }
}

static unsigned char *load_rom_file(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz <= 0)
    {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf)
    {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz)
    {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_len = (size_t)sz;
    return buf;
}

static int init_sdl(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    window = SDL_CreateWindow("G&W Atari Lynx (Handy)",
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              GW_LCD_WIDTH * DISPLAY_SCALE, GW_LCD_HEIGHT * DISPLAY_SCALE,
                              0);
    if (!window)
        return -1;

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        return -1;

    fb_texture = SDL_CreateTexture(renderer,
                                   SDL_PIXELFORMAT_RGB565,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   GW_LCD_WIDTH, GW_LCD_HEIGHT);
    if (!fb_texture)
        return -1;

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUDIO_LYNX_SAMPLE_RATE;
    want.format = AUDIO_S16;
    want.channels = 2;
    want.samples = 512;
    want.callback = audio_callback;

    audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_device == 0)
        return -1;

    memset(fb_data, 0, sizeof(fb_data));
    return 0;
}

static void blit_to_framebuffer(void)
{
    const uint16_t *src = lynx_framebuffer;
    const int y_offset = (GW_LCD_HEIGHT - HANDY_SCREEN_HEIGHT * 2) / 2;

    for (int sy = 0; sy < HANDY_SCREEN_HEIGHT; sy++)
    {
        uint16_t *row0 = fb_data + (y_offset + sy * 2) * GW_LCD_WIDTH;
        uint16_t *row1 = row0 + GW_LCD_WIDTH;
        const uint16_t *in = src + sy * HANDY_SCREEN_WIDTH;
        for (int sx = 0; sx < HANDY_SCREEN_WIDTH; sx++)
        {
            uint16_t c = in[sx];
            row0[sx * 2]     = c;
            row0[sx * 2 + 1] = c;
            row1[sx * 2]     = c;
            row1[sx * 2 + 1] = c;
        }
    }

    SDL_UpdateTexture(fb_texture, NULL, fb_data, GW_LCD_WIDTH * 2);
    SDL_RenderCopy(renderer, fb_texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

static void lynx_state_path(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s.state", lynx_linux_rom_path);
}

static void lynx_linux_save_state(void)
{
    if (!lynx)
        return;

    char path[1024];
    lynx_state_path(path, sizeof(path));
    FILE *fp = fopen(path, "wb");
    if (!fp)
    {
        fprintf(stderr, "LYNX: cannot open '%s' for save\n", path);
        return;
    }
    bool ok = lynx->ContextSave(fp);
    fclose(fp);
    fprintf(stderr, "LYNX: state %s to '%s'\n", ok ? "saved" : "save FAILED", path);
}

static void lynx_linux_load_state(void)
{
    if (!lynx)
        return;

    char path[1024];
    lynx_state_path(path, sizeof(path));
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        fprintf(stderr, "LYNX: cannot open '%s' for load\n", path);
        return;
    }
    bool ok = lynx->ContextLoad(fp);
    fclose(fp);
    if (!ok)
        lynx->Reset();
    fprintf(stderr, "LYNX: state %s from '%s'\n", ok ? "loaded" : "load FAILED", path);
}

static void map_sdl_key(SDL_Keycode key, bool down)
{
    ULONG mask = 0;
    switch (key)
    {
    case SDLK_UP:    mask = BUTTON_UP; break;
    case SDLK_DOWN:  mask = BUTTON_DOWN; break;
    case SDLK_LEFT:  mask = BUTTON_LEFT; break;
    case SDLK_RIGHT: mask = BUTTON_RIGHT; break;
    case SDLK_z:     mask = BUTTON_A; break;
    case SDLK_x:     mask = BUTTON_B; break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        mask = BUTTON_PAUSE; break;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        mask = BUTTON_OPT1; break;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        mask = BUTTON_OPT2; break;
    default:
        return;
    }

    if (down)
        buttons |= mask;
    else
        buttons &= ~mask;
}

static void poll_input(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
            run_loop = false;
        else if (event.type == SDL_KEYDOWN)
        {
            if (event.key.keysym.sym == SDLK_ESCAPE)
                run_loop = false;
            else if (event.key.keysym.sym == SDLK_F2)
                linux_savestate_req = 1;
            else if (event.key.keysym.sym == SDLK_F4)
                linux_loadstate_req = 1;
            else
                map_sdl_key(event.key.keysym.sym, true);
        }
        else if (event.type == SDL_KEYUP)
            map_sdl_key(event.key.keysym.sym, false);
    }
}

static void store_audio(void)
{
    int gen = (int)(gAudioBufferPointer / 2);
    if (gen > 0)
        audio_push((const int16_t *)lynx_audio_buffer, gen);
    gAudioBufferPointer = 0;
}

int main(int argc, char **argv)
{
    size_t rom_len = 0;
    unsigned char *rom_ptr = NULL;

    if (argc > 1)
    {
        rom_ptr = load_rom_file(argv[1], &rom_len);
        if (!rom_ptr)
        {
            fprintf(stderr, "Failed to load ROM: %s\n", argv[1]);
            return 1;
        }
        rom_copy = rom_ptr;
        snprintf(lynx_linux_rom_path, sizeof(lynx_linux_rom_path), "%s", argv[1]);
        printf("Loaded ROM %s (%zu bytes)\n", argv[1], rom_len);
    }
    else
    {
        rom_ptr = (unsigned char *)ROM_DATA;
        rom_len = ROM_DATA_LENGTH;
        snprintf(lynx_linux_rom_path, sizeof(lynx_linux_rom_path), "./%s",
                 lynx_embedded_rom_source);
        printf("Using embedded ROM %s (%u bytes, .%s)\n",
               lynx_embedded_rom_source, ROM_DATA_LENGTH, ROM_EXT);
    }

    if (init_sdl() != 0)
    {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        free(rom_copy);
        return 1;
    }

    lynx = new CSystem((const UBYTE *)rom_ptr, (ULONG)rom_len,
                       MIKIE_PIXEL_FORMAT_16BPP_565, AUDIO_LYNX_SAMPLE_RATE);
    if (lynx == NULL || lynx->mFileType == HANDY_FILETYPE_ILLEGAL)
    {
        fprintf(stderr, "Lynx: ROM loading failed (mFileType=%lu)\n",
                lynx ? (unsigned long)lynx->mFileType : 0UL);
        delete lynx;
        free(rom_copy);
        SDL_Quit();
        return 1;
    }

    gPrimaryFrameBuffer = (UBYTE *)lynx_framebuffer;
    gAudioBuffer = lynx_audio_buffer;
    gAudioEnabled = 1;

    printf("Cart: %s (%s)\n", lynx->mCart->CartGetName(),
           lynx->mCart->CartGetManufacturer());

    SDL_PauseAudioDevice(audio_device, 0);

    {
        const char *al = getenv("GWAUTOLOAD");
        if (al && al[0] && al[0] != '0')
            linux_loadstate_req = 1;
    }

    Uint64 perf_freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();

    while (run_loop)
    {
        poll_input();

        if (linux_savestate_req)
        {
            linux_savestate_req = 0;
            lynx_linux_save_state();
        }
        if (linux_loadstate_req)
        {
            linux_loadstate_req = 0;
            lynx_linux_load_state();
        }

        lynx->SetButtonData(buttons);
        lynx->UpdateFrame(true);
        blit_to_framebuffer();

        /* Lynx frame length varies — pace from audio like retro-go main_lynx.cpp */
        int stereo_frames = (int)(gAudioBufferPointer / 2);
        store_audio();

        float target_ms = (stereo_frames > 0)
            ? (1000.0f * (float)stereo_frames / (float)AUDIO_LYNX_SAMPLE_RATE)
            : (1000.0f / LYNX_FPS);

        Uint64 now = SDL_GetPerformanceCounter();
        float elapsed_ms = (float)(now - last) * 1000.0f / (float)perf_freq;
        if (elapsed_ms < target_ms)
            SDL_Delay((Uint32)(target_ms - elapsed_ms));
        last = SDL_GetPerformanceCounter();
    }

    delete lynx;
    lynx = NULL;
    free(rom_copy);
    rom_copy = NULL;
    SDL_CloseAudioDevice(audio_device);
    SDL_DestroyTexture(fb_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
