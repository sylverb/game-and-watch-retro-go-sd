#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: none (no registers read at entry)
// Logic:
//   Compare $35F3[X] with $289F. If equal, increment $DE.
static void AICond_02_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint16_t x = ram[0x289E];              // lda $289e / tax (X 16-bit)
    uint8_t val = ram[0x35F3 + x];         // lda $35f3,x
    if (val == ram[0x289F]) {              // cmp $289f / bne @bdb7
        ram[0xDE]++;                       // inc $de
    }
}

// PITFALLS: 1 (DB=$7E assumed), 6 (A 8-bit mode inferred from context)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x289E=1, 0x289F=1, 0x35F3=1
//   output_ram:  0x00DE=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AICond_02 ($BD:A9)