#include "snes/snes.h"

// This function sets up AI targeting parameters and jumps to RandAITarget.
// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// No input registers are used. All values are written to RAM directly.
static void AITarget_24_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xAF] = 0x05;
    ram[0xB0] = 0x0C;
    ram[0xAD] = 0xFF;
    RandAITarget_emu(snes);  // jmp RandAITarget
}

// PITFALLS: 1 (DB=$7E required for correct RAM writes)
// HELPERS: RandAITarget_emu(snes) — delegates RandAITarget @ $BA:9C
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AITarget_24 ($BA:FE)