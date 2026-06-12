#include "snes/snes.h"

// 16-bit unsigned division: ram[$3945] / ram[$3947] → ram[$3949] (quotient), ram[$394B] (remainder)
// Entry mode: A 16-bit (mf=0), X/Y 16-bit (xf=0), DB=$7E, DP=0
// Uses no sub-routines — fully self-contained.
static void Div16_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    write16(ram, 0x3949, 0);  // quotient
    write16(ram, 0x394B, 0);  // remainder
    uint16_t dividend = read16(ram, 0x3945);
    uint16_t divisor  = read16(ram, 0x3947);
    if (dividend == 0 || divisor == 0) return;  // beq @843f

    uint16_t remainder = 0;
    uint16_t quotient  = 0;
    for (int i = 0; i < 16; i++) {
        // rol $3945 (but we simulate it on dividend)
        uint16_t carry = (dividend >> 15) & 1;
        dividend <<= 1;
        // rol $394b (remainder)
        remainder = (remainder << 1) | carry;
        // sec / sbc
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient = (quotient << 1) | 1;  // set bit 0 of quotient
        } else {
            quotient <<= 1;  // clear bit 0
        }
    }
    write16(ram, 0x3949, quotient);
    write16(ram, 0x394B, remainder);
}

// PITFALLS: 6 (mode A 16-bit required), 7 (arithmetic truncation not
// relevant here since all ops are 16-bit and explicit)
// HELPERS: none
// CONTRACT:
//   inputs_ram:  0x3945=2, 0x3947=2
//   output_ram:  0x3949=2
//   entry_mode:  mf=false, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::Div16 ($84:07)