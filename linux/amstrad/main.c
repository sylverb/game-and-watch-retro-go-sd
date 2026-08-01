#include <odroid_system.h>

#include <SDL2/SDL.h>

#include <assert.h>
#include "gw_lcd.h"
#include "rom_manager.h"
#include "appid.h"
#include "cap32.h"
#include "main_amstrad.h"
#include "amstrad_loader.h"
#include "main.h"

int SHIFTON = 0;
char DISKA_NAME[512]="\0";
char DISKB_NAME[512]="\0";
char cart_name[512]="\0";
char loader_buffer[256];
int emu_status;

#define GW_LCD_WIDTH  320
#define GW_LCD_HEIGHT 240

extern const unsigned char ROM_DATA[];
extern unsigned int ROM_DATA_LENGTH;
extern unsigned int cart_rom_len;
extern const char *ROM_NAME;
extern const char *ROM_EXT;
extern unsigned int *ROM_MAPPER;

static bool auto_key = false;

#define AMSTRAD_FPS 50
#define AMSTRAD_SAMPLE_RATE 22050
#define AUDIO_BUFFER_LENGTH_AMSTRAD  (AMSTRAD_SAMPLE_RATE / AMSTRAD_FPS)

static int16_t soundBuffer[AUDIO_BUFFER_LENGTH_AMSTRAD];


#define AMSTRAD_DISK_EXTENSION "dsk"

static uint16_t palette565[256];

uint8_t amstrad_framebuffer[CPC_SCREEN_WIDTH * CPC_SCREEN_HEIGHT];
uint16_t fb_data[CPC_SCREEN_WIDTH * CPC_SCREEN_HEIGHT*6];

unsigned int *amstrad_getScreenPtr()
{
    return (unsigned int *)amstrad_framebuffer;
}

void amstrad_ui_set_led(bool state) {
}

void retro_audio_mix_batch() {

}

typedef struct SdlSound {
//    Mixer* mixer;
    int started;
    uint32_t readPtr;
    uint32_t writePtr;
    uint32_t bytesPerSample;
    uint32_t bufferMask;
    uint32_t bufferSize;
    uint32_t skipCount;
    uint8_t* buffer;
} SdlSound;

SdlSound sdlSound;
int oldLen = 0;

static odroid_gamepad_state_t previous_joystick_state;

/*
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
                    size = saveMsxState((uint8_t *)saveBuffer,1024*1024);
                    fwrite(saveBuffer, size, 1, pFile);
                    fclose(pFile);
                }
                break;
            case SDLK_F4:
                if (last_down_event.key.keysym.sym == SDLK_F4) {
                    FILE* pFile = fopen("saveMsx.bin","rb");
                    if (pFile) {
                        fread(saveBuffer, 1024*1024, 1, pFile);
                        loadMsxState((uint8_t *)saveBuffer,1024*1024);
                    }
                }
                break;
            default:
                break;
            }
        }
     }
} 
*/

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *fb_texture;

int offset_audio=0;

static bool amstrad_LoadState(const char *pathName)
{
      printf("Loading state not implemented...\n");
      return true;
}

static bool amstrad_SaveState(const char *pathName)
{
      printf("Saving state not implemented...\n");
      return true;
}

int init_window(int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO/*|SDL_INIT_AUDIO*/) != 0)
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

//The frames per second cap timer
int capTimer;

SDL_mutex *sndlock;

void blitscreen(uint8_t *src_fb)
{
    const int w1 = CPC_SCREEN_WIDTH;
    const int w2 = CPC_SCREEN_WIDTH;
    const int h2 = CPC_SCREEN_HEIGHT;
    uint8_t *src_row;
    uint16_t *dest_row;

    for (int y = 0; y < h2; y++)
    {
        src_row = &src_fb[y * w1];
        dest_row = &fb_data[y * w2];
        for (int x = 0; x < w2; x++)
        {
            dest_row[x] = palette565[src_row[x]];
        }
    }

    SDL_UpdateTexture(fb_texture, NULL, fb_data, CPC_SCREEN_WIDTH * BPP);
    SDL_RenderCopy(renderer, fb_texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

bool amstrad_is_cpm;

static int autorun_delay = 50;
static int wait_computer = 1;

static void computer_autoload()
{
    amstrad_is_cpm = false;
    // if name contains [CPM] then it's CPM file
    //    if (file_check_flag(ACTIVE_FILE->name, size, FLAG_BIOS_CPM, 5))
    //    {
    //        amstrad_is_cpm = true;
    //    }

    //   if (game_configuration.has_btn && retro_computer_cfg.use_internal_remap)
    //   {
    //      printf("[DB] game remap applied.\n");
    //      memcpy(btnPAD[0].buttons, game_configuration.btn_config.buttons, sizeof(t_button_cfg));
    //   }

    //   if (game_configuration.has_command)
    //   {
    //      strncpy(loader_buffer, game_configuration.loader_command, LOADER_MAX_SIZE);
    //   } else {
    loader_run(loader_buffer);
    //   }

    strcat(loader_buffer, "\n");
    printf("[core] DSK autorun: %s", loader_buffer);
    kbd_buf_feed(loader_buffer);
    auto_key = true;
    //   if (amstrad_is_cpm)
    //   {
    //      cpm_boot(loader_buffer);
    //   }
    //   else
    //   {
    //      ev_autorun_prepare(loader_buffer);
    //   }
}

bool autorun_command()
{
    if (autorun_delay)
    {
        autorun_delay--;
        return false;
    }

    // wait one loop for the key be pressed
    wait_computer ^= 1;
    if (wait_computer)
        return false;

    if (kbd_buf_update())
    {
        auto_key = false;
        // prepare next autorun
        autorun_delay = 50;
        wait_computer = 1;
    }

    return true;
}

int main(int argc, char *argv[])
{
    odroid_gamepad_state_t joystick = {0};
    int drawFrame;
    init_window(CPC_SCREEN_WIDTH, CPC_SCREEN_HEIGHT);

    odroid_system_init(APPID_AMSTRAD, AMSTRAD_SAMPLE_RATE);
    odroid_system_emu_init(&amstrad_LoadState, &amstrad_SaveState, NULL, NULL, NULL, NULL, NULL);

    /* Init controls */
    memset(&previous_joystick_state,0, sizeof(odroid_gamepad_state_t));

    // Create RGB8 to RGB565 table
    for (int i = 0; i < 256; i++)
    {
        // RGB 8bits to RGB 565 (RRR|GGG|BB -> RRRRR|GGGGGG|BBBBB)
        palette565[i] = (((i >> 5) * 31 / 7) << 11) |
                        ((((i & 0x1C) >> 2) * 63 / 7) << 5) |
                        ((i & 0x3) * 31 / 3);
    }

    capmain(0, NULL);

    amstrad_set_audio_buffer((int8_t *)soundBuffer, sizeof(soundBuffer));


    attach_disk("Rick Dangerous 2.dsk", 0);
//    attach_disk("Batman Forever.dsk", 0);

    cap32_set_palette(0);
    computer_autoload();


    while (1) {
        if (auto_key) {
            autorun_command();
        } else {
            odroid_input_read_gamepad(&joystick);
        }

        // Render 1 frame
        caprice_retro_loop();
        blitscreen(amstrad_framebuffer);

        // Render audio
    }

    SDL_Quit();

    return 0;
}


static volatile int16_t *Buffer = 0;
static unsigned int BufferSize;
static unsigned int BufferRead;
static unsigned int BufferWrite;
static volatile unsigned int BufferIn;
long soundbufsize=240;

static void fillaudio(void *udata, uint8_t *stream, int len)
{
 int16_t *tmps = (int16_t*)stream;
 len >>= 1;

 while(len)
 {
  int16_t sample = 0;
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

uint32_t GetMaxSound(void)
{
 return(BufferSize);
}

uint32_t GetWriteSound(void)
{
 return(BufferSize - BufferIn);
}

void WriteSound(int16_t *buf, int Count)
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

static int32_t soundWrite(void* dummy, int16_t *buffer, uint32_t count) {
    WriteSound(buffer,count);
}

#if 0
void archSoundCreate(Mixer* mixer, uint32_t sampleRate, uint32_t bufferSize, int16_t channels) {
    SDL_AudioSpec wanted;

    printf("archSoundCreate channels %d\n",channels,sampleRate);
    /* Set the audio format */
    wanted.freq = AMSTRAD_SAMPLE_RATE;
    wanted.format = AUDIO_S16;
    wanted.channels = 1;
    wanted.samples = 1024;
    wanted.callback = fillaudio;
    wanted.userdata = NULL;

    BufferSize = soundbufsize * AMSTRAD_SAMPLE_RATE / 1000;

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
    mixerSetWriteCallback(mixer, soundWrite, NULL, AUDIO_BUFFER_LENGTH_AMSTRAD);

    mixerSetEnable(mixer,1);
    mixerSetMasterVolume(mixer,100);
}
#endif


void archSoundDestroy(void)
{
   if (sdlSound.started) {
     SDL_QuitSubSystem(SDL_INIT_AUDIO);
   }
   sdlSound.started = 0;
}

void archShowStartEmuFailDialog() {}
