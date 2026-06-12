#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Input: X = target index (used as offset into $2000+ structures)
// Output: ram[$35EA] = 0 if target valid, 1 if invalid
static void ValidateAITarget_c(Snes *snes, uint16_t x) {
    uint8_t *ram = snes->ram;
    ram[0x35EA] = 0;                    // stz $35ea
    uint8_t a = ram[0x2003 + x];        // lda $2003,x
    a &= 0xC0;                          // and #$c0
    if (a != 0) goto invalid;           // bne @c0f0
    a = ram[0x2005 + x];                // lda $2005,x
    a &= 0x82;                          // and #$82
    if (a != 0) goto invalid;           // bne @c0f0
    a = ram[0x2006 + x];                // lda $2006,x
    if (a & 0x80) goto invalid;         // bpl @c0f3 (inverted)
    return;                             // rts
invalid:
    ram[0x35EA]++;                      // inc $35ea
}

// PITFALLS: 1 (DB=$7E assumed), 6 (A 8-bit mode assumed)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=16, y=none
//   inputs_ram:  0x2003+x=1, 0x2005+x=1, 0x2006+x=1
//   output_ram:  0x35EA=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::ValidateAITarget ($C0:DA)