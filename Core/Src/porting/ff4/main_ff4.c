// FF4 retro-go porting glue — Phase 5.4 proof-of-life.
//
// Wires the LakeSnes core to the real device:
//   1. Cache /roms/homebrew/ff4.sfc into the round-robin flash region
//      (odroid_overlay_cache_file_in_flash).
//   2. Initialise LakeSnes with the cached ROM bytes (ff4_init).
//   3. Spin a frame loop that advances LakeSnes one frame at a time
//      (ff4_step) while reading the gamepad. No LCD swap and no audio
//      yet — the goal of this phase is to confirm LakeSnes runs on
//      the STM32H7B0 without crashing. Press SELECT+START to exit
//      back to the launcher.

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <odroid_system.h>
#include "appid.h"
#include "main_ff4.h"
#include "main.h"
#include "gw_buttons.h"

extern bool ff4_init(const uint8_t *rom_bytes, int rom_length);
extern void ff4_step(void);
extern void ff4_shutdown(void);
extern void ff4_get_state(uint32_t *frames_out, uint64_t *cycles_out);
extern void ff4_blit_to_lcd(uint16_t *lcd_fb);
extern void ff4_set_button(int player, int button, bool pressed);

#if defined(FF4_AUTOBOOT) || defined(FF4_LOAD_SAVESTATE)
#include <string.h>
#include "snes/snes.h"
#include "snes/cpu.h"
#include "snes/ppu.h"
#include "snes/apu.h"
extern Snes *ff4_snes;
#endif

#ifdef FF4_AUTOBOOT
/* D3 + D4 + D5 shared state. D1/D2 are stateless. */
static uint32_t g_diag_host_frame = 0;
static uint32_t g_diag_pc_bank_hist[256];
static uint32_t g_diag_pc_sample_count = 0;
/* miss ring written by ff4_dispatch_try (see dispatch_all.c patch);
 * we only read it here. */
extern uint32_t g_diag_miss_ring[8];
extern uint32_t ff4_dispatch_hits;
extern uint32_t ff4_dispatch_misses;
#endif

/* SNES joypad bit order (matches LakeSnes input_read serial shift). */
#define SNES_BTN_B      0
#define SNES_BTN_Y      1
#define SNES_BTN_SELECT 2
#define SNES_BTN_START  3
#define SNES_BTN_UP     4
#define SNES_BTN_DOWN   5
#define SNES_BTN_LEFT   6
#define SNES_BTN_RIGHT  7
#define SNES_BTN_A      8
#define SNES_BTN_X      9
#define SNES_BTN_L      10
#define SNES_BTN_R      11

static void ff4_pump_buttons(const odroid_gamepad_state_t *js) {
    ff4_set_button(1, SNES_BTN_UP,     js->values[ODROID_INPUT_UP]);
    ff4_set_button(1, SNES_BTN_DOWN,   js->values[ODROID_INPUT_DOWN]);
    ff4_set_button(1, SNES_BTN_LEFT,   js->values[ODROID_INPUT_LEFT]);
    ff4_set_button(1, SNES_BTN_RIGHT,  js->values[ODROID_INPUT_RIGHT]);
    ff4_set_button(1, SNES_BTN_A,      js->values[ODROID_INPUT_A]);
    ff4_set_button(1, SNES_BTN_B,      js->values[ODROID_INPUT_B]);
    ff4_set_button(1, SNES_BTN_X,      js->values[ODROID_INPUT_X]);
    ff4_set_button(1, SNES_BTN_Y,      js->values[ODROID_INPUT_Y]);
    ff4_set_button(1, SNES_BTN_SELECT, js->values[ODROID_INPUT_SELECT]);
    ff4_set_button(1, SNES_BTN_START,  js->values[ODROID_INPUT_START]);
}

#include "gw_lcd.h"
#include "gw_audio.h"
#include "porting/common.h"

/* Called from inside LakeSnes's snes_runFrame loop every ~4096 opcodes
 * to keep the WWDG (≈237 ms window on this build) happy. Without this
 * the first frame of pure-interpreter execution easily times out. */
void ff4_port_wdog_refresh(void) {
    wdog_refresh();
}

/* 48 kHz / 60 fps = 800 mono samples per frame. LakeSnes' dsp_getSamples
 * resamples from the SPC700's native 534 (NTSC) samples/frame to any
 * requested count, so we ask directly for 800. Stereo is generated in
 * the scratch buffer below, then downmixed to mono into the SAI DMA
 * half-buffer before submission. */
#define FF4_AUDIO_SAMPLE_RATE   48000
#define FF4_AUDIO_FRAME_SAMPLES 800

static int16_t ff4_audio_stereo_scratch[FF4_AUDIO_FRAME_SAMPLES * 2];

extern Snes *ff4_snes;
extern void snes_setSamples(Snes *snes, int16_t *sampleData, int samplesPerFrame);

static void ff4_sound_submit(void) {
    if (common_emu_sound_loop_is_muted()) return;

    int16_t *dma_buf  = audio_get_active_buffer();
    uint16_t dma_len  = audio_get_buffer_length();
    int16_t  vol      = common_emu_sound_get_volume();

    /* Downmix stereo → mono with volume scaling. The LakeSnes DSP stores
     * L, R interleaved at samplesPerFrame * 2 int16. */
    uint16_t n = dma_len < FF4_AUDIO_FRAME_SAMPLES ? dma_len : FF4_AUDIO_FRAME_SAMPLES;
    for (uint16_t i = 0; i < n; i++) {
        int32_t mono = (int32_t)ff4_audio_stereo_scratch[i * 2]
                     + (int32_t)ff4_audio_stereo_scratch[i * 2 + 1];
        mono >>= 1;
        dma_buf[i] = (int16_t)((mono * vol) >> 8);
    }
    /* Pad any remainder with silence so the SAI doesn't repeat stale data. */
    for (uint16_t i = n; i < dma_len; i++) dma_buf[i] = 0;
}

int app_main_ff4(uint8_t load_state, uint8_t start_paused, int8_t save_slot) {
    (void)load_state;
    (void)start_paused;
    (void)save_slot;

    printf("FF4 start (Phase 5.4 proof-of-life)\n");
    printf("=== FF4_BOOT_MARKER_2026_06_13_AUTOTEST ===\n");

    odroid_system_init(APPID_FF4, FF4_AUDIO_SAMPLE_RATE);

    /* Cache the ROM into the round-robin flash region. The pointer
     * returned is XIP-addressable for the lifetime of this app.
     *
     * NOTE: "Final Fantasy IV.bin" is the FF4 overlay code (extracted
     * via objcopy --only-section=.overlay_ff4 in SD_CONTENT_STAMP).
     * The actual SNES ROM lives at a separate path so the menu entry
     * and the data are decoupled — mirrors the zelda3 / zelda3.ro
     * convention. User drops the ROM at /roms/homebrew/ff4.sfc. */
    uint32_t rom_length = 0;
    uint8_t *rom_bytes = odroid_overlay_cache_file_in_flash(
        "/roms/homebrew/ff4.sfc", &rom_length, false);
    if (rom_bytes == NULL || rom_length == 0) {
        printf("FF4: missing /roms/homebrew/ff4.sfc\n");
        return -1;
    }
    printf("FF4: rom cached at %p, %lu bytes\n",
           (void *)rom_bytes, (unsigned long)rom_length);

    if (!ff4_init(rom_bytes, (int)rom_length)) {
        printf("FF4: LakeSnes init failed\n");
        return -1;
    }

#ifdef FF4_LOAD_SAVESTATE
    {
        uint32_t st_size = 0;
        uint8_t *st_bytes = odroid_overlay_cache_file_in_flash(
            "/roms/homebrew/Final Fantasy IV.lss", &st_size, false);
        printf("=== FF4_SAVESTATE_TRY === size=%lu\n",
               (unsigned long)st_size);
        if (st_bytes == NULL || st_size == 0) {
            printf("=== FF4_SAVESTATE_MISSING ===\n");
        } else {
            bool ok = snes_loadState(ff4_snes, st_bytes, (int)st_size);
            printf("=== FF4_SAVESTATE_%s === pc=%02X:%04X frames=%lu\n",
                   ok ? "OK" : "FAIL",
                   ff4_snes->cpu->k, ff4_snes->cpu->pc,
                   (unsigned long)ff4_snes->frames);

            printf("=== FF4_WRAM_AT_LOAD === nmi$0200:");
            for (int i = 0; i < 16; i++) printf(" %02X", ff4_snes->ram[0x0200 + i]);
            printf("\n");

#ifdef FF4_PC_FIXUP
            /* Force PC to a known-good entry point. The savestate captures
             * pc=$00:3302 which falls in the PPU/APU register zone ($2000-
             * $3FFF) and is not valid executable code on G&W. Jump to
             * FieldMain @ $00:80A0 (which enables NMI, waits a frame, and
             * enters the main game loop). WRAM/VRAM from the savestate are
             * preserved, but field state ($79/$7A/$7B counters) gets reset
             * by FieldMain's prologue. */
            ff4_snes->cpu->k = 0x00;
            ff4_snes->cpu->pc = 0x80A0;
            ff4_snes->cpu->e = false;  /* native mode */
            ff4_snes->cpu->i = true;   /* IRQs masked at entry (cli later) */
            ff4_snes->cpu->mf = true;  /* A 8-bit */
            ff4_snes->cpu->xf = true;  /* X/Y 8-bit */
            ff4_snes->cpu->dp = 0x0000;
            ff4_snes->cpu->db = 0x00;
            ff4_snes->cpu->sp = 0x01FF;
            printf("=== FF4_PC_FIXUP === pc forced to 00:80A0 (FieldMain)\n");
#endif

#ifdef FF4_APU_ECHO
            /* Post-load APU mailbox unstuck: the saved state captures the
             * SPC handshake mid-conversation (FF4 audio engine polling).
             * On G&W the SPC700 is stubbed (InitSound_ext/ExecSound_ext
             * no-ops), so outPorts stay at zero forever and the CPU spins
             * waiting for an echo. Mirror inPorts → outPorts at load time
             * so any "wait until $2140 == 0xXX" loop sees its echo. */
            printf("=== FF4_APU_FIXUP === in=%02X %02X %02X %02X "
                   "out_before=%02X %02X %02X %02X\n",
                   ff4_snes->apu->inPorts[0], ff4_snes->apu->inPorts[1],
                   ff4_snes->apu->inPorts[2], ff4_snes->apu->inPorts[3],
                   ff4_snes->apu->outPorts[0], ff4_snes->apu->outPorts[1],
                   ff4_snes->apu->outPorts[2], ff4_snes->apu->outPorts[3]);
            for (int i = 0; i < 4; i++) {
                ff4_snes->apu->outPorts[i] = ff4_snes->apu->inPorts[i];
            }
#endif
        }
    }
#endif

    odroid_gamepad_state_t joystick = {0};
    int frame = 0;
    uint32_t t_start = HAL_GetTick();

    /* Start the SAI DMA loop with a length matching one frame worth of
     * mono samples. The DMA half-buffer callbacks toggle dma_state and
     * audio_get_active_buffer() returns the half we may safely write. */
    memset(ff4_audio_stereo_scratch, 0, sizeof(ff4_audio_stereo_scratch));
    audio_start_playing(FF4_AUDIO_FRAME_SAMPLES);

    while (true) {
        wdog_refresh();

        odroid_input_read_gamepad(&joystick);

        /* Exit combo: SELECT+START returns to the launcher. */
        if (joystick.values[ODROID_INPUT_SELECT]
            && joystick.values[ODROID_INPUT_START]) {
            printf("FF4: exit requested at frame %d\n", frame);
            break;
        }

        ff4_pump_buttons(&joystick);

#ifdef FF4_AUTOBOOT
        /* D3: sample PC bank once per host frame (cheap: 1 load + 1 incr) */
        if (ff4_snes != NULL && ff4_snes->cpu != NULL) {
            uint8_t bank = ff4_snes->cpu->k;
            g_diag_pc_bank_hist[bank]++;
            if (++g_diag_pc_sample_count >= 250) {
                uint32_t other = g_diag_pc_sample_count
                    - g_diag_pc_bank_hist[0x00] - g_diag_pc_bank_hist[0x01]
                    - g_diag_pc_bank_hist[0x04] - g_diag_pc_bank_hist[0x7E];
                printf("=== FF4_DIAG_PCHIST_2026_06_13 === host=%lu "
                       "$00=%lu $01=%lu $04=%lu $7E=%lu other=%lu last_pc=%02X:%04X\n",
                       (unsigned long)g_diag_host_frame,
                       (unsigned long)g_diag_pc_bank_hist[0x00],
                       (unsigned long)g_diag_pc_bank_hist[0x01],
                       (unsigned long)g_diag_pc_bank_hist[0x04],
                       (unsigned long)g_diag_pc_bank_hist[0x7E],
                       (unsigned long)other,
                       bank, ff4_snes->cpu->pc);
                memset(g_diag_pc_bank_hist, 0, sizeof g_diag_pc_bank_hist);
                g_diag_pc_sample_count = 0;
            }
        }

        /* D1: PPU/NMI state every 50 frames */
        if (ff4_snes != NULL && ff4_snes->ppu != NULL
            && (g_diag_host_frame % 50) == 0) {
            printf("=== FF4_DIAG_PPU_2026_06_13 === host=%lu snes_frames=%lu nmiEn=%d "
                   "inVbl=%d forceBlank=%d bright=%u vPos=%u\n",
                   (unsigned long)g_diag_host_frame,
                   (unsigned long)ff4_snes->frames,
                   ff4_snes->nmiEnabled, ff4_snes->inVblank,
                   ff4_snes->ppu->forcedBlank, ff4_snes->ppu->brightness,
                   ff4_snes->vPos);
        }

        /* D2: APU mailbox every 50 frames (offset 25 to interleave with D1) */
        if (ff4_snes != NULL && ff4_snes->apu != NULL
            && (g_diag_host_frame % 50) == 25) {
            printf("=== FF4_DIAG_APU_2026_06_13 === host=%lu "
                   "in=%02X %02X %02X %02X out=%02X %02X %02X %02X\n",
                   (unsigned long)g_diag_host_frame,
                   ff4_snes->apu->inPorts[0],  ff4_snes->apu->inPorts[1],
                   ff4_snes->apu->inPorts[2],  ff4_snes->apu->inPorts[3],
                   ff4_snes->apu->outPorts[0], ff4_snes->apu->outPorts[1],
                   ff4_snes->apu->outPorts[2], ff4_snes->apu->outPorts[3]);
        }

        /* D5: heartbeat every 60 frames (~1 Hz). Split 64-bit cycle counter
         * into two %lx halves since nano-printf lacks %llu. */
        if (ff4_snes != NULL && ff4_snes->cpu != NULL
            && (g_diag_host_frame % 60) == 0) {
            uint64_t cyc = ff4_snes->cycles;
            printf("=== FF4_DIAG_ALIVE_2026_06_13 === host=%lu "
                   "snes_cyc_hi=%08lx snes_cyc_lo=%08lx snes_frm=%lu pc=%02X:%04X\n",
                   (unsigned long)g_diag_host_frame,
                   (unsigned long)(cyc >> 32),
                   (unsigned long)(cyc & 0xFFFFFFFFu),
                   (unsigned long)ff4_snes->frames,
                   ff4_snes->cpu->k, ff4_snes->cpu->pc);
            printf("=== FF4_WRAM_NMI_2026_06_13 === host=%lu wram$0200:",
                   (unsigned long)g_diag_host_frame);
            for (int i = 0; i < 16; i++)
                printf(" %02X", ff4_snes->ram[0x0200 + i]);
            printf("\n");
        }

        /* D4: dispatch miss ring dump every 100 frames */
        if ((g_diag_host_frame % 100) == 0 && g_diag_host_frame > 0) {
            printf("=== FF4_DIAG_MISS_2026_06_13 === host=%lu hits=%lu misses=%lu uniq=",
                   (unsigned long)g_diag_host_frame,
                   (unsigned long)ff4_dispatch_hits,
                   (unsigned long)ff4_dispatch_misses);
            for (int i = 0; i < 8; i++) {
                printf("%06lX ", (unsigned long)g_diag_miss_ring[i]);
            }
            printf("\n");
        }

        g_diag_host_frame++;
#endif

#ifdef FF4_APU_ECHO
        /* Continuous APU mailbox echo: SPC700 is stubbed on G&W so it
         * never acknowledges. Mirror inPorts → outPorts each host frame
         * so the FF4 audio engine's "wait until $2140==X" loops see X
         * the next time they poll. */
        if (ff4_snes != NULL && ff4_snes->apu != NULL) {
            for (int i = 0; i < 4; i++) {
                ff4_snes->apu->outPorts[i] = ff4_snes->apu->inPorts[i];
            }
        }
#endif

#ifdef FF4_FORCE_NMI
        /* Hypothesis test: the post-savestate spin loop may wait on a
         * WRAM counter that only the NMI handler updates. The FF4 code
         * disabled nmiEnabled after a few frames because the SPC didn't
         * respond as expected. Re-enable NMI each frame so the handler
         * fires at VBlank and updates whatever counter the spin loop is
         * waiting on. */
        if (ff4_snes != NULL) {
            ff4_snes->nmiEnabled = true;
        }
#endif

#ifdef FF4_FORCE_DISPLAY_ON
        /* Diagnostic: force PPU display ON. If the savestate's VRAM/OAM/
         * CGRAM are still valid and the game code just hasn't gotten to
         * its fade-in yet, this will reveal whatever was supposed to be
         * on screen at savestate time. */
        if (ff4_snes != NULL && ff4_snes->ppu != NULL) {
            ff4_snes->ppu->forcedBlank = false;
            ff4_snes->ppu->brightness = 15;
        }
#endif

        ff4_step();
        frame++;

        /* Pull one frame worth of SPC/DSP samples (resampled to 48 kHz
         * by dsp_getSamples) and copy into the active SAI half-buffer
         * with mono downmix + volume scaling. */
        if (ff4_snes != NULL && ff4_snes->apu != NULL) {
            snes_setSamples(ff4_snes,
                            ff4_audio_stereo_scratch,
                            FF4_AUDIO_FRAME_SAMPLES);
            ff4_sound_submit();
        }

        /* Blit the PPU frame to the active LCD buffer, then swap. */
        ff4_blit_to_lcd((uint16_t *)lcd_get_active_buffer());
        lcd_swap();

        /* Liveness probe: every 5 host frames print Snes-side counters
         * so we can tell whether the interpreter is actually progressing
         * or just spinning. Remove once Phase 5.5/5.6 land real signal. */
        if ((frame % 5) == 0) {
            uint32_t snes_frames = 0;
            uint64_t snes_cycles = 0;
            ff4_get_state(&snes_frames, &snes_cycles);
            extern uint32_t ff4_dispatch_hits;
            extern uint32_t ff4_dispatch_misses;
            extern uint32_t ff4_miss_per_bank[256];
            uint32_t now = HAL_GetTick();
            printf("FF4 live: host=%d snes_frame=%lu dispatch=%lu/%lu wall_ms=%lu\n",
                   frame,
                   (unsigned long)snes_frames,
                   (unsigned long)ff4_dispatch_hits,
                   (unsigned long)(ff4_dispatch_hits + ff4_dispatch_misses),
                   (unsigned long)(now - t_start));
            /* Every 50 host frames, print the top-3 banks by miss count
             * so we know which module the boot/title sequence lives in. */
            if ((frame % 50) == 0) {
                int top[3] = {-1, -1, -1};
                uint32_t topv[3] = {0, 0, 0};
                for (int b = 0; b < 256; b++) {
                    uint32_t v = ff4_miss_per_bank[b];
                    if (v > topv[0]) { topv[2]=topv[1]; top[2]=top[1]; topv[1]=topv[0]; top[1]=top[0]; topv[0]=v; top[0]=b; }
                    else if (v > topv[1]) { topv[2]=topv[1]; top[2]=top[1]; topv[1]=v; top[1]=b; }
                    else if (v > topv[2]) { topv[2]=v; top[2]=b; }
                }
                printf("FF4 banks: ");
                for (int i = 0; i < 3; i++) {
                    if (top[i] < 0) break;
                    printf("$%02X=%lu ", top[i], (unsigned long)topv[i]);
                }
                printf("\n");
            }
        }
    }

    ff4_shutdown();
    return 0;
}
