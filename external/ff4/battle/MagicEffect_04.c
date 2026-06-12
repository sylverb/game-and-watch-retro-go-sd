#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: no explicit register inputs, all data in WRAM
// Logic:
//   if ($cd != $ce) {
//     call CalcDmg
//     $a2 = $a4 (copy damage low)
//     if ($2740 & 0x80) {
//       $a5 |= 0x80 (set high bit of high byte)
//     } else {
//       $a3 |= 0x80 (set high bit of middle byte)
//     }
//   }
//   return
static void MagicEffect_04_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    if (ram[0xCD] == ram[0xCE]) return; // beq @d487
    calc_dmg_emu(snes);                 // jsr CalcDmg
    write16(ram, 0xA2, read16(ram, 0xA4)); // ldx $a4 / stx $a2
    if ((ram[0x2740] & 0x80) != 0) {    // and #$80 / beq @d481
        ram[0xA5] |= 0x80;              // ora #$80 / sta $a5
    } else {
        ram[0xA3] |= 0x80;              // lda $a3 / ora #$80 / sta $a3
    }
}

// PITFALLS: 1 (DB=$7E required for WRAM access), 6 (mode A 8-bit assumed)
// HELPERS: calc_dmg_emu(snes) — delegates CalcDmg @ $03:C99F
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0xCD=1, 0xCE=1, 0x2740=1
//   output_ram:  0xA2=2, 0xA3=1, 0xA5=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_04 ($D4:66)