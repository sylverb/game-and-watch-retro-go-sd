#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// This function sets up fixed values in RAM and jumps to RandAITarget.
// It behaves as a thunk that initializes $af, $b0, and $ad before delegation.
static void AITarget_22_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xB0] = 0x0C;  // lda #$0c / sta $b0
    ram[0xAF] = 0x00;  // lda #$00 / sta $af
    ram[0xAD] = 0xFF;  // dec (from 0) / sta $ad → 0xFF
    RandAITarget_emu(snes);  // jmp RandAITarget
}

// PITFALLS: 1 (DB must be $7E for correct RAM writes)
// HELPERS: RandAITarget_emu(snes) — delegates RandAITarget @ $BA:9C
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AITarget_22 ($BA:8E)