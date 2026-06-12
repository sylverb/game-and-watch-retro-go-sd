#include "snes/snes.h"

// Copies two 16-bit values from $2709/$270D to $2707/$270B respectively.
// Entry mode: A 16-bit (mf=0), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM:
//   in : ram[$2709] and ram[$270D] (16-bit each)
//   out: ram[$2707] = ram[$2709], ram[$270B] = ram[$270D]
static void MagicEffect_16_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    write16(ram, 0x2707, read16(ram, 0x2709)); // lda $2709 / sta $2707
    write16(ram, 0x270B, read16(ram, 0x270D)); // lda $270d / sta $270b
}

// PITFALLS: 1 (DB=$7E required for correct absolute addressing)
// HELPERS: read16/write16 for 16-bit memory access
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x2709=2, 0x270D=2
//   output_ram:  0x2707=2, 0x270B=2
//   entry_mode:  mf=false, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_16 ($DA:0C)