// FF4 native C port — Phase 5.4 proof-of-life.
//
// Drives the LakeSnes shadow-execution core. At this stage the goal is
// only to confirm that LakeSnes initialises and runs frames on the
// STM32H7B0 with a real ROM loaded — no PPU/LCD wiring, no hybrid
// native-C/asm dispatcher yet. The 88 translated battle/ routines are
// already compiled into the overlay, just not called yet.

#include <stddef.h>
#include <stdbool.h>
#include "snes/snes.h"

Snes *ff4_snes = NULL;

bool ff4_init(const uint8_t *rom_bytes, int rom_length) {
    ff4_snes = snes_init();
    if (ff4_snes == NULL) {
        return false;
    }
    if (!snes_loadRom(ff4_snes, rom_bytes, rom_length)) {
        snes_free(ff4_snes);
        ff4_snes = NULL;
        return false;
    }
    return true;
}

void ff4_step(void) {
    if (ff4_snes != NULL) {
        snes_runFrame(ff4_snes);
    }
}

void ff4_set_button(int player, int button, bool pressed) {
    if (ff4_snes != NULL) {
        snes_setButtonState(ff4_snes, player, button, pressed);
    }
}

void ff4_shutdown(void) {
    if (ff4_snes != NULL) {
        snes_free(ff4_snes);
        ff4_snes = NULL;
    }
}

#include <stdint.h>
void ff4_get_state(uint32_t *frames_out, uint64_t *cycles_out) {
    if (ff4_snes == NULL) {
        if (frames_out) *frames_out = 0;
        if (cycles_out) *cycles_out = 0;
        return;
    }
    if (frames_out) *frames_out = ff4_snes->frames;
    if (cycles_out) *cycles_out = ff4_snes->cycles;
}
