#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM (no register I/O — convention battle):
//   in : ram[$2704] and ram[$2706] (status bytes), $273b (target byte)
//   out: ram[$a9] = original $2704, ram[$2704] and ram[$2706] masked,
//        ram[$273b] = 0x10
static void MagicEffect_2a_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint8_t temp = ram[0x2704];       // lda $2704
    ram[0xA9] = temp;                 // sta $a9
    temp &= 0xBB;                     // and #$bb
    ram[0x2704] = temp;               // sta $2704
    temp = ram[0x2706];               // lda $2706
    temp &= 0xC3;                     // and #$c3
    ram[0x2706] = temp;               // sta $2706
    ram[0x273B] = 0x10;               // lda #$10 / sta $273b
}

// PITFALLS: 1 (DB=$7E required for WRAM access)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x2704=1, 0x2706=1
//   output_ram:  0xA9=1, 0x2704=1, 0x2706=1, 0x273B=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_2a ($DD:B1)