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
    }

    ff4_shutdown();
    return 0;
}
