#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// This function checks if only one monster remains ($29cd == 1),
// and if so, increments $de.
static void AICond_0b_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint8_t count = ram[0x29CD];  // load monster count
    if (count == 1) {             // cmp #$01 / bne @bf0e
        ram[0xDE]++;              // inc $de
    }
}

// PITFALLS: 1 (DB=$7E assumed for correct RAM access)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  0x29CD=1
//   output_ram:  0xDE=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AICond_0b ($BF:05)