#include "snes/snes.h"

// CountBits: counts the number of set bits in the 8-bit input value.
// Entry: A = value to count bits in (8-bit)
// Exit:  X = number of set bits (0-8)
static void CountBits_c(Snes *snes, uint8_t a) {
    uint16_t x = 0;           // ldx #0
    for (int y = 8; y > 0; y--) {  // ldy #8 / dey / bne loop
        if (a & 0x80) x++;    // asl A / bcc (inverted) → if carry, inc X
        a <<= 1;              // asl A (shift left, bit 7 → carry)
    }
    snes->cpu->x = x;         // result in X
}

// PITFALLS: 6 (mode A assumed 8-bit from context and lack of longa/shorta)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=8, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::CountBits ($85:0C)