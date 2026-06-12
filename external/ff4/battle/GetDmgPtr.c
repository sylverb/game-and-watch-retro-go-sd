#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: cpu->a = target index (7-bit), bit 7 set for enemies
// Logic:
//   if target >= 0 (character):
//     index = target * 2
//   else:
//     index = (target & 0x7F) + 5, then index *= 2
// Output: X = index * 2 (16-bit), used as offset into party/enemy arrays
static void GetDmgPtr_c(Snes *snes, uint8_t target) {
    uint8_t *ram = snes->ram;
    ram[0xA9] = target;              // sta $a9
    uint8_t a = target;
    if ((a & 0x80) == 0) {           // bpl @ca6b (branch if positive/character)
        a &= 0x7F;                   // and #$7f (not needed, but matches asm)
    } else {
        a &= 0x7F;                   // and #$7f
        a = (uint8_t)(a + 5);        // clc / adc #$05 (8-bit truncation, pitfall 7)
    }
    a <<= 1;                         // asl (8-bit shift, result fits in X)
    snes->cpu->x = (uint16_t)a;      // tax (X is 16-bit, so zero-extend)
}

// PITFALLS: 7 (8-bit arithmetic truncation on adc #$05)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=8, x=none, y=none
//   inputs_ram:  none
//   output_ram:  0xA9=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::GetDmgPtr ($00:CA62)