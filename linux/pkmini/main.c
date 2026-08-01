#include <unistd.h>
#include <odroid_system.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <SDL2/SDL.h>

#include <odroid_system.h>

#include "porting.h"
#include "crc32.h"
#include "rg_storage.h"

#include "gw_lcd.h"

// PokeMini headers
#include "PokeMini.h"
#include "MinxIO.h"
#include "PMCommon.h"
#include "Hardware.h"
#include "Joystick.h"
#include "MinxAudio.h"
#include "Video.h"
#include "Video_x3.h"

#define APP_ID 20

#define PKMINI_SCALE 3
#define PKMINI_WIDTH 96
#define PKMINI_HEIGHT 64

#define WIDTH  320
#define HEIGHT 240
#define BPP      2
#define SCALE    2

#define AUDIO_SAMPLE_RATE   (44100)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 72)

#define PKMINI_BUFFER_LENGTH_MIN 612
#define PKMINI_BUFFER_LENGTH_MAX 613
static int16_t pkmini_audio_samples[PKMINI_BUFFER_LENGTH_MAX];

// Sound buffer size
#define SOUNDBUFFER	2048
#define PMSOUNDBUFF	(SOUNDBUFFER*2)

static bool fullFrame = false;
static uint skipFrames = 0;

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *fb_texture;
uint16_t pkmini_data[PKMINI_SCALE * PKMINI_WIDTH * PKMINI_SCALE * PKMINI_HEIGHT];

uint16_t fb_data[WIDTH * HEIGHT * BPP];

SDL_AudioSpec wanted;
void fill_audio(void *udata, Uint8 *stream, int len);

extern const unsigned char ROM_DATA[];
extern unsigned int ROM_DATA_LENGTH;

uint32_t allocated_ram = 0;
uint32_t ahb_allocated_ram = 0;
uint32_t itc_allocated_ram = 0;

bool rg_storage_exists(const char *path)
{
    return access(path, F_OK) == 0;
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

static inline void blit(void) {
    // we want 60 Hz for NTSC
    int wantedTime = 1000 / 60;
    SDL_Delay(wantedTime); // rendering takes basically "0ms"

    uint16_t x_offset = (WIDTH - PKMINI_WIDTH * PKMINI_SCALE) / 2;
    uint16_t y_offset = (HEIGHT - PKMINI_HEIGHT * PKMINI_SCALE) / 2;
    uint16_t *src_buffer = pkmini_data;
    uint16_t *dst_buffer = fb_data + y_offset * WIDTH + x_offset;
    for (int y = 0; y < PKMINI_HEIGHT*PKMINI_SCALE; y++) {
        memcpy(dst_buffer,(void *)src_buffer,PKMINI_WIDTH * PKMINI_SCALE * sizeof(uint16_t));
        src_buffer+=PKMINI_WIDTH * PKMINI_SCALE;
        dst_buffer+=WIDTH;
    }

    SDL_UpdateTexture(fb_texture, NULL, fb_data, WIDTH * BPP);
    SDL_RenderCopy(renderer, fb_texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

void fill_audio(void *udata, Uint8 *stream, int len)
{

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
    wanted.freq = AUDIO_SAMPLE_RATE;
    wanted.format = AUDIO_S16;
    wanted.channels = 1;    /* 1 = mono, 2 = stereo */
    wanted.samples = AUDIO_BUFFER_LENGTH * 2;  /* Good low-latency value for callback */
    wanted.callback = fill_audio;
    wanted.userdata = NULL;

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
    return 0;
}

static bool LoadState(const char *savePathName)
{
    return true;
}

static int PokeMini_LoadMINFileXPLATFORM(size_t size, uint8_t* buffer)
{
	// Check if size is valid
	if ((size <= 0x2100) || (size > 0x200000))
		return 0;
	
	// Free existing color information
	PokeMini_FreeColorInfo();
	
	PM_ROM_Mask = GetMultiple2Mask(size);
	PM_ROM_Size = PM_ROM_Mask + 1;
	PM_ROM = buffer;

	NewMulticart();
	
	return 1;
}

static void ZeroArray(uint16_t array[], int size)
{
	int i;
	for (i = 0; i < size; i++)
		array[i] = 0;
}

static void ReverseArray(uint16_t array[], int size)
{
	int i, j;
	for (i = 0, j = size; i < j; i++, j--)
	{
		uint16_t tmp = array[i];
		array[i] = array[j];
		array[j] = tmp;
	}
}

// Apply screen shake effect
static void SetPixelOffset(void)
{
	if (CommandLine.rumblelvl)
	{
		int row_offset = PokeMini_GenRumbleOffset(PKMINI_WIDTH * PKMINI_SCALE) * PKMINI_SCALE;
		int buffer_size = PKMINI_WIDTH * PKMINI_SCALE * PKMINI_HEIGHT * PKMINI_SCALE;
		
		// Derived from the classic 'rotate array' technique
		// from 'Programming Pearls' by Jon Bentley
		if (row_offset < 0)
		{
			row_offset = buffer_size + row_offset;
			ZeroArray(pkmini_data, buffer_size - row_offset - 1);
			ReverseArray(pkmini_data + buffer_size - row_offset, row_offset - 1);
		}
		else
		{
			ReverseArray(pkmini_data, buffer_size - row_offset - 1);
			ZeroArray(pkmini_data + buffer_size - row_offset, row_offset - 1);
		}

		ReverseArray(pkmini_data, buffer_size - 1);
	}
}

int main(int argc, char *argv[])
{
    uint8_t frame_count = 0;
    uint16_t audio_sample_count;
    TPokeMini_VideoSpec *video_spec = NULL;

    init_window(WIDTH, HEIGHT);

    odroid_system_init(APP_ID, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, NULL, NULL, NULL, NULL, NULL);

    odroid_gamepad_state_t joystick = {0};

    CommandLineInit();

	CommandLine.forcefreebios = 0; // OFF
	CommandLine.eeprom_share = 0;  // OFF (there is no practical benefit to a shared eeprom save
	                               //      - it just gets full and becomes a nuisance...)
	CommandLine.updatertc = 2;	   // Update RTC (0=Off, 1=State, 2=Host)

    strcpy(CommandLine.bios_file, "./bios.min");
    strcpy(CommandLine.eeprom_file, "pokemonmini.eep");


    video_spec = (TPokeMini_VideoSpec *)&PokeMini_Video3x3;

    PokeMini_SetVideo(video_spec, 16, 0, 0);//CommandLine.lcdfilter, CommandLine.lcdmode);

    PokeMini_Create(0/*flags*/, PMSOUNDBUFF);

    PokeMini_VideoPalette_Init(PokeMini_BGR16, 0/* disable high colour*/);

    PokeMini_VideoPalette_Index(CommandLine.palette, NULL, CommandLine.lcdcontrast, CommandLine.lcdbright);

    PokeMini_ApplyChanges(); // Note: 'CommandLine.piezofilter' value is also read inside here

    MinxAudio_ChangeEngine(CommandLine.sound);

    PokeMini_LoadMINFileXPLATFORM(ROM_DATA_LENGTH, (uint8_t*)ROM_DATA);

    MinxIO_FormatEEPROM();

    PokeMini_LoadEEPROMFile(CommandLine.eeprom_file);

    PokeMini_Reset(0);

    while (true)
    {
        odroid_input_read_gamepad(&joystick);
        if (joystick.values[ODROID_INPUT_A] == 1)
            MinxIO_Keypad(MINX_KEY_A, 1);
        else
            MinxIO_Keypad(MINX_KEY_A, 0);
        if (joystick.values[ODROID_INPUT_B] == 1)
            MinxIO_Keypad(MINX_KEY_B, 1);
        else
            MinxIO_Keypad(MINX_KEY_B, 0);
        if (joystick.values[ODROID_INPUT_START] == 1)
            MinxIO_Keypad(MINX_KEY_C, 1);
        else
            MinxIO_Keypad(MINX_KEY_C, 0);
        if (joystick.values[ODROID_INPUT_SELECT] == 1)
            MinxIO_Keypad(MINX_KEY_SHOCK, 1);
        else
            MinxIO_Keypad(MINX_KEY_SHOCK, 0);
        if (joystick.values[ODROID_INPUT_UP] == 1)
            MinxIO_Keypad(MINX_KEY_UP, 1);
        else
            MinxIO_Keypad(MINX_KEY_UP, 0);
        if (joystick.values[ODROID_INPUT_DOWN] == 1)
            MinxIO_Keypad(MINX_KEY_DOWN, 1);
        else
            MinxIO_Keypad(MINX_KEY_DOWN, 0);
        if (joystick.values[ODROID_INPUT_LEFT] == 1)
            MinxIO_Keypad(MINX_KEY_LEFT, 1);
        else
            MinxIO_Keypad(MINX_KEY_LEFT, 0);
        if (joystick.values[ODROID_INPUT_RIGHT] == 1)
            MinxIO_Keypad(MINX_KEY_RIGHT, 1);
        else
            MinxIO_Keypad(MINX_KEY_RIGHT, 0);
        if (joystick.values[ODROID_INPUT_X] == 1) {
            printf("Saving state\n");
            PokeMini_SaveSSStream("save.bin", 0);
        }
        if (joystick.values[ODROID_INPUT_Y] == 1) {
            printf("Loading state\n");
            PokeMini_LoadSSStream("save.bin", 0);
        }
            
        uint startTime = get_elapsed_time();
        bool drawFrame = !skipFrames;

        PokeMini_EmulateFrame();
        audio_sample_count = frame_count%2==0?PKMINI_BUFFER_LENGTH_MIN:PKMINI_BUFFER_LENGTH_MAX;

        MinxAudio_GetSamplesS16Ch(pkmini_audio_samples, audio_sample_count, 1);

        PokeMini_VideoBlit((uint16_t *)pkmini_data, PKMINI_WIDTH*PKMINI_SCALE);
        if (PokeMini_Rumbling)
        {
            SetPixelOffset();
        }

        blit();
        frame_count++;
    }
    PokeMini_SaveEEPROMFile(CommandLine.eeprom_file);

    SDL_Quit();

    return 0;
}
