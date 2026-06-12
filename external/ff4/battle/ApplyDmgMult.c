#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: cpu->a = elemental_multiplier (8-bit), $a4-$a5 = damage 16-bit LE
// CALLER MUST set Z and N flags to reflect the input value BEFORE jsr,
// because the routine starts with `bne` (consults Z).
//
// Logic:
//   if mult == 0     : damage = 0
//   if mult == 1     : damage >>= 1
//   else (mult > 1)  : damage = (mult >> 1) * damage  (truncated to 16-bit)
static void ApplyDmgMult_c(Snes *snes, uint8_t mult) {
    uint8_t *ram = snes->ram;

    if (mult == 0) {                     // bne @ca48 → not taken
        ram[0xA4] = 0;
        ram[0xA5] = 0;
        return;
    }
    uint8_t shifted = mult >> 1;         // lsr A
    if (shifted == 0) {                  // bne @ca50 → not taken (mult was 1)
        uint16_t dmg = read16(ram, 0xA4);
        dmg >>= 1;                       // lsr $a5 / ror $a4
        write16(ram, 0xA4, dmg);
        return;
    }
    // mult > 1 : Mult16(shifted, damage), truncate to 16-bit
    write16(ram, 0x393D, (uint16_t)shifted);  // stx $393d (X 16-bit, X_hi=0)
    uint16_t dmg = read16(ram, 0xA4);
    write16(ram, 0x393F, dmg);                 // stx $393f
    mult16_emu(snes);                          // jsr Mult16 (delegated)
    write16(ram, 0xA4, read16(ram, 0x3941));   // damage = result lo
}

// PITFALLS: 1 (DB=$7E required), 2 (Z/N flags simulated by caller — but
// from the FUNCTION SIDE this is a documented contract, not a fix)
// HELPERS: mult16_emu(snes) — delegates Mult16 @ $03:83B9
//          read16/write16 — little-endian 16-bit accessors over ram[]
// CONTRACT:
//   inputs_reg:  a=8, x=none, y=none
//   inputs_ram:  0xa4=2
//   output_ram:  0xa4=2
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::ApplyDmgMult ($03:CA41)