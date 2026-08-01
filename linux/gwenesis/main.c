/*
 * Linux desktop harness for Gwenesis / Mega Drive core (SDL2 + gdb).
 * Usage:
 *   ./retro-go-gwenesis.elf <rom.bin>              (ROM depuis fichier)
 *   ./retro-go-gwenesis.elf                        (si compilé avec loaded_gwenesis_rom.c)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include <stdbool.h>
#include <stdint.h>

#include "common_linux.h"
#include "gw_audio.h"
#include "odroid_audio.h"
#include "odroid_input.h"

#include "gwenesis_linux.h"
#include "gwenesis_sdl.h"

#include "gw_lcd.h"
#include "rom_manager.h"
#include "rg_i18n.h"

#include "gwenesis_bus.h"
#include "gwenesis_io.h"
#include "gwenesis_sram.h"
#include "gwenesis_sn76489.h"
#include "gwenesis_vdp.h"
#include "gwenesis_savestate.h"
#include "m68k.h"
#include "ym2612.h"
#include "z80inst.h"

/* Savestate request flags set by the SDL input handler (odroid_input.c). */
extern int linux_savestate_req;
extern int linux_loadstate_req;

unsigned char *gwenesis_linux_rom;
size_t gwenesis_linux_rom_size;
size_t gwenesis_linux_rom_storage_size;
char gwenesis_linux_rom_path[512];

/* Gwenesis core expects these symbols when built with TARGET_GNW. */
#ifdef TARGET_GNW
const unsigned char *ROM_DATA = NULL;
unsigned int ROM_DATA_LENGTH = 0;
#endif

void rg_i18n_linux_set_lang(void);

#define SCALE 2

extern const unsigned char gwenesis_embedded_rom[];
extern const uint32_t gwenesis_embedded_rom_size;
extern const char gwenesis_embedded_rom_source[];

static odroid_gamepad_state_t joystick;

int16_t gwenesis_ym2612_buffer[GWENESIS_AUDIO_BUFFER_CAPACITY];
int ym2612_index;
int ym2612_clock;

int16_t gwenesis_sn76489_buffer[GWENESIS_AUDIO_BUFFER_CAPACITY];
int sn76489_index;
int sn76489_clock;

int system_clock;
unsigned int lines_per_frame = LINES_PER_FRAME_NTSC;
unsigned int scan_line;
unsigned int drawFrame = 1;
static int hori_screen_offset, vert_screen_offset;

static unsigned int gwenesis_audio_freq;
static unsigned int gwenesis_audio_buffer_lenght;
static unsigned int gwenesis_refresh_rate;

static int gwenesis_lpfilter = 0;

/* GPGX-aligned H-INT counter (persists across frames, reloaded on last vblank line). */
static int hint_counter = 0xff;
static int skip_first_vint = 1;

#define VINT_H32_CYCLES 770u
#define VINT_H40_CYCLES 788u

static inline void gwenesis_run_cpus_to(unsigned target)
{
  m68k_run((int)target);
  z80_run((int)target);
  if (GWENESIS_AUDIO_ACCURATE == 0) {
    gwenesis_SN76489_run((int)target);
    ym2612_run((int)target);
  }
}

extern unsigned char gwenesis_vdp_regs[0x20];
extern unsigned short gwenesis_vdp_status;
extern int hint_pending;
extern unsigned int screen_width, screen_height;
extern int mode_pal;

static void gwenesis_system_init(void);
static void gwenesis_sound_start(void);
static void gwenesis_sound_submit(void);
static void run_gwenesis_emulation(void);

void gwenesis_io_get_buttons(void);

#ifdef TARGET_GNW
/* Desktop (linux) memory allocators for the Gwenesis "GNW" target. */
void ahb_init(void) {}
void itc_init(void) {}

void *ahb_malloc(size_t size) { return malloc(size); }
void *ahb_calloc(size_t count, size_t size) { return calloc(count, size); }
void *itc_malloc(size_t size) { return malloc(size); }
void *itc_calloc(size_t count, size_t size) { return calloc(count, size); }
void *ram_malloc(size_t size) { return malloc(size); }
void *ram_calloc(size_t count, size_t size) { return calloc(count, size); }
#endif

static int load_embedded_rom(void)
{
    gwenesis_linux_rom = (unsigned char *)gwenesis_embedded_rom;
    gwenesis_linux_rom_size = (size_t)gwenesis_embedded_rom_size;
    gwenesis_linux_rom_storage_size = gwenesis_linux_rom_size;
    ROM_DATA = (const unsigned char *)gwenesis_embedded_rom;
    ROM_DATA_LENGTH = (unsigned int)gwenesis_embedded_rom_size;
    snprintf(gwenesis_linux_rom_path, sizeof(gwenesis_linux_rom_path), "./%s",
             gwenesis_embedded_rom_source);

    memset(&linux_active_file, 0, sizeof(linux_active_file));
    strncpy(linux_active_file.path, gwenesis_linux_rom_path, sizeof(linux_active_file.path) - 1);
    linux_active_file.size = (uint32_t)gwenesis_linux_rom_size;
    ACTIVE_FILE = &linux_active_file;

    return 0;
}

void gwenesis_io_get_buttons(void)
{
    /* Map desktop keys to the emulated Gwenesis pad bits.
     * Porting version has some board/game-specific shortcut logic; for
     * Linux we keep a straightforward mapping. */
    button_state[0] =
        (joystick.values[ODROID_INPUT_UP] << PAD_UP) |
        (joystick.values[ODROID_INPUT_DOWN] << PAD_DOWN) |
        (joystick.values[ODROID_INPUT_LEFT] << PAD_LEFT) |
        (joystick.values[ODROID_INPUT_RIGHT] << PAD_RIGHT) |
        (joystick.values[ODROID_INPUT_B] << PAD_B) |
        (joystick.values[ODROID_INPUT_X] << PAD_C) |
        (joystick.values[ODROID_INPUT_A] << PAD_A) |
        (joystick.values[ODROID_INPUT_START] << PAD_S);

    /* Core expects inverted (active-low) button_state bits. */
    button_state[0] = (unsigned char)~button_state[0];
}

static void gwenesis_system_init(void)
{
    /* Select NTSC or PAL timing based on ROM region (set by load_cartridge). */
    extern int mode_pal;
    if (mode_pal) {
        gwenesis_audio_freq = GWENESIS_AUDIO_FREQ_PAL;
        gwenesis_audio_buffer_lenght = GWENESIS_AUDIO_BUFFER_LENGTH_PAL;
        gwenesis_refresh_rate = GWENESIS_REFRESH_RATE_PAL;
    } else {
        gwenesis_audio_freq = GWENESIS_AUDIO_FREQ_NTSC;
        gwenesis_audio_buffer_lenght = GWENESIS_AUDIO_BUFFER_LENGTH_NTSC;
        gwenesis_refresh_rate = GWENESIS_REFRESH_RATE_NTSC;
    }

    memset(gwenesis_sn76489_buffer, 0, sizeof(gwenesis_sn76489_buffer));
    memset(gwenesis_ym2612_buffer, 0, sizeof(gwenesis_ym2612_buffer));

    odroid_audio_init((int)gwenesis_audio_freq);
    lcd_set_refresh_rate(gwenesis_refresh_rate);
}

static void gwenesis_sound_start(void)
{
    audio_start_playing((uint16_t)gwenesis_audio_buffer_lenght);
}

static void gwenesis_sound_submit(void)
{
    if (common_emu_sound_loop_is_muted())
        return;

    int16_t factor = (int16_t)common_emu_sound_get_volume();
    int16_t *sound_buffer = audio_get_active_buffer();
    uint16_t sound_buffer_length = audio_get_buffer_length();

    /* Optional low-pass filter (disabled by default). */
    const uint32_t factora = 0x1000;
    const uint32_t factorb = 0x10000 - factora;
    static int16_t gwenesis_audio_out = 0;

    if (gwenesis_lpfilter) {
        for (uint16_t i = 0; i < sound_buffer_length; i++) {
            int32_t tmp = (int32_t)gwenesis_audio_out * (int32_t)factorb +
                           (int32_t)(gwenesis_ym2612_buffer[i] +
                                    gwenesis_sn76489_buffer[i]) *
                               (int32_t)factora;
            gwenesis_audio_out = (int16_t)(tmp >> 16);
            sound_buffer[i] = (int16_t)((int32_t)gwenesis_audio_out * (int32_t)factor / 128);
        }
    } else {
        for (uint16_t i = 0; i < sound_buffer_length; i++) {
            int32_t mixed = (int32_t)gwenesis_ym2612_buffer[i] +
                             (int32_t)gwenesis_sn76489_buffer[i];
            sound_buffer[i] = (int16_t)(mixed * factor / 512);
        }
    }

    odroid_audio_submit(sound_buffer, sound_buffer_length);
}

static void gwenesis_state_path(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s.state", gwenesis_linux_rom_path);
}

static void gwenesis_linux_save_state(void)
{
    char path[1024];
    gwenesis_state_path(path, sizeof(path));
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "GWENESIS: cannot open '%s' for save\n", path);
        return;
    }
    gwenesis_savestate_write_file_header(file);
    gwenesis_save_state(file);
    fclose(file);
    fprintf(stderr, "GWENESIS: state saved to '%s'\n", path);
}

static void gwenesis_linux_load_state(void)
{
    char path[1024];
    gwenesis_state_path(path, sizeof(path));
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "GWENESIS: cannot open '%s' for load\n", path);
        return;
    }
    unsigned char header[GWENESIS_SAVESTATE_HEADER_SIZE];
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        fprintf(stderr, "GWENESIS: '%s' truncated header\n", path);
        fclose(file);
        return;
    }
    int ss_version = gwenesis_savestate_version_from_header(header);
    gwenesis_load_state(file, ss_version);
    fclose(file);
    fprintf(stderr, "GWENESIS: state loaded from '%s' (version %d)\n", path, ss_version);
}

static void run_gwenesis_emulation(void)
{
    printf("Genesis start\n");

#ifdef TARGET_GNW
    load_cartridge();
#else
    load_cartridge(gwenesis_linux_rom, gwenesis_linux_rom_size);
#endif
    gwenesis_system_init();

    power_on();
    reset_emulation();
    hint_counter = 0xff;
    skip_first_vint = 1;

    if (gwenesis_sram_enabled) {
        char sram_path[1024];
        snprintf(sram_path, sizeof(sram_path), "%s.sram", gwenesis_linux_rom_path);
        gwenesis_set_sram_path(sram_path);
        gwenesis_sram_load();
    }

    unsigned short *screen = (unsigned short *)lcd_get_active_buffer();
    gwenesis_vdp_set_buffer(&screen[0]);

    lcd_wait_for_vblank();
    gwenesis_sound_start();

    {
        const char *al = getenv("GWAUTOLOAD");
        if (al && al[0] && al[0] != '0')
            linux_loadstate_req = 1;
    }

    for (;;) {
        odroid_input_read_gamepad(&joystick);

        if (linux_savestate_req) {
            linux_savestate_req = 0;
            gwenesis_linux_save_state();
        }
        if (linux_loadstate_req) {
            linux_loadstate_req = 0;
            gwenesis_linux_load_state();
        }

        common_emu_clear_dwt_cycles();
        (void)common_emu_frame_loop();

        hint_pending = 0;

        screen_height = REG1_PAL ? 240 : 224;  /* game-selectable: 224 or 240 active lines */
        screen_width = REG12_MODE_H40 ? 320 : 256;
        lines_per_frame = mode_pal ? LINES_PER_FRAME_PAL : LINES_PER_FRAME_NTSC; /* hardware: 313 or 262 */
        vert_screen_offset = mode_pal ? 0 : 320 * (240 - 224) / 2;
        hori_screen_offset = 0;

        screen = (unsigned short *)lcd_get_active_buffer();
        gwenesis_vdp_set_buffer(&screen[vert_screen_offset + hori_screen_offset]);

        gwenesis_vdp_render_config();

        system_clock = 0;
        zclk = 0;
        ym2612_clock = 0;
        ym2612_index = 0;
        sn76489_clock = 0;
        sn76489_index = 0;
        scan_line = 0;

        /* Frame loop aligned with Genesis Plus GX system_frame_gen():
         * vblank-first scan order, VINT delay, H-INT on active lines only. */
        {
          const unsigned vint_cycles =
              REG12_MODE_H40 ? VINT_H40_CYCLES : VINT_H32_CYCLES;
          int line;

          gwenesis_vdp_status =
              (unsigned short)((gwenesis_vdp_status & (unsigned short)~0x0112u) |
                               STATUS_VBLANK);
          gwenesis_vdp_status ^= STATUS_ODDFRAME;

          /* First vblank line (=VINT), with optional delay before IRQ. */
          scan_line = (int)screen_height;
          if (!skip_first_vint) {
            gwenesis_run_cpus_to(system_clock + vint_cycles);
            gwenesis_vdp_status |= STATUS_VIRQPENDING;
            if (REG1_VBLANK_INTERRUPT != 0)
              m68k_set_irq(6);
            z80_irq_line(1);
          }
          gwenesis_run_cpus_to(system_clock + VDP_CYCLES_PER_LINE);
          system_clock += VDP_CYCLES_PER_LINE;
          z80_irq_line(0);

          /* Remaining vblank lines (no H-INT counter). */
          for (line = (int)screen_height + 1; line < (int)lines_per_frame - 1;
               line++) {
            scan_line = line;
            gwenesis_run_cpus_to(system_clock + VDP_CYCLES_PER_LINE);
            system_clock += VDP_CYCLES_PER_LINE;
          }

          /* Last vblank line: reload H-INT counter, clear VBLANK (GPGX). */
          scan_line = (int)lines_per_frame - 1;
          hint_counter = (int)REG10_LINE_COUNTER;
          gwenesis_vdp_status &= (unsigned short)~STATUS_VBLANK;
          gwenesis_run_cpus_to(system_clock + VDP_CYCLES_PER_LINE);
          system_clock += VDP_CYCLES_PER_LINE;

          /* Active display (H-INT before CPU run for each line). */
          for (line = 0; line < (int)screen_height; line++) {
            scan_line = line;
            gwenesis_vdp_latch_line_scroll(line);
            if (hint_counter == 0) {
              hint_counter = (int)REG10_LINE_COUNTER;
              hint_pending = 1;
              if (REG0_LINE_INTERRUPT)
                m68k_update_irq(4);
            } else {
              hint_counter--;
            }
            gwenesis_run_cpus_to(system_clock + VDP_CYCLES_PER_LINE);
            if (drawFrame)
              gwenesis_vdp_render_line(line);
            system_clock += VDP_CYCLES_PER_LINE;
          }

          skip_first_vint = 0;
        }

        if (GWENESIS_AUDIO_ACCURATE == 1) {
            gwenesis_SN76489_run(system_clock);
            ym2612_run(system_clock);
        }

        m68k.cycles -= system_clock;

        gwenesis_sound_submit();
        common_ingame_overlay();

        lcd_swap();
        common_emu_sound_sync(false);

        if (gwenesis_sram_enabled)
            gwenesis_sram_save();
    }
}

int main(int argc, char **argv)
{
    if (load_embedded_rom() != 0)
        return 1;

    rg_i18n_linux_set_lang();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "retro-go gwenesis (linux)",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        GW_LCD_WIDTH * SCALE, GW_LCD_HEIGHT * SCALE, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }

    gwenesis_sdl_set_video(window, renderer);
    lcd_init(NULL, NULL, LCD_INIT_CLEAR_BUFFERS);

    run_gwenesis_emulation();

    odroid_audio_terminate();
    SDL_Quit();
    return 0;
}
