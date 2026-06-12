#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Compares two 16-bit values in RAM and increments $de if they match.
//   if (ram[$289e:$289f] == ram[$1801:$1800]) ram[$de]++;
static void AICond_05_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    // Compare bytes at $289e and $1801
    if (ram[0x289e] != ram[0x1801]) return;  // bne @be30
    // Compare bytes at $289f and $1800
    if (ram[0x289f] != ram[0x1800]) return;  // bne @be30
    // If both match, increment $de
    ram[0xde]++;
}

// PITFALLS: 1 (DB=$7E required for correct RAM addressing)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x1800=1, 0x1801=1, 0x289e=1, 0x289f=1
//   output_ram:  0x00DE=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AICond_05 ($BE:1E)