#include <odroid_system.h>
#include <string.h>
#include <assert.h>

#include "main.h"
#include "bilinear.h"
#include "gw_lcd.h"
#include "gw_linker.h"
#include "rg_i18n.h"
#include "gw_buttons.h"
#include "common.h"
#include "rom_manager.h"
#ifndef GNW_DISABLE_COMPRESSION
#include "lzma.h"
#endif
#include "appid.h"
#include "gw_malloc.h"

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

#define VIDEOPAC_BIOS_BUFF_LENGTH 1024
#define VIDEOPAC_ROM_BUFF_LENGTH 16*1024

// Use 60Hz for Videopac
#define AUDIO_SAMPLE_RATE_VIDEOPAC 63360
static bool low_pass_enabled  = true;
static int32_t low_pass_range = (60 * 0x10000) / 100;
static int32_t low_pass_prev  = 0;

static odroid_gamepad_state_t previous_joystick_state;

// Data used by O2EM
uint8_t soundBuffer[SOUND_BUFFER_LEN];

int RLOOP=0;
int joystick_data[2][5]={{0,0,0,0,0},{0,0,0,0,0}};
extern unsigned char key[256*2];
#define RETROK_RETURN 13

void update_joy(void)
{
}

// --- MAIN


static void blit() {

}

static bool SaveState(char *savePathName, char *sramPathName, int slot)
{
    return true;
}

static bool LoadState(char *savePathName, char *sramPathName, int slot)
{
    return true;
}

static void videopac_input_update(odroid_gamepad_state_t *joystick)
{
    if ((joystick->values[ODROID_INPUT_LEFT]) && !previous_joystick_state.values[ODROID_INPUT_LEFT])
    {
        joystick_data[0][2] = 1;
    }
    else if (!(joystick->values[ODROID_INPUT_LEFT]) && previous_joystick_state.values[ODROID_INPUT_LEFT])
    {
        joystick_data[0][2] = 0;
    }
    if ((joystick->values[ODROID_INPUT_RIGHT]) && !previous_joystick_state.values[ODROID_INPUT_RIGHT])
    {
        joystick_data[0][3] = 1;
    }
    else if (!(joystick->values[ODROID_INPUT_RIGHT]) && previous_joystick_state.values[ODROID_INPUT_RIGHT])
    {
        joystick_data[0][3] = 0;
    }
    if ((joystick->values[ODROID_INPUT_UP]) && !previous_joystick_state.values[ODROID_INPUT_UP])
    {
        joystick_data[0][0] = 1;
    }
    else if (!(joystick->values[ODROID_INPUT_UP]) && previous_joystick_state.values[ODROID_INPUT_UP])
    {
        joystick_data[0][0] = 0;
    }
    if ((joystick->values[ODROID_INPUT_DOWN]) && !previous_joystick_state.values[ODROID_INPUT_DOWN])
    {
        joystick_data[0][1] = 1;
    }
    else if (!(joystick->values[ODROID_INPUT_DOWN]) && previous_joystick_state.values[ODROID_INPUT_DOWN])
    {
        joystick_data[0][1] = 0;
    }
    if ((joystick->values[ODROID_INPUT_A]) && !previous_joystick_state.values[ODROID_INPUT_A])
    {
        joystick_data[0][4] = 1;
    }
    else if (!(joystick->values[ODROID_INPUT_A]) && previous_joystick_state.values[ODROID_INPUT_A])
    {
        joystick_data[0][4] = 0;
    }
    if ((joystick->values[ODROID_INPUT_B]) && !previous_joystick_state.values[ODROID_INPUT_B])
    {
        joystick_data[0][4] = 1;
    }
    else if (!(joystick->values[ODROID_INPUT_B]) && previous_joystick_state.values[ODROID_INPUT_B])
    {
        joystick_data[0][4] = 0;
    }
    // Game button on G&W
    if ((joystick->values[ODROID_INPUT_START]) && !previous_joystick_state.values[ODROID_INPUT_START])
    {
        key[RETROK_RETURN] = 1;
    }
    else if (!(joystick->values[ODROID_INPUT_START]) && previous_joystick_state.values[ODROID_INPUT_START])
    {
        key[RETROK_RETURN] = 0;
    }
    // Time button on G&W
    if ((joystick->values[ODROID_INPUT_SELECT]) && !previous_joystick_state.values[ODROID_INPUT_SELECT])
    {
    }
    else if (!(joystick->values[ODROID_INPUT_SELECT]) && previous_joystick_state.values[ODROID_INPUT_SELECT])
    {
    }
    // Start button on Zelda G&W
    if ((joystick->values[ODROID_INPUT_X]) && !previous_joystick_state.values[ODROID_INPUT_X])
    {
        key[RETROK_RETURN] = 1;
    }
    else if (!(joystick->values[ODROID_INPUT_X]) && previous_joystick_state.values[ODROID_INPUT_X])
    {
        key[RETROK_RETURN] = 0;
    }
    // Select button on Zelda G&W
    if ((joystick->values[ODROID_INPUT_Y]) && !previous_joystick_state.values[ODROID_INPUT_Y])
    {
    }
    else if (!(joystick->values[ODROID_INPUT_Y]) && previous_joystick_state.values[ODROID_INPUT_Y])
    {
    }

    memcpy(&previous_joystick_state, joystick, sizeof(odroid_gamepad_state_t));
}

static rg_app_desc_t * init(uint8_t load_state, int8_t save_slot)
{
    odroid_system_init(APPID_GB, AUDIO_SAMPLE_RATE_VIDEOPAC);
    odroid_system_emu_init(&LoadState, &SaveState, NULL, NULL, NULL, NULL, NULL);

    // Load ROM

    // Audio
    audio_start_playing(SOUND_BUFFER_LEN);

    rg_app_desc_t *app = odroid_system_get_app();

    if (load_state) {
        odroid_system_emu_load_state(save_slot);
    } else {
        lcd_clear_buffers();
    }

    return app;
}

static size_t videopac_getromdata(retro_emulator_file_t *rom_file, unsigned char **res_data, size_t max_size) {
    /* src pointer to the ROM data in the external flash (raw or lzma) */

#ifndef GNW_DISABLE_COMPRESSION
    if(strcmp(rom_file->ext, "lzma") == 0) {
        const unsigned char *src = rom_file->address;
        uint8_t *dest = itc_malloc(max_size);
        size_t n_decomp_bytes;
        n_decomp_bytes = lzma_inflate(dest, max_size, src, rom_file->size);
        *res_data = dest;
        return n_decomp_bytes;
    }
    else
#endif
    {
        *res_data = (unsigned char *)ROM_DATA;
        return ROM_DATA_LENGTH;
    }
}

static bool load_bios()
{
    retro_emulator_file_t *rom_file;
    uint8_t *bios_data;
    size_t bios_size;
    uint32_t crc;
    size_t i;

    rom_system_t *rom_system = (rom_system_t *)rom_manager_system(&rom_mgr, "Philips Vectrex");
    rom_file = (retro_emulator_file_t *)rom_manager_get_file((const rom_system_t *)rom_system,"bios.lzma");
    if (rom_file == NULL) {
        rom_file = (retro_emulator_file_t *)rom_manager_get_file((const rom_system_t *)rom_system,"bios.bin");
    }
    if (rom_file == NULL) {
        printf("[O2EM]: Error loading BIOS ROM.\n");
        return false;
    }

    bios_size = videopac_getromdata(rom_file, &bios_data,VIDEOPAC_BIOS_BUFF_LENGTH);

    if (bios_size != 1024)
    {
        printf("[O2EM]: Error loading BIOS ROM. bios_size : %d\n",bios_size);
        return false;
    }

    memcpy(rom_table[0],bios_data,1024);

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

#if 0
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
    else 
#endif
    if (((size % 3072) == 0))
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
    uint8_t *rom_data;
    size_t rom_size;
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
    if (!load_bios())
        return;
    rom_size = videopac_getromdata(ACTIVE_FILE, &rom_data, VIDEOPAC_ROM_BUFF_LENGTH);
    if (!load_cart(rom_data, rom_size))
        return;
}

static void pcm_submit() {
    size_t i;

    if (common_emu_sound_loop_is_muted()) {
        return;
    }

    int32_t factor = common_emu_sound_get_volume();
    int16_t* sound_buffer = audio_get_active_buffer();
    uint16_t sound_buffer_length = audio_get_buffer_length();

    uint8_t *audio_in_ptr  = soundBuffer;
    int16_t *audio_out_ptr = sound_buffer;

    /* Convert 8u mono to 16s stereo */
    if (low_pass_enabled)
    {
        /* Restore previous sample */
        int32_t low_pass = low_pass_prev;
        /* Single-pole low-pass filter (6 dB/octave) */
        int32_t factor_a = low_pass_range;
        int32_t factor_b = 0x10000 - factor_a;

        for(i = 1; i <= sound_buffer_length; i++)
        {
            int32_t sample16;

            /* Get current sample */
            sample16  = ((((*(audio_in_ptr++) * factor) /
                256) - 128) << 8) + 32768;

            /* Apply low-pass filter */
            low_pass = (low_pass * factor_a) + (sample16 * factor_b);

            /* 16.16 fixed point */
            low_pass >>= 16;

            /* Update output buffer */
            *(audio_out_ptr++) = (int16_t)low_pass;
        }

        /* Save last sample for next frame */
        low_pass_prev = low_pass;
    }
    else
    {
        for(i = 1; i <= sound_buffer_length; i++)
        {
            int32_t sample16;

            /* Get current sample */
            sample16  = ((((*(audio_in_ptr++) * factor) /
                256) - 128) << 8) + 32768;

            *(audio_out_ptr++) = (int16_t)sample16;
        }
    }
}

void gnw_videopack_blit(uint8_t *input, APALETTE *palette) {
    int i,j;
    unsigned char ind;
    uint16_t *outp = (uint16_t *)lcd_get_inactive_buffer();
    for(i=0;i<HEIGHT;i++)
    {
        for(j=0;j<WIDTH;j++)
        {
            ind=input[i*340 + j + 10];
            (*outp++) = RGB565(palette[ind].r, palette[ind].g, palette[ind].b);
        }
//		outp+=	WIDTH;
    }
    common_ingame_overlay();
}

void app_main_videopac(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    odroid_dialog_choice_t options[] = {
        ODROID_DIALOG_CHOICE_LAST
    };

    init(load_state, save_slot);
    odroid_gamepad_state_t joystick;

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }

    load_data();

    init_display();
    init_cpu();
    init_system();

    set_score(app_data.scoretype, app_data.scoreaddress, app_data.default_highscore);

    app_data.euro = 0;

    while (true)
    {
        wdog_refresh();

        bool drawFrame = common_emu_frame_loop();

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &blit);
        common_emu_input_loop_handle_turbo(&joystick);

        videopac_input_update(&joystick);

        RLOOP=1;
        cpu_exec();
        lcd_swap();
        pcm_submit();

        common_emu_sound_sync(false);
    }
}
