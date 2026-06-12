#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// This function decrements the byte at $3881 and returns.
static void MagicEffect_2f_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0x3881]--;  // dec $3881
}

// PITFALLS: 1 (DB=$7E assumed for correct RAM access)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x3881=1
//   output_ram:  0x3881=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_2f ($00:DDDD)