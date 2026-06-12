#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// No input registers; output is fixed: ram[$A8] = 4
static void MagicEffect_32_c(Snes *snes) {
    snes->ram[0xA8] = 4;
}

// PITFALLS: 1 (DB=$7E assumed for all battle routines)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  0x00A8=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_32 ($DF:D2)