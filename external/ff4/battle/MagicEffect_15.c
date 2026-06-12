#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM:
//   in : $a4-$a5 = damage (16-bit LE), $270b = current_hp (16-bit LE), $270d = max_hp (16-bit LE)
//   out: $270b = updated_hp (16-bit LE), $a5 = updated flags (bit 7/6 set)
static void MagicEffect_15_c(Snes *snes) {
    uint8_t *ram = snes->ram;

    calc_dmg_emu(snes);         // jsr CalcDmg

    uint16_t dmg = read16(ram, 0xA4);
    uint16_t hp = read16(ram, 0x270B);
    uint16_t max_hp = read16(ram, 0x270D);

    uint16_t new_hp = hp + dmg; // clc / adc
    if (new_hp >= max_hp) {     // cmp / bcc → inverted sense
        new_hp = max_hp;        // lda $270d
    }
    write16(ram, 0x270B, new_hp); // sta $270b

    ram[0xA5] |= 0xC0;          // ora #$c0 / sta $a5
}

// PITFALLS: 1 (DB=$7E required for correct absolute addressing),
//           3 (CMP/BCC inversion: bcc branches when A<mem, so we
//              enter the body when A>=mem),
//           6 (mode A 16-bit during adc/cmp/sta block),
//           7 (arithmetic truncation: adc must be 16-bit, no truncation here)
// HELPERS: calc_dmg_emu(snes) — delegates CalcDmg @ $03:C99F
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  0xa4=2, 0xa5=1, 0x270b=2, 0x270d=2
//   output_ram:  0x270b=2
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_15 ($D9:EC)