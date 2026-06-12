#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Input: ram[$269D] = base value (8-bit)
// Output: none (CalcDmg writes to $3902-$3903 and processes damage)
// This function computes (base_value * 2) and stores it in $3902-$3903,
// then jumps to CalcDmg to process the damage calculation.
static void MagicEffect_2b_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint8_t base = ram[0x269D];         // lda $269d
    uint16_t val = (uint16_t)base;      // tax (zero-extend to 16-bit)
    write16(ram, 0x3902, val);          // stx $3902

    uint16_t doubled = read16(ram, 0x3902);
    doubled <<= 1;                      // asl $3902
    write16(ram, 0x3902, doubled);      // rol $3903 handled by 16-bit shift

    calc_dmg_emu(snes);                 // jmp CalcDmg
}

// PITFALLS: 1 (DB=$7E required for correct RAM access),
//           6 (A 8-bit mode assumed from battle module convention),
//           7 (X 16-bit mode assumed, stx writes full 16-bit value)
// HELPERS: calc_dmg_emu(snes) — delegates CalcDmg @ $00:C99F
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x269D=1
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_2b ($DD:C9)