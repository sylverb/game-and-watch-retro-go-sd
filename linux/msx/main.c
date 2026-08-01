#include <odroid_system.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <SDL.h>

#include "porting.h"
#include "crc32.h"
#include "MSX.h"

#include "gw_lcd.h"
#include "Properties.h"
#include "ArchFile.h"
#include "VideoRender.h"
#include "AudioMixer.h"
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
#include "ArchEvent.h"
#include "ArchSound.h"
#include "ArchNotifications.h"
#include "JoystickPort.h"
#include "InputEvent.h"
#include "R800.h"
#include "SlotManager.h"
#include "Src/Utils/SaveState.h"
#include "save_msx.h"
#include "main.h"
#include "VDP_MSX.h"

extern const unsigned char ROM_DATA[];
extern unsigned int ROM_DATA_LENGTH;
extern unsigned int cart_rom_len;
extern const char *ROM_NAME;
extern const char *ROM_EXT;
extern unsigned int ROM_MAPPER;

#define AUDIO_MSX_SAMPLE_RATE 16000
#define FPS_NTSC  60
#define FPS_PAL   50
static int8_t msx_fps = FPS_PAL;

void retro_run(void);

static Properties* properties;
static Video* video;
static Mixer* mixer;

static Pixel* image_buffer;
static Pixel16* image_buffer_16;
static unsigned image_buffer_base_width;
static unsigned image_buffer_current_width;
static unsigned image_buffer_height;
static unsigned width = 272;
static unsigned height = 240;
static int double_width;


bool is_coleco, is_sega, is_spectra, is_auto, auto_rewind_cas;
int msx2_dif = 0;

unsigned disk_index = 0;
unsigned disk_images = 0;
char disk_paths[10][PATH_MAX];
bool disk_inserted = false;

//////
#define MSX_DISK_EXTENSION "dsk"
#define MSX_DISK_EXTENSION_COMPRESSED "cdk"


extern BoardInfo boardInfo;
static Properties* properties;
static Machine *msxMachine;
static Mixer* mixer;

enum{
   MSX_GAME_ROM = 0,
   MSX_GAME_DISK,
   MSX_GAME_HDIDE
};

static int msx_game_type = MSX_GAME_ROM;
static int8_t currentVolume = -1;


unsigned char *rom_decompress_buffer[1024*1024];

static uint16_t palette565[256];

///////////
UInt32 allocated_ram = 0;
UInt32 ahb_allocated_ram = 0;
UInt32 itc_allocated_ram = 0;

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

void *ram_malloc(size_t size)
{
   itc_allocated_ram+=size;
//   printf("itc_malloc %zu bytes (new total = %u)\n",size,itc_allocated_ram);
   void *ret = malloc(size);
   return ret;
}

void *ahb_only_malloc(size_t size)
{
   return malloc(size);
}

void msxLedSetFdd1(int state)
{
   (void)state;
}

#define APP_ID  11

typedef struct SdlSound {
    Mixer* mixer;
    int started;
    UInt32 readPtr;
    UInt32 writePtr;
    UInt32 bytesPerSample;
    UInt32 bufferMask;
    UInt32 bufferSize;
    UInt32 skipCount;
    UInt8* buffer;
} SdlSound;

SdlSound sdlSound;
int oldLen = 0;

static unsigned char msx_joystick_state = 0;
static odroid_gamepad_state_t previous_joystick_state;

Pixel* frameBufferGetLine(FrameBuffer* frameBuffer, int y)
{
   return (image_buffer + y * image_buffer_current_width);
}

Pixel16* frameBufferGetLine16(FrameBuffer* frameBuffer, int y)
{
   return (image_buffer_16 + y * image_buffer_current_width);
}

FrameBuffer* frameBufferGetDrawFrame(void)
{
   return (void*)image_buffer;
}

FrameBuffer* frameBufferFlipDrawFrame(void)
{
   return (void*)image_buffer;
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
   return (void*)image_buffer;
}

FrameBufferData* frameBufferGetActive()
{
   return (void*)image_buffer;
}

void   frameBufferSetLineCount(FrameBuffer* frameBuffer, int val)
{
   image_buffer_height = val;
}

int    frameBufferGetLineCount(FrameBuffer* frameBuffer) {
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
   image_buffer_current_width = double_width ? image_buffer_base_width * 2 : image_buffer_base_width;
}

static int loop = 1;
char saveBuffer[1024*1024];

void keyboardUpdate() 
{
    SDL_Event event;
    static SDL_Event last_down_event;

    if (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN) {
//            printf("Press %d\n", event.key.keysym.sym);
            switch (event.key.keysym.sym) {
            case SDLK_SPACE:
                eventMap[EC_SPACE]  = 1;
                break;
            case SDLK_UNDERSCORE:
                eventMap[EC_UNDSCRE]  = 1;
                break;
            case SDLK_SLASH:
                eventMap[EC_DIV]  = 1;
                break;
            case SDLK_a:
//                eventMap[EC_UNDSCRE]  = 1;
                eventMap[EC_A]  = 1;
                break;
            case SDLK_b:
//                eventMap[EC_DIV]  = 1;
                eventMap[EC_B]  = 1;
                break;
            case SDLK_c:
                eventMap[EC_C]  = 1;
                break;
            case SDLK_d:
                eventMap[EC_D]  = 1;
                break;
            case SDLK_e:
                eventMap[EC_E]  = 1;
                break;
            case SDLK_f:
                eventMap[EC_F]  = 1;
                break;
            case SDLK_g:
                eventMap[EC_G]  = 1;
                break;
            case SDLK_h:
                eventMap[EC_H]  = 1;
                break;
            case SDLK_i:
                eventMap[EC_I]  = 1;
                break;
            case SDLK_j:
                eventMap[EC_J]  = 1;
                break;
            case SDLK_k:
                eventMap[EC_K]  = 1;
                break;
            case SDLK_l:
                eventMap[EC_L]  = 1;
                break;
            case SDLK_m:
                eventMap[EC_M]  = 1;
                break;
            case SDLK_n:
                eventMap[EC_N]  = 1;
                break;
            case SDLK_o:
                eventMap[EC_O]  = 1;
                break;
            case SDLK_p:
                eventMap[EC_P]  = 1;
                break;
            case SDLK_q:
                eventMap[EC_Q]  = 1;
                break;
            case SDLK_r:
                eventMap[EC_R]  = 1;
                break;
            case SDLK_s:
                eventMap[EC_S]  = 1;
                break;
            case SDLK_t:
                eventMap[EC_T]  = 1;
                break;
            case SDLK_u:
                eventMap[EC_U]  = 1;
                break;
            case SDLK_v:
                eventMap[EC_V]  = 1;
                break;
            case SDLK_w:
                eventMap[EC_W]  = 1;
                break;
            case SDLK_x:
                eventMap[EC_X]  = 1;
                break;
            case SDLK_y:
                eventMap[EC_Y]  = 1;
                break;
            case SDLK_z:
                eventMap[EC_Z]  = 1;
                break;
            case SDLK_0:
                eventMap[EC_0]  = 1;
                break;
            case SDLK_1:
                eventMap[EC_1]  = 1;
                break;
            case SDLK_2:
                eventMap[EC_2]  = 1;
                break;
            case SDLK_3:
                eventMap[EC_3]  = 1;
                break;
            case SDLK_4:
                eventMap[EC_4]  = 1;
                break;
            case SDLK_5:
                eventMap[EC_5]  = 1;
                break;
            case SDLK_6:
                eventMap[EC_6]  = 1;
                break;
            case SDLK_7:
                eventMap[EC_7]  = 1;
                break;
            case SDLK_8:
                eventMap[EC_8]  = 1;
                break;
            case SDLK_9:
                eventMap[EC_9]  = 1;
                break;
            case SDLK_COLON:
                eventMap[EC_COLON]  = 1;
                break;
            case SDLK_COMMA:
                eventMap[EC_COMMA]  = 1;
                break;
            case SDLK_PERIOD:
                eventMap[EC_PERIOD]  = 1;
                break;
            case SDLK_LSHIFT:
                eventMap[EC_LSHIFT]  = 1;
                break;
            case SDLK_RSHIFT:
                eventMap[EC_RSHIFT]  = 1;
                break;
            case SDLK_RETURN:
                eventMap[EC_RETURN]  = 1;
                break;
            case SDLK_LCTRL:
                eventMap[EC_CTRL]  = 1;
                break;
            case SDLK_UP:
                eventMap[EC_UP]  = 1;
                break;
            case SDLK_DOWN:
                eventMap[EC_DOWN]  = 1;
                break;
            case SDLK_LEFT:
                eventMap[EC_LEFT]  = 1;
                break;
            case SDLK_RIGHT:
                eventMap[EC_RIGHT]  = 1;
                break;
            case SDLK_BACKSPACE:
                eventMap[EC_BKSPACE]  = 1;
                break;
            default:
                break;
            }

            last_down_event = event;
        } else if (event.type == SDL_KEYUP) {
//            printf("Release %d\n", event.key.keysym.sym);
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
                loop = 0;
                exit(1);
                break;
            case SDLK_SPACE:
                eventMap[EC_SPACE]  = 0;
                break;
            case SDLK_UNDERSCORE:
                eventMap[EC_UNDSCRE]  = 0;
                break;
            case SDLK_SLASH:
                eventMap[EC_DIV]  = 0;
                break;
            case SDLK_a:
//                eventMap[EC_UNDSCRE]  = 0;
                eventMap[EC_A]  = 0;
                break;
            case SDLK_b:
//                eventMap[EC_DIV]  = 0;
                eventMap[EC_B]  = 0;
                break;
            case SDLK_c:
                eventMap[EC_C]  = 0;
                break;
            case SDLK_d:
                eventMap[EC_D]  = 0;
                break;
            case SDLK_e:
                eventMap[EC_E]  = 0;
                break;
            case SDLK_f:
                eventMap[EC_F]  = 0;
                break;
            case SDLK_g:
                eventMap[EC_G]  = 0;
                break;
            case SDLK_h:
                eventMap[EC_H]  = 0;
                break;
            case SDLK_i:
                eventMap[EC_I]  = 0;
                break;
            case SDLK_j:
                eventMap[EC_J]  = 0;
                break;
            case SDLK_k:
                eventMap[EC_K]  = 0;
                break;
            case SDLK_l:
                eventMap[EC_L]  = 0;
                break;
            case SDLK_m:
                eventMap[EC_M]  = 0;
                break;
            case SDLK_n:
                eventMap[EC_N]  = 0;
                break;
            case SDLK_o:
                eventMap[EC_O]  = 0;
                break;
            case SDLK_p:
                eventMap[EC_P]  = 0;
                break;
            case SDLK_q:
                eventMap[EC_Q]  = 0;
                break;
            case SDLK_r:
                eventMap[EC_R]  = 0;
                break;
            case SDLK_s:
                eventMap[EC_S]  = 0;
                break;
            case SDLK_t:
                eventMap[EC_T]  = 0;
                break;
            case SDLK_u:
                eventMap[EC_U]  = 0;
                break;
            case SDLK_v:
                eventMap[EC_V]  = 0;
                break;
            case SDLK_w:
                eventMap[EC_W]  = 0;
                break;
            case SDLK_x:
                eventMap[EC_X]  = 0;
                break;
            case SDLK_y:
                eventMap[EC_Y]  = 0;
                break;
            case SDLK_z:
                eventMap[EC_Z]  = 0;
                break;
            case SDLK_0:
                eventMap[EC_0]  = 0;
                break;
            case SDLK_1:
                eventMap[EC_1]  = 0;
                break;
            case SDLK_2:
                eventMap[EC_2]  = 0;
                break;
            case SDLK_3:
                eventMap[EC_3]  = 0;
                break;
            case SDLK_4:
                eventMap[EC_4]  = 0;
                break;
            case SDLK_5:
                eventMap[EC_5]  = 0;
                break;
            case SDLK_6:
                eventMap[EC_6]  = 0;
                break;
            case SDLK_7:
                eventMap[EC_7]  = 0;
                break;
            case SDLK_8:
                eventMap[EC_8]  = 0;
                break;
            case SDLK_9:
                eventMap[EC_9]  = 0;
                break;
            case SDLK_COLON:
                eventMap[EC_COLON]  = 0;
                break;
            case SDLK_COMMA:
                eventMap[EC_COMMA]  = 0;
                break;
            case SDLK_PERIOD:
                eventMap[EC_PERIOD]  = 0;
                break;
            case SDLK_LSHIFT:
                eventMap[EC_LSHIFT]  = 0;
                break;
            case SDLK_RSHIFT:
                eventMap[EC_RSHIFT]  = 0;
                break;
            case SDLK_RETURN:
                eventMap[EC_RETURN]  = 0;
                break;
            case SDLK_LCTRL:
                eventMap[EC_CTRL]  = 0;
                break;
            case SDLK_UP:
                eventMap[EC_UP]  = 0;
                break;
            case SDLK_DOWN:
                eventMap[EC_DOWN]  = 0;
                break;
            case SDLK_LEFT:
                eventMap[EC_LEFT]  = 0;
                break;
            case SDLK_RIGHT:
                eventMap[EC_RIGHT]  = 0;
                break;
            case SDLK_BACKSPACE:
                eventMap[EC_BKSPACE]  = 0;
                break;
            case SDLK_F1:
                if (last_down_event.key.keysym.sym == SDLK_F1) {
                    FILE* pFile = fopen("saveMsx.bin","wb");
                    int size = 0;
                    printf("save\n");
                    size = saveMsxState((Uint8 *)saveBuffer,1024*1024);
                    fwrite(saveBuffer, size, 1, pFile);
                    fclose(pFile);
                }
                break;
            case SDLK_F4:
                if (last_down_event.key.keysym.sym == SDLK_F4) {
                    FILE* pFile = fopen("saveMsx.bin","rb");
                    if (pFile) {
                        fread(saveBuffer, 1024*1024, 1, pFile);
                        loadMsxState((Uint8 *)saveBuffer,1024*1024);
                    }
                }
                break;
            default:
                break;
            }
        }
     }
} 

uint8_t state_save_buffer[192 * 1024];

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *fb_texture;

int offset_audio=0;

static bool msx_system_LoadState(char *pathName)
{
      printf("Loading state not implemented...\n");
      return true;
}

static bool msx_system_SaveState(char *pathName)
{
      printf("Saving state not implemented...\n");
      return true;
}

int init_window(int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) != 0)
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

    SDL_PauseAudio(0);

    return 0;
}

/** Joystick() ***********************************************/
/** Query positions of two joystick connected to ports 0/1. **/
/** Returns 0.0.B2.A2.R2.L2.D2.U2.0.0.B1.A1.R1.L1.D1.U1.    **/
/*************************************************************/
unsigned int Joystick(void)
{
    return(msx_joystick_state);
}

/** Keyboard() ***********************************************/
/** Modify keyboard matrix.                                 **/
/*************************************************************/
void Keyboard(void)
{
  /* Everything is done in Joystick() */
}
//The frames per second cap timer
int capTimer;

SDL_mutex *sndlock;


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

static void setPropertiesMsx(Machine *machine, int msxType) {
    int i = 0;

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
            strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/MSX.rom");
            i++;

            if (msx_game_type == MSX_GAME_DISK) {
                machine->slotInfo[i].slot = 3;
                machine->slotInfo[i].subslot = 1;
                machine->slotInfo[i].startPage = 2;
                machine->slotInfo[i].pageCount = 4;
                machine->slotInfo[i].romType = ROM_TC8566AF;
                strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/PANASONICDISK.rom");
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
            machine->slotInfo[i].pageCount = 16; // 128kB of RAM
            machine->slotInfo[i].romType = RAM_MAPPER;
            strcpy(machine->slotInfo[i].name, "");
            i++;

            machine->slotInfo[i].slot = 0;
            machine->slotInfo[i].subslot = 0;
            machine->slotInfo[i].startPage = 0;
            machine->slotInfo[i].pageCount = 4;
            machine->slotInfo[i].romType = ROM_CASPATCH;
            strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/MSX2.rom");
            i++;

            machine->slotInfo[i].slot = 3;
            machine->slotInfo[i].subslot = 1;
            machine->slotInfo[i].startPage = 0;
            machine->slotInfo[i].pageCount = 2;
            machine->slotInfo[i].romType = ROM_NORMAL;
            strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/MSX2EXT.rom");
            i++;

            if (msx_game_type == MSX_GAME_DISK) {
                /* Floppy BIOS at 3-1-2 (MSX2 layout). */
                machine->slotInfo[i].slot = 3;
                machine->slotInfo[i].subslot = 1;
                machine->slotInfo[i].startPage = 2;
                machine->slotInfo[i].pageCount = 4;
                machine->slotInfo[i].romType = ROM_TC8566AF;
                strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/PANASONICDISK.rom");
                i++;
            }
            if (msx_game_type == MSX_GAME_HDIDE) {
                /* Sunrise IDE in machine slots (not cart): hdId 0 → HDD drive 1. */
                machine->slotInfo[i].slot = 1;
                machine->slotInfo[i].subslot = 0;
                machine->slotInfo[i].startPage = 0;
                machine->slotInfo[i].pageCount = 8;
                machine->slotInfo[i].romType = ROM_SUNRISEIDE;
                strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/Nextor.ROM");
                i++;
            }

            machine->slotInfo[i].slot = 3;
            machine->slotInfo[i].subslot = 0;
            machine->slotInfo[i].startPage = 2;
            machine->slotInfo[i].pageCount = 2;
            machine->slotInfo[i].romType = ROM_MSXMUSIC; // FMPAC
            strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/MSX2PMUS.rom");
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
            strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/MSX2P.rom");
            i++;

            machine->slotInfo[i].slot = 0;
            machine->slotInfo[i].subslot = 2;
            machine->slotInfo[i].startPage = 2;
            machine->slotInfo[i].pageCount = 2;
            machine->slotInfo[i].romType = ROM_MSXMUSIC; // FMPAC
            strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/MSX2PMUS.rom");
            i++;

            machine->slotInfo[i].slot = 3;
            machine->slotInfo[i].subslot = 0;
            machine->slotInfo[i].startPage = 0;
            machine->slotInfo[i].pageCount = 32; // 256kB of RAM
            machine->slotInfo[i].romType = RAM_MAPPER;
            strcpy(machine->slotInfo[i].name, "");
            i++;

            machine->slotInfo[i].slot = 3;
            machine->slotInfo[i].subslot = 1;
            machine->slotInfo[i].startPage = 0;
            machine->slotInfo[i].pageCount = 2;
            machine->slotInfo[i].romType = ROM_NORMAL;
            strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/MSX2PEXT.rom");
            i++;

            machine->slotInfo[i].slot = 3;
            machine->slotInfo[i].subslot = 1;
            machine->slotInfo[i].startPage = 2;
            machine->slotInfo[i].pageCount = 4;
            machine->slotInfo[i].romType = ROM_0x4000;
            strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/MSXKANJI.rom");
            i++;

            if (msx_game_type == MSX_GAME_DISK) {
                /* Floppy BIOS (required for .dsk boot). */
                machine->slotInfo[i].slot = 3;
                machine->slotInfo[i].subslot = 2;
                machine->slotInfo[i].startPage = 2;
                machine->slotInfo[i].pageCount = 4;
                machine->slotInfo[i].romType = ROM_TC8566AF;
                strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/PANASONICDISK.rom");
                i++;
            } else if (msx_game_type == MSX_GAME_HDIDE) {
                machine->slotInfo[i].slot = 1;
                machine->slotInfo[i].subslot = 0;
                machine->slotInfo[i].startPage = 0;
                machine->slotInfo[i].pageCount = 8;
                machine->slotInfo[i].romType = ROM_SUNRISEIDE;
                strcpy(machine->slotInfo[i].name, "../external/blueMSX-go/system/bluemsx/Machines/Shared Roms/Nextor.ROM");
                i++;
            }

            machine->slotInfoCount = i;
            break;
    }
}

static void createMsxMachine(int msxType) {
    msxMachine = ahb_calloc(1,sizeof(Machine));

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
    if (0 == strcasecmp(ROM_EXT,MSX_DISK_EXTENSION_COMPRESSED)) {
        // Find if file is disk image or IDE HDD image
        const uint8_t *diskData = ROM_DATA;
        uint32_t payload_offset = diskData[4]+(diskData[5]<<8)+(diskData[6]<<16)+(diskData[7]<<24);
        if (payload_offset <= 0x288) {
            msx_game_type = MSX_GAME_DISK;
        } else {
            msx_game_type = MSX_GAME_HDIDE;
        }
    } else if (0 == strcasecmp(ROM_EXT,MSX_DISK_EXTENSION)) {
        if (ROM_DATA_LENGTH <= 737280)
            msx_game_type = MSX_GAME_DISK;
        else
            msx_game_type = MSX_GAME_HDIDE;
    } else {
            msx_game_type = MSX_GAME_ROM;
    }
    printf("Game type : %d\n",msx_game_type);
    setPropertiesMsx(msxMachine,msxType);
}

static void insertGame() {
    bool controls_found = true;
    printf("Rom Mapper %d\n",ROM_MAPPER);
    uint16_t mapper = ROM_MAPPER;
    switch (msx_game_type) {
        case MSX_GAME_ROM:
        {
            if (mapper == ROM_UNKNOWN) {
                uint32_t rom_size;
                uint8_t *rom_data;
                rom_data = ROM_DATA;
                rom_size = ROM_DATA_LENGTH;
                mapper = GuessROM(rom_data,rom_size);
            }

            printf("insertCartridge msx mapper %d\n",mapper);
            insertCartridge(properties, 0, ROM_NAME, NULL, mapper, -1);
            break;
        }
        case MSX_GAME_DISK:
        {
            insertDiskette(properties, 0, ROM_NAME, NULL, -1);
            // We load SCC-I cartridge for disk games requiring it
            // We load SCC-I cartridge for disk games requiring it
            switch (mapper) {
                case ROM_SNATCHER:
                case ROM_SDSNATCHER:
                    insertCartridge(properties, 0, CARTNAME_SNATCHER, NULL, ROM_SNATCHER, -1);
                break;
                case ROM_SCC:
                    insertCartridge(properties, 0, CARTNAME_SCC, NULL, ROM_SCC, -1);
                break;
            }
            break;
        }
        case MSX_GAME_HDIDE:
        {
            insertCartridge(properties, 1, CARTNAME_SNATCHER, NULL, ROM_SNATCHER, -1);
            insertDiskette(properties, 1, ROM_NAME, NULL, -1);
            break;
        }
    }
}

static void createProperties() {
    properties = propCreate(1, EMU_LANG_ENGLISH, P_KBD_EUROPEAN, P_EMU_SYNCNONE, "");
    properties->sound.stereo = 0;
//    properties->emulation.vdpSyncMode = P_VDP_SYNCAUTO;
//    properties->emulation.vdpSyncMode = P_VDP_SYNC50HZ;
    properties->emulation.vdpSyncMode = P_VDP_SYNCAUTO;
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

#if 0
size_t msx_getromdata(uint8_t **data, uint8_t *src_data, size_t src_size, const char *ext)
{
    /* src pointer to the ROM data in the external flash (raw or LZ4) */
    unsigned char *dest = (unsigned char *)rom_decompress_buffer;
    uint32_t available_size = (uint32_t)sizeof(rom_decompress_buffer);

    wdog_refresh();
    if(strcmp(ext, "lzma") == 0){
        size_t n_decomp_bytes;
        n_decomp_bytes = lzma_inflate(dest, available_size, src_data, src_size);
        *data = dest;
        return n_decomp_bytes;
    }
    else
    {
        *data = (unsigned char *)src_data;

        return src_size;
    }
}
#endif




static inline void blit_normal() {
    const int w2 = width;
    const int h2 = height;
    Uint8  *src_row;
    UInt16 *dest_row;

    for (int y = 0; y < h2; y++) {
        src_row  = &image_buffer[y*width];
        dest_row = &image_buffer_16[y * w2];
        for (int x = 0; x < width; x++) {
            dest_row[x] = palette565[src_row[x]];
        }
    }
}


int main(int argc, char *argv[])
{
    int drawFrame;
    init_window(width, height);

    odroid_system_init(APP_ID, AUDIO_MSX_SAMPLE_RATE);
//    odroid_system_emu_init(&msx_system_LoadState, &msx_system_SaveState, NULL, NULL, NULL, NULL, NULL);

    /* Init controls */
    memset(&previous_joystick_state,0, sizeof(odroid_gamepad_state_t));

   image_buffer               =  malloc(FB_MAX_LINE_WIDTH*FB_MAX_LINES*sizeof(uint8_t));
   image_buffer_16            =  malloc(FB_MAX_LINE_WIDTH*FB_MAX_LINES*sizeof(uint16_t));
   image_buffer_base_width    =  width;
   image_buffer_current_width =  width;
   image_buffer_height        =  height;
   double_width = 0;

   disk_index = 0;
   disk_images = 0;
   disk_inserted = false;

    // Create RGB8 to RGB565 table
    for (int i = 0; i < 256; i++)
    {
        // RGB 8bits to RGB 565 (RRR|GGG|BB -> RRRRR|GGGGGG|BBBBB)
        palette565[i] = (((i>>5)*31/7)<<11) |
                         ((((i&0x1C)>>2)*63/7)<<5) |
                         ((i&0x3)*31/3);
    }

    setupEmulatorRessources(2); // 0 = MSX1 1 = MSX2 2 = MSX2+

    const char* tracePc = getenv("MSX_TRACE_PC");
    static uint16_t lastPc;
    static int samePc;
    int frame = 0;

    while (1) {
        keyboardUpdate();

        // Render 1 frame
        ((R800*)boardInfo.cpuRef)->terminate = 0;
        boardInfo.run(boardInfo.cpuRef);

        if (tracePc != NULL) {
            uint16_t pc = ((R800*)boardInfo.cpuRef)->regs.PC.W;
            if (pc == lastPc) {
                samePc++;
            } else {
                samePc = 0;
            }
            lastPc = pc;
            if (frame < 8 || samePc == 60 || (frame % 120) == 0) {
                fprintf(stderr, "frame %d PC=%04X same=%d CCDD=%02X mem38=%02X%02X D020=%02X%02X\n",
                        frame, pc, samePc, slotRead(boardInfo.cpuRef, 0xCCDD),
                        slotRead(boardInfo.cpuRef, 0x0038), slotRead(boardInfo.cpuRef, 0x0039),
                        slotRead(boardInfo.cpuRef, 0xD020), slotRead(boardInfo.cpuRef, 0xD021));
            }
            if (samePc > 300) {
                fprintf(stderr, "CPU stuck at PC=%04X\n", pc);
                samePc = 0;
            }
            frame++;
        }

        // If current MSX screen mode is 10 or 12, data has been directly written into
        // framebuffer elseway convert 8bit pixels to RGB565
        if ((vdpGetScreenMode() != 10) && (vdpGetScreenMode() != 12)) {
                blit_normal();
        }
        SDL_UpdateTexture(fb_texture, NULL, image_buffer_16, image_buffer_current_width * BPP);
        SDL_RenderCopy(renderer, fb_texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        // Render audio
        mixerSyncGNW(mixer,(AUDIO_MSX_SAMPLE_RATE/msx_fps));
    }

    SDL_Quit();

    return 0;
}

/* Core stubs */
void frameBufferDataDestroy(FrameBufferData* frameData){}
void frameBufferSetActive(FrameBufferData* frameData){}
void frameBufferSetMixMode(FrameBufferMixMode mode, FrameBufferMixMode mask){}
void frameBufferClearDeinterlace(){}
void frameBufferSetInterlace(FrameBuffer* frameBuffer, int val){}
void archTrap(UInt8 value){}
void videoUpdateAll(Video* video, Properties* properties){}



static volatile Int16 *Buffer = 0;
static unsigned int BufferSize;
static unsigned int BufferRead;
static unsigned int BufferWrite;
static volatile unsigned int BufferIn;
long soundbufsize=240;

static void fillaudio(void *udata, Uint8 *stream, int len)
{
 Int16 *tmps = (Int16*)stream;
 len >>= 1;

 while(len)
 {
  Int16 sample = 0;
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

Uint32 GetMaxSound(void)
{
 return(BufferSize);
}

Uint32 GetWriteSound(void)
{
 return(BufferSize - BufferIn);
}

void WriteSound(Int16 *buf, int Count)
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

static Int32 soundWrite(void* dummy, Int16 *buffer, UInt32 count) {
    WriteSound(buffer,count);
}

void archSoundCreate(Mixer* mixer, UInt32 sampleRate, UInt32 bufferSize, Int16 channels) {
    SDL_AudioSpec wanted;

    printf("archSoundCreate channels %d\n",channels,sampleRate);
    /* Set the audio format */
    wanted.freq = AUDIO_MSX_SAMPLE_RATE;
    wanted.format = AUDIO_S16;
    wanted.channels = 1;
    wanted.samples = 1024;
    wanted.callback = fillaudio;
    wanted.userdata = NULL;

    BufferSize = soundbufsize * AUDIO_MSX_SAMPLE_RATE / 1000;

    BufferSize -= wanted.samples * 2;		/* SDL uses at least double-buffering, so
                            multiply by 2. */

    if(BufferSize < wanted.samples) BufferSize = wanted.samples;

    Buffer = malloc(sizeof(int) * BufferSize);
    BufferRead = BufferWrite = BufferIn = 0;

    /* Open the audio device, forcing the desired format */
    if (SDL_OpenAudio(&wanted, NULL) < 0) {
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        return;
    }

    SDL_PauseAudio(0);

    // Init Sound
    mixerSetStereo(mixer, 0);
    mixerSetWriteCallback(mixer, soundWrite, NULL, (AUDIO_MSX_SAMPLE_RATE/msx_fps));

    mixerSetEnable(mixer,1);
    mixerSetMasterVolume(mixer,100);
}


void archSoundDestroy(void)
{
   if (sdlSound.started) {
      mixerSetWriteCallback(sdlSound.mixer, NULL, NULL, 0);
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
   }
   sdlSound.started = 0;
}

void archShowStartEmuFailDialog() {}

#ifdef MSX_NO_FILESYSTEM
const char* boardGetBaseDirectory(void)
{
    return ".";
}
#endif
