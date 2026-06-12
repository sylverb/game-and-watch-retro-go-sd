#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: none (no registers read at entry)
// Logic:
//   X = ram[$D2] - 5
//   index into $29B5[X] to get index2
//   read $29CA[index2] and compare with $29CD
//   if equal, increment $DE
static void AICond_06_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint16_t x = (uint16_t)(ram[0xD2] - 5);        // sec / lda $d2 / sbc #$05 / tax
    uint16_t index2 = ram[0x29B5 + x];             // lda $29b5,x / tax
    uint8_t val = ram[0x29CA + index2];             // lda $29ca,x
    if (val == ram[0x29CD]) {                      // cmp $29cd / bne (inverted)
        ram[0xDE]++;                               // inc $de
    }
}

// PITFALLS: 1 (DB=$7E assumed), 3 (CMP/BNE inversion: bne skips the inc)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0xd2=1, 0x29b5=1, 0x29ca=1, 0x29cd=1
//   output_ram:  0xde=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AICond_06 ($BE:31)