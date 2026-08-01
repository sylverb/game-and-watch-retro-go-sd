#include <odroid_system.h>
#include <string.h>

#include "main.h"
#include "bilinear.h"
#include "gw_lcd.h"
#include "gw_linker.h"
#include "gw_buttons.h"
#include "shared.h"
#include "rom_manager.h"
#include "common.h"
#include "main_smsplusgx.h"
#include "appid.h"
#include "rg_i18n.h"
#ifndef GNW_DISABLE_COMPRESSION
#include "lzma.h"
#endif
#include "gw_malloc.h"

#define SMS_WIDTH 256
#define SMS_HEIGHT 192

#define COL_WIDTH   272
#define COL_HEIGHT  208

#define GG_WIDTH 160
#define GG_HEIGHT 144

#define PIXEL_MASK 0x1F
#define PAL_SHIFT_MASK 0x80

#define AUDIO_BUFFER_LENGTH_SMS (AUDIO_SAMPLE_RATE / 60)

static uint16_t palette[32];
static uint32_t palette_spaced[32];


static bool consoleIsGG  = false;
static bool consoleIsSMS = false;
static bool consoleIsCOL = false;
static bool consoleIsSG  = false;

// TODO: Move to lcd.c/h
extern LTDC_HandleTypeDef hltdc;

void set_config();
unsigned int crc32_le(unsigned int crc, unsigned char const * buf,unsigned int len);

// --- MAIN
#ifndef GNW_DISABLE_COMPRESSION
#define SMSROM_RAM_BUFFER_LENGTH (60*1024)
static uint8_t *ROMinRAM_DATA;

#endif

static void blit_console();

static int
load_rom_from_flash(uint8_t emu_engine)
{
    static uint8 sram[0x8000];
    /* check if it's compressed */

#ifndef GNW_DISABLE_COMPRESSION
#if SD_CARD == 1
#error "Roms compression is not supported on SD Card"
#else
    uint32_t src_size = 0;
    const uint8_t *src = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &src_size, false);
    if (src == NULL || src_size == 0) {
        return 0;
    }
    if (strcmp(ACTIVE_FILE->ext, "lzma") == 0)
    {
        /* it can fit in ITC RAM */
        if ((emu_engine == SMSPLUSGX_ENGINE_COLECO) || (emu_engine == SMSPLUSGX_ENGINE_SG1000))
        {
            size_t n_decomp_bytes;
            ROMinRAM_DATA = itc_malloc(SMSROM_RAM_BUFFER_LENGTH);
            n_decomp_bytes = lzma_inflate((uint8 *)ROMinRAM_DATA, SMSROM_RAM_BUFFER_LENGTH, (uint8 *)src, src_size);
            cart.rom = (uint8 *)ROMinRAM_DATA;
            cart.size = (uint32_t)n_decomp_bytes;
        }
    }
    else
    {
        cart.rom = (uint8 *)src;
        cart.size = src_size;
    }
#endif
#else
    ram_start = (uint32_t)&_OVERLAY_SMS_BSS_END;
    uint32_t size = ACTIVE_FILE->size;
    if (size > ram_get_free_size()) {
        cart.rom = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &size, false);
    } else {
        cart.rom = ram_malloc(size);
        if (cart.rom != NULL) {
            odroid_overlay_cache_file_in_ram(ACTIVE_FILE->path, cart.rom);
        }
    }
    cart.size = size;
#endif
    if (cart.rom == NULL) {
        return 0;
    }
    cart.sram = sram;
    cart.pages = cart.size / 0x4000;
    cart.crc = crc32_le(0, cart.rom, cart.size);
    cart.loaded = 1;

    if (emu_engine == SMSPLUSGX_ENGINE_COLECO)
    {
        option.console = 6; // Force Coleco
    }
    else if (emu_engine == SMSPLUSGX_ENGINE_SG1000)
    {
        option.console = 5; // Force SG1000
    }
    set_config();
    printf("%s: OK. cart.size=%d, cart.crc=%#010lx\n", __func__, (int)cart.size, cart.crc);

    if (sms.console == CONSOLE_COLECO)
    {
        coleco.rom = (uint8*)ahb_malloc(0x2000); // 8KB bios
        printf("Loading Coleco BIOS %p\n", coleco.rom);
        odroid_sdcard_read_file("/bios/coleco/coleco.bin", coleco.rom, 0x2000);
    }
    return 1;
}

extern uint32 glob_bp_lut[0x10000];
#define SAVE_STATE_BUFFER_SIZE (60 * 1024)

static bool SaveState(const char *savePathName)
{
    uint8_t *state_save_buffer = (uint8_t *)glob_bp_lut;
    system_save_state(state_save_buffer);

    FILE *file = fopen(savePathName, "wb");
    if (file == NULL) {
        return false;
    }

    size_t written = fwrite(state_save_buffer, SAVE_STATE_BUFFER_SIZE, 1, file);

    fclose(file);

    if (!written) {
        return false;
    }

    /* restore the contents of _bp_lut */
    render_init();

    return true;
}

static bool LoadState(const char *savePathName)
{
    uint8_t *state_save_buffer = (uint8_t *)glob_bp_lut;

    FILE *file = fopen(savePathName, "rb");
    if (file == NULL) {
        return false;
    }

    size_t read = fread(state_save_buffer, SAVE_STATE_BUFFER_SIZE, 1, file);

    fclose(file);

    if (!read) {
        return false;
    }

    system_load_state((void *)state_save_buffer);
    /* restore the contents of _bp_lut */
    render_init();

    return true;
}

static void *Screenshot()
{
    lcd_wait_for_vblank();

    lcd_clear_active_buffer();
    blit_console();
    return lcd_get_active_buffer();
}

static uint8_t fb_buffer[COL_WIDTH*COL_HEIGHT];

#define CONV(_b0) ((0b11111000000000000000000000&_b0)>>10) | ((0b000001111110000000000&_b0)>>5) | ((0b0000000000011111&_b0));

static void
blit_gg(bitmap_t *bmp, uint16_t *framebuffer) {	/* 160 x 144 -> 320 x 240 */
    int y_src = 0;
    int y_dst = 0;
    for (; y_src < bmp->viewport.h; y_src += 3, y_dst += 5) {
        int x_src = 0;
        int x_dst = 0;
        for (; x_src < bmp->viewport.w; x_src += 1, x_dst += 2) {
            uint8_t *src_col = &bmp->data[(y_src + bmp->viewport.y) * bmp->pitch + x_src + bmp->viewport.x];
            uint32_t b0 = palette_spaced[src_col[bmp->pitch * 0] & 0x1f];
            uint32_t b1 = palette_spaced[src_col[bmp->pitch * 1] & 0x1f];
            uint32_t b2 = palette_spaced[src_col[bmp->pitch * 2] & 0x1f];

            framebuffer[((y_dst + 0) * WIDTH) + x_dst] = CONV(b0);
            framebuffer[((y_dst + 1) * WIDTH) + x_dst] = CONV((b0+b1)>>1);
            framebuffer[((y_dst + 2) * WIDTH) + x_dst] = CONV(b1);
            framebuffer[((y_dst + 3) * WIDTH) + x_dst] = CONV((b1+b2)>>1);
            framebuffer[((y_dst + 4) * WIDTH) + x_dst] = CONV(b2);

            framebuffer[((y_dst + 0) * WIDTH) + x_dst + 1] = CONV(b0);
            framebuffer[((y_dst + 1) * WIDTH) + x_dst + 1] = CONV((b0+b1)>>1);
            framebuffer[((y_dst + 2) * WIDTH) + x_dst + 1] = CONV(b1);
            framebuffer[((y_dst + 3) * WIDTH) + x_dst + 1] = CONV((b1+b2)>>1);
            framebuffer[((y_dst + 4) * WIDTH) + x_dst + 1] = CONV(b2);
        }
    }
}

static void
blit_sms(bitmap_t *bmp, uint16_t *framebuffer) {	/* 256 x 192 -> 320 x 230 */
    const int hpad = (WIDTH - 320) / 2;
    const int vpad = (HEIGHT - 230) / 2;

    uint32_t block[6 * 5]; /* workspace: 5 rows, 6 pixels wide */

    int y_src = 1;         /* 1st and last row of 192 will not be scaled */
    int y_dst = 1 + vpad;  /* the remaining 190 are scaled */
    for (; y_src < bmp->viewport.h - 1; y_src += 5, y_dst += 6) {
        int x_src = 0;
        int x_dst = hpad;
        for (; x_src < bmp->viewport.w - 1; x_src += 5, x_dst += 6) {
            for (int y = 0; y < 5; y++) {
                uint8_t *src_row = &bmp->data[(y_src + y + bmp->viewport.y) * bmp->pitch];
                uint32_t b0 = palette_spaced[src_row[x_src + 0] & 0x1f];
                uint32_t b1 = palette_spaced[src_row[x_src + 1] & 0x1f];
                uint32_t b2 = palette_spaced[src_row[x_src + 2] & 0x1f];
                uint32_t b3 = palette_spaced[src_row[x_src + 3] & 0x1f];
                uint32_t b4 = palette_spaced[src_row[x_src + 4] & 0x1f];

                block[(y * 6) + 0] = b0;
                block[(y * 6) + 1] = (b0+b1+b1+b1)>>2;
                block[(y * 6) + 2] = (b1+b2)>>1;
                block[(y * 6) + 3] = (b2+b3)>>1;
                block[(y * 6) + 4] = (b3+b3+b3+b4)>>2;
                block[(y * 6) + 5] = b4;
            }

            for (int x = 0; x < 6; x++) {
                uint32_t b0 = block[(0 * 6) + x];
                uint32_t b1 = block[(1 * 6) + x];
                uint32_t b2 = block[(2 * 6) + x];
                uint32_t b3 = block[(3 * 6) + x];
                uint32_t b4 = block[(4 * 6) + x];

                framebuffer[((y_dst + 0) * WIDTH) + x + x_dst] = CONV(b0);
                framebuffer[((y_dst + 1) * WIDTH) + x + x_dst] = CONV((b0+b1+b1+b1)>>2);
                framebuffer[((y_dst + 2) * WIDTH) + x + x_dst] = CONV((b1+b2)>>1);
                framebuffer[((y_dst + 3) * WIDTH) + x + x_dst] = CONV((b2+b3)>>1);
                framebuffer[((y_dst + 4) * WIDTH) + x + x_dst] = CONV((b3+b3+b3+b4)>>2);
                framebuffer[((y_dst + 5) * WIDTH) + x + x_dst] = CONV(b4);
            }
        }

        /* Last column, x_src = 255 */
        uint8_t *src_col = &bmp->data[(y_src + bmp->viewport.y) * bmp->pitch + x_src];
        uint32_t b0 = palette_spaced[src_col[bmp->pitch * 0] & 0x1f];
        uint32_t b1 = palette_spaced[src_col[bmp->pitch * 1] & 0x1f];
        uint32_t b2 = palette_spaced[src_col[bmp->pitch * 2] & 0x1f];
        uint32_t b3 = palette_spaced[src_col[bmp->pitch * 3] & 0x1f];
        uint32_t b4 = palette_spaced[src_col[bmp->pitch * 4] & 0x1f];

        framebuffer[((y_dst + 0) * WIDTH) + x_dst] = CONV(b0);
        framebuffer[((y_dst + 1) * WIDTH) + x_dst] = CONV((b0+b1+b1+b1)>>2);
        framebuffer[((y_dst + 2) * WIDTH) + x_dst] = CONV((b1+b2)>>1);
        framebuffer[((y_dst + 3) * WIDTH) + x_dst] = CONV((b2+b3)>>1);
        framebuffer[((y_dst + 4) * WIDTH) + x_dst] = CONV((b3+b3+b3+b4)>>2);
        framebuffer[((y_dst + 5) * WIDTH) + x_dst] = CONV(b4);
    }

    y_src = 0;		   /* First & last row */
    y_dst = 0 + vpad;
    for (; y_src < bmp->viewport.h; y_src += 191, y_dst += 228) {
        uint8_t *src_row = &bmp->data[(y_src + bmp->viewport.y) * bmp->pitch];
        uint16_t *dest_row = &framebuffer[WIDTH * y_dst];
        int x_src = 0;
        int x_dst = hpad;
        for (; x_src < bmp->viewport.w - 1; x_src += 5, x_dst += 6) {
            uint32_t b0 = palette_spaced[src_row[x_src + 0] & 0x1f];
            uint32_t b1 = palette_spaced[src_row[x_src + 1] & 0x1f];
            uint32_t b2 = palette_spaced[src_row[x_src + 2] & 0x1f];
            uint32_t b3 = palette_spaced[src_row[x_src + 3] & 0x1f];
            uint32_t b4 = palette_spaced[src_row[x_src + 4] & 0x1f];

            dest_row[x_dst + 0]   = CONV(b0);
            dest_row[x_dst + 1] = CONV((b0+b1+b1+b1)>>2);
            dest_row[x_dst + 2] = CONV((b1+b2)>>1);
            dest_row[x_dst + 3] = CONV((b2+b3)>>1);
            dest_row[x_dst + 4] = CONV((b3+b3+b3+b4)>>2);
            dest_row[x_dst + 5] = CONV(b4);
        }
        /* Last column, x_src = 255 */
        dest_row[x_dst] = CONV(palette_spaced[src_row[x_src] & 0x1f]);
    }
}

void sms_pcm_submit() {
    if (common_emu_sound_loop_is_muted()) {
        return;
    }

    int32_t factor = common_emu_sound_get_volume() / 2; // Divide by 2 to prevent overflow in stereo mixing
    int16_t* sound_buffer = audio_get_active_buffer();
    uint16_t sound_buffer_length = audio_get_buffer_length();

    for (int i = 0; i < sound_buffer_length; i++) {
        /* mix left & right */
        int32_t sample = (sms_snd.output[0][i] + sms_snd.output[1][i]);
        sound_buffer[i] = (sample * factor) >> 8;
    }
}

static void blit_console() {
    pixel_t* curr_framebuffer = lcd_get_active_buffer();
    if (sms.console == CONSOLE_GG)     blit_gg(&bitmap, curr_framebuffer);
    else                               blit_sms(&bitmap, curr_framebuffer);
}

static void blit()
{
    blit_console();
    common_ingame_overlay();
}

static void sms_draw_frame()
{
  static uint32_t lastFPSTime = 0;
  static uint32_t frames = 0;

  uint32_t currentTime = HAL_GetTick();
  uint32_t delta = currentTime - lastFPSTime;

  frames++;

  if (delta >= 1000) {
      int fps = (10000 * frames) / delta;
      printf("FPS: %d.%d, frames %ld, delta %ld ms\n", fps / 10, fps % 10, frames, delta);
      frames = 0;
      lastFPSTime = currentTime;
  }

  render_copy_palette((uint16_t *)palette);
  for (int i = 0; i < 32; i++) {
      uint16_t p = (palette[i] << 8) | (palette[i] >> 8);
      palette_spaced[i] = ((0b1111100000000000 & p) << 10) |
                          ((0b0000011111100000 & p) << 5) |
                          ((0b0000000000011111 & p));
  }

  blit();
  common_ingame_overlay();
  lcd_swap();
}

static void sms_update_keys( odroid_gamepad_state_t* joystick )
{
  uint8 k = 0;

  input.pad[0] = 0x00;
  input.pad[1] = 0x00;
  input.system = 0x00;

  if (joystick->values[ODROID_INPUT_UP])    { input.pad[0] |= INPUT_UP;       k = 1; }
  if (joystick->values[ODROID_INPUT_RIGHT]) { input.pad[0] |= INPUT_RIGHT;    k = 2; }
  if (joystick->values[ODROID_INPUT_DOWN])  { input.pad[0] |= INPUT_DOWN;     k = 3; }
  if (joystick->values[ODROID_INPUT_LEFT])  { input.pad[0] |= INPUT_LEFT;     k = 4; }
  if (joystick->values[ODROID_INPUT_A])     { input.pad[0] |= INPUT_BUTTON2;  k = 5; }
  if (joystick->values[ODROID_INPUT_B])     { input.pad[0] |= INPUT_BUTTON1;  k = 6; }

  if (consoleIsGG)
  {
      if ((joystick->values[ODROID_INPUT_SELECT]) || (joystick->values[ODROID_INPUT_Y]))  input.system |= INPUT_PAUSE;
      if ((joystick->values[ODROID_INPUT_START]) || (joystick->values[ODROID_INPUT_X]))  input.system |= INPUT_START;
  }
  else if (consoleIsCOL)
  {
      // G&W Key      : Coleco
      // Game         : 1
      // Game + Up    : 2
      // Game + Left  : 3
      // Game + Down  : 4
      // Game + Right : 5
      // Game + A     : 6
      // Game + B     : 0
      // Time         : #
      // Time + Up    : *
      // Time + Left  : 9
      // Time + Down  : 8
      // Time + Right : 7
      // Time + A     : 6
      // Time + B     : 5

      coleco.keypad[0] = 0xf0;
      if ((joystick->values[ODROID_INPUT_SELECT]) || (joystick->values[ODROID_INPUT_Y])) coleco.keypad[0] = 11 - k;
      else
      if ((joystick->values[ODROID_INPUT_START]) || (joystick->values[ODROID_INPUT_X]))  coleco.keypad[0] = (1 + k) % 7;
  }
  else { // Default like SMS
      if ((joystick->values[ODROID_INPUT_SELECT]) || (joystick->values[ODROID_INPUT_Y])) input.system |= INPUT_PAUSE;
      if ((joystick->values[ODROID_INPUT_START]) || (joystick->values[ODROID_INPUT_X]))  input.system |= INPUT_START;
  }
}


int
app_main_smsplusgx(uint8_t load_state, uint8_t start_paused, int8_t save_slot, uint8_t is_coleco)
{
    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }

    odroid_system_init(APPID_SMS, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, NULL, NULL, NULL, NULL);

    system_reset_config();
    load_rom_from_flash( is_coleco );

    sms.use_fm = 0;

    if (sms.console == CONSOLE_COLECO) {
        bitmap.width  = COL_WIDTH;
        bitmap.height = COL_HEIGHT;
        bitmap.pitch  = bitmap.width;
        bitmap.data   = fb_buffer;
    } else {
        bitmap.width = SMS_WIDTH;
        bitmap.height = SMS_HEIGHT;
        bitmap.pitch = bitmap.width;
        bitmap.data = fb_buffer;
    }

    // sms.dummy = framebuffer[0]; //A normal cart shouldn't access this memory ever. Point it to vram just in case.

    option.sndrate = AUDIO_SAMPLE_RATE;
    option.overscan = 0;
    option.extra_gg = 0;

    system_init2();
    system_reset();

    audio_start_playing(AUDIO_BUFFER_LENGTH_SMS);

    consoleIsSMS = sms.console == CONSOLE_SMS || sms.console == CONSOLE_SMS2;
    consoleIsGG  = sms.console == CONSOLE_GG || sms.console == CONSOLE_GGMS;
    consoleIsCOL = sms.console == CONSOLE_COLECO;
    consoleIsSG  = sms.console == CONSOLE_SG1000;

    if (sms.display == DISPLAY_NTSC) {
        common_emu_state.frame_time_10us = (uint16_t)(100000 / FPS_NTSC + 0.5f);
    }
    else {
        common_emu_state.frame_time_10us = (uint16_t)(100000 / FPS_PAL + 0.5f);
    }

    if (load_state) {
        odroid_system_emu_load_state(save_slot);
    } else {
        lcd_clear_buffers();
    }

    odroid_gamepad_state_t joystick;
    odroid_dialog_choice_t options[] = {
            ODROID_DIALOG_CHOICE_LAST
    };
    while (true)
    {
        wdog_refresh();

        bool drawFrame = common_emu_frame_loop();

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &blit);
        common_emu_input_loop_handle_turbo(&joystick);

        sms_update_keys( &joystick );

        system_frame(!drawFrame);

        if (drawFrame) {
            sms_draw_frame();
        }

        sms_pcm_submit();

        common_emu_sound_sync(false);
    }
}
