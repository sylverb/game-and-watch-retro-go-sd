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

#include "gw_lcd.h"

/* Called from inside LakeSnes's snes_runFrame loop every ~4096 opcodes
 * to keep the WWDG (≈237 ms window on this build) happy. Without this
 * the first frame of pure-interpreter execution easily times out. */
void ff4_port_wdog_refresh(void) {
    wdog_refresh();
}

#define FF4_AUDIO_SAMPLE_RATE 32000

int app_main_ff4(uint8_t load_state, uint8_t start_paused, int8_t save_slot) {
    (void)load_state;
    (void)start_paused;
    (void)save_slot;

    printf("FF4 start (Phase 5.4 proof-of-life)\n");

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

    odroid_gamepad_state_t joystick = {0};
    int frame = 0;
    uint32_t t_start = HAL_GetTick();
    while (true) {
        wdog_refresh();

        odroid_input_read_gamepad(&joystick);

        /* Exit combo: SELECT+START returns to the launcher. */
        if (joystick.values[ODROID_INPUT_SELECT]
            && joystick.values[ODROID_INPUT_START]) {
            printf("FF4: exit requested at frame %d\n", frame);
            break;
        }

        ff4_step();
        frame++;

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
