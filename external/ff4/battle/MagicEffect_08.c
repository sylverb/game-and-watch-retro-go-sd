#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// This function sets bits 2 and 3 ($0C) in the byte at $2706
// No inputs or outputs in registers; all access is via WRAM
static void MagicEffect_08_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0x2706] |= 0x0C;  // lda / ora #$0c / sta (8-bit operation)
}

// PITFALLS: 1 (DB must be $7E to access WRAM), 6 (A is 8-bit)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x2706=1
//   output_ram:  0x2706=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_08 ($D6:13)