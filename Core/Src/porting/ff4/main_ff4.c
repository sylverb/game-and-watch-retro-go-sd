// FF4 retro-go porting glue — Phase 5.3 scaffold.
// Entry point declared to retro-go via APPID_FF4 + the Homebrew branch
// in rg_emulators.c. Does NOT yet run the game: it initialises the
// system as an emulator slot, calls the LakeSnes core init, and
// returns to the launcher. Real gameplay needs the rom_manager / SRAM /
// input wiring plus the remaining translated modules (btlgfx/menu/...).

#include <stdint.h>
#include <stdio.h>

#include <odroid_system.h>
#include "appid.h"
#include "main_ff4.h"

extern void ff4_init(void);
extern void ff4_step(void);

#define FF4_AUDIO_SAMPLE_RATE 32000

int app_main_ff4(uint8_t load_state, uint8_t start_paused, int8_t save_slot) {
    (void)load_state;
    (void)start_paused;
    (void)save_slot;

    printf("FF4 start (Phase 5.3 scaffold)\n");

    odroid_system_init(APPID_FF4, FF4_AUDIO_SAMPLE_RATE);

    ff4_init();
    ff4_step();

    /* TODO: wire ROM loader, SRAM, input, frame loop. For now we return
     * to the launcher so the FF4 entry is selectable end-to-end without
     * hanging the device. */
    return 0;
}
