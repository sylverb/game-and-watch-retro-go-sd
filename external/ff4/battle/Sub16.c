#include "snes/snes.h"

// Subtracts two 16-bit values in RAM and stores the result.
// Entry mode: A 16-bit (mf=0), X 16-bit (xf=0), DB=$7E, DP=0
// Inputs:
//   $395E = minuend (16-bit)
//   $3960 = subtrahend (16-bit)
// Output:
//   $3962 = difference (16-bit)
static void Sub16_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint16_t minuend = read16(ram, 0x395E);
    uint16_t subtrahend = read16(ram, 0x3960);
    uint16_t diff = minuend - subtrahend;  // sec + sbc -> subtract with carry=1
    write16(ram, 0x3962, diff);
}

// PITFALLS: 6 (mode A is 16-bit due to `longa` at entry)
// HELPERS: read16, write16 for 16-bit memory access
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x395e=2, 0x3960=2
//   output_ram:  0x3962=2
//   entry_mode:  mf=false, xf=false, dp=0x0, db=0x7E
//   entry_flags: c=true, z=auto, n=auto
// REVERSED_FUNCTION: battle::Sub16 ($84:FC)