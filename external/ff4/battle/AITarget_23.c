#include "snes/snes.h"

// This function sets up targeting parameters for an AI action and
// delegates to RandAITarget to select a target.
// Entry mode: A 8-bit (inherited), X 16-bit (inherited), DB=$7E, DP=0
// No input registers; all inputs/outputs in WRAM.
static void AITarget_23_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xAF] = 0;      // sta $af
    ram[0xB0] = 0x0C;   // sta $b0
    ram[0xAD] = ram[0xD2]; // lda $d2 / sta $ad
    RandAITarget_emu(snes); // jmp RandAITarget
}

// PITFALLS: 1 (DB must be $7E for correct WRAM access)
// HELPERS: RandAITarget_emu(snes) — delegates RandAITarget @ $BA:9C
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0xd2=1
//   output_ram:  0xad=1, 0xaf=1, 0xb0=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AITarget_23 ($BA:EF)