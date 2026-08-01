#include <odroid_system.h>

#include <assert.h>
#include "gw_lcd.h"
#include "gw_linker.h"
#include "gw_buttons.h"
#include "gw_malloc.h"
#include "rom_manager.h"
#include "common.h"
#ifndef GNW_DISABLE_COMPRESSION
#include "lzma.h"
#endif
#include "appid.h"
#include "bilinear.h"
#include "rg_i18n.h"

#include "Bios.h"
#include "Cartridge.h"
#include "Database.h"
#include "Maria.h"
#include "Palette.h"
#include "Pokey.h"
#include "Region.h"
#include "ProSystem.h"
#include "Tia.h"
#include "Memory.h"

#define ROM_BUFF_LENGTH 131200 // 128kB + header
#define TIA_MAX_LENGTH 624
#ifndef GNW_DISABLE_COMPRESSION
// Memory to handle compressed roms
static uint8_t rom_memory[ROM_BUFF_LENGTH];
#endif

static int videoWidth                  = 320;
static int videoHeight                 = 240;

static uint16_t display_palette16[256] = {0};
static uint8_t keyboard_data[17]       = {0};

static uint8_t *pokeyMixBuffer         = NULL;

static uint8_t save_buffer[32832];

static bool LoadState(const char *savePathName) {
    FILE *file = fopen(savePathName, "rb");
    if (file == NULL) {
        return false;
    }

    fread(save_buffer, sizeof(save_buffer), 1, file);

    fclose(file);

    if ((save_buffer[0] == '7') &&
        (save_buffer[1] == '8') &&
        (save_buffer[2] == '0') &&
        (save_buffer[3] == '0')) {
            prosystem_Load((const char *)save_buffer+4);
    } else {
        return false;
    }

    return true;
}

static bool SaveState(const char *savePathName) {
    save_buffer[0] = '7';
    save_buffer[1] = '8';
    save_buffer[2] = '0';
    save_buffer[3] = '0';
    int size = prosystem_Save((char *)save_buffer+4) + 4;

    FILE *file = fopen(savePathName, "wb");
    if (file == NULL) {
        fclose(file);
        return false;
    }

    size_t written = fwrite(save_buffer, size, 1, file);

    fclose(file);
    
    if (!written) {
        return false;
    }

    return true;
}

#define BLIT_VIDEO_BUFFER(typename_t, src, palette, width, height, pitch, dst) \
   {                                                                           \
      typename_t *surface = (typename_t*)dst;                                  \
      uint32_t x, y;                                                           \
                                                                               \
      for(y = 0; y < height; y++)                                              \
      {                                                                        \
         typename_t *surface_ptr = surface;                                    \
         const uint8_t *src_ptr  = src;                                        \
                                                                               \
         for(x = 0; x < width; x++)                                            \
            *(surface_ptr++) = *(palette + *(src_ptr++));                      \
                                                                               \
         surface += pitch;                                                     \
         src     += width;                                                     \
      }                                                                        \
   }

static void *Screenshot()
{
    lcd_wait_for_vblank();

    lcd_clear_active_buffer();

    uint8_t *buffer = maria_surface + ((maria_visibleArea.top - maria_displayArea.top) * Rect_GetLength(&maria_visibleArea));
    BLIT_VIDEO_BUFFER(uint16_t, buffer, display_palette16, 320, 240, 320, lcd_get_active_buffer());

    return lcd_get_active_buffer();
}

static size_t getromdata(unsigned char **data) {
    ram_start = (uint32_t)&_OVERLAY_A7800_BSS_END;
#ifndef GNW_DISABLE_COMPRESSION
#if SD_CARD == 1
#error "Roms compression is not supported on SD Card"
#else
    uint32_t src_size = 0;
    const unsigned char *src = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &src_size, false);
    unsigned char *dest = (unsigned char *)rom_memory;

    if(strcmp(ACTIVE_FILE->ext, "lzma") == 0){
        size_t n_decomp_bytes;
        n_decomp_bytes = lzma_inflate(dest, ROM_BUFF_LENGTH, src, src_size);
        *data = dest;
        ram_start += n_decomp_bytes;
        return n_decomp_bytes;
    }
    else
    {
        *data = (unsigned char *)src;
        return src_size;
    }
#endif
#else
    uint32_t size = ACTIVE_FILE->size;
    if (size > ram_get_free_size()) {
        *data = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &size, false);
    } else {
        *data = ram_malloc(size);
        if (*data != NULL) {
            odroid_overlay_cache_file_in_ram(ACTIVE_FILE->path, *data);
        }
    }
    return size;
#endif
}

static void display_ResetPalette(void)
{
   unsigned index;

   for(index = 0; index < 256; index++)
   {
      uint32_t r = palette_data[(index * 3) + 0] << 16;
      uint32_t g = palette_data[(index * 3) + 1] << 8;
      uint32_t b = palette_data[(index * 3) + 2];
      display_palette16[index] = ((r & 0xF80000) >> 8) |
                                 ((g & 0x00F800) >> 5) |
                                 ((b & 0x0000F8) >> 3);
   }
}

void update_joystick(odroid_gamepad_state_t *joystick) {
   // ----------------------------------------------------------------------------
   // SetInput
   // +----------+--------------+-------------------------------------------------
   // | Offset   | Controller   | Control
   // +----------+--------------+-------------------------------------------------
   // | 00       | Joystick 1   | Right
   // | 01       | Joystick 1   | Left
   // | 02       | Joystick 1   | Down
   // | 03       | Joystick 1   | Up
   // | 04       | Joystick 1   | Button 1
   // | 05       | Joystick 1   | Button 2
   // | 06       | Joystick 2   | Right
   // | 07       | Joystick 2   | Left
   // | 08       | Joystick 2   | Down
   // | 09       | Joystick 2   | Up
   // | 10       | Joystick 2   | Button 1
   // | 11       | Joystick 2   | Button 2
   // | 12       | Console      | Reset
   // | 13       | Console      | Select
   // | 14       | Console      | Pause
   // | 15       | Console      | Left Difficulty
   // | 16       | Console      | Right Difficulty
   // +----------+--------------+-------------------------------------------------
    keyboard_data[0]  = joystick->values[ODROID_INPUT_RIGHT] ? 1 : 0;
    keyboard_data[1]  = joystick->values[ODROID_INPUT_LEFT]  ? 1 : 0;
    keyboard_data[2]  = joystick->values[ODROID_INPUT_DOWN]  ? 1 : 0;
    keyboard_data[3]  = joystick->values[ODROID_INPUT_UP]    ? 1 : 0;
    keyboard_data[4]  = joystick->values[ODROID_INPUT_B]     ? 1 : 0;
    keyboard_data[5]  = joystick->values[ODROID_INPUT_A]     ? 1 : 0;
    if (joystick->values[ODROID_INPUT_SELECT] || joystick->values[ODROID_INPUT_Y]) {
        keyboard_data[13] = 1;
    } else {
        keyboard_data[13] = 0;
    }
    if (joystick->values[ODROID_INPUT_START] || joystick->values[ODROID_INPUT_X]) {
        keyboard_data[14] = 1;
    } else {
        keyboard_data[14] = 0;
    }
    keyboard_data[15] = 0;

}

static void sound_store()
{
    uint8_t *tia_samples_buf = tia_buffer;
    size_t j;

    /* Mix in sound generated by POKEY chip
    * (Ballblazer, Commando, various homebrew and hacks) */
    if(cartridge_pokey)
    {
        uint8_t *pokey_samples_buf = pokey_buffer;
        uint8_t *pokey_mix_buf     = pokeyMixBuffer;

        /* Copy samples to pokeyMixBuffer */
        for(j = 0; j < tia_size; j++)
            *(pokey_mix_buf++) = (*(tia_samples_buf++) + *(pokey_samples_buf++)) >> 1;

        /* pokeyMixBuffer 'replaces' tia_buffer */
        tia_samples_buf = pokeyMixBuffer;
    }

    if (common_emu_sound_loop_is_muted()) {
        return;
    }

    int32_t factor = common_emu_sound_get_volume();
    int16_t* sound_buffer = audio_get_active_buffer();
    uint16_t sound_buffer_length = audio_get_buffer_length();

    // Write to sound buffer and lower the volume accordingly
    for (int i = 0; i < sound_buffer_length; i++) {
        int32_t sample = *(tia_samples_buf++) << 8;
        sound_buffer[i] = (sample * factor) >> 8;
    }
}

int app_main_a7800(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    const uint8_t *buffer = NULL;
    uint32_t rom_length = 0;
    uint8_t *rom_ptr = NULL;

    odroid_gamepad_state_t joystick;
    odroid_dialog_choice_t options[] = {
        ODROID_DIALOG_CHOICE_LAST
    };

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }

    pokeyMixBuffer = (uint8_t*)itc_malloc(TIA_BUFFER_SIZE * sizeof(uint8_t));

    memset(keyboard_data, 0, sizeof(keyboard_data));

    /* Difficulty switches: 
     * Left position = (B)eginner, Right position = (A)dvanced
     * Left difficulty switch defaults to left position, "(B)eginner"
     */
    keyboard_data[15] = 1;
    /* Right difficulty switch defaults to right position,
     * "(A)dvanced", which fixes Tower Toppler
     */
    keyboard_data[16] = 0;

    rom_length = getromdata(&rom_ptr);

    if (cartridge_Load(true,rom_ptr,rom_length)) {
        bios_enabled = false; // Bios not loaded
    } else {
        // Rom not supported
        return 0;
    }
    display_ResetPalette();
    database_Load(cartridge_digest);
    prosystem_Reset();

    // Use detected system frequency
    common_emu_state.frame_time_10us = (uint16_t)(100000 / prosystem_frequency + 0.5f);

    odroid_system_init(APPID_A7800, tia_size*prosystem_frequency); // 31200Hz for PAL, 31440Hz for NTSC
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot, NULL, NULL, NULL, NULL);

    // Init Sound
    audio_start_playing(tia_size);

    if (load_state) {
        odroid_system_emu_load_state(save_slot);
    } else {
        lcd_clear_buffers();
    }

    void blit()
    {
        videoWidth  = Rect_GetLength(&maria_visibleArea);
        videoHeight = Rect_GetHeight(&maria_visibleArea);
        buffer      = maria_surface + ((maria_visibleArea.top - maria_displayArea.top) * Rect_GetLength(&maria_visibleArea));
        BLIT_VIDEO_BUFFER(uint16_t, buffer, display_palette16, 320, 240, 320, lcd_get_active_buffer());
        common_ingame_overlay();
    }

    while (1)
    {
        wdog_refresh();

        bool drawFrame = common_emu_frame_loop();

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &blit);
        common_emu_input_loop_handle_turbo(&joystick);

        update_joystick(&joystick);

        prosystem_ExecuteFrame(keyboard_data);

        if (drawFrame) {
            blit();
            lcd_swap();
        }

        sound_store();

        common_emu_sound_sync(false);
    }

    return 0;
}
