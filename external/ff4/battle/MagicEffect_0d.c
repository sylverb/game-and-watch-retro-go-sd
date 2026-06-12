#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM (no register I/O — convention battle):
//   in : ram[$272A] = some value (checked vs $FF, then incremented)
//        ram[$1800] = battle ID low byte
//        ram[$1801] = battle ID high byte
//   out: ram[$272A] = incremented value (capped at $FF)
//   conditional: if ram[$272A] == $FF → RemoveTarget is called
//                if battle == $01B7 (zeromus) → no increment
static void MagicEffect_0d_c(Snes *snes) {
    uint8_t *ram = snes->ram;

    if (ram[0x272A] == 0xFF) {           // lda $272a / cmp #$ff / bne
        remove_target_emu(snes);         // jmp RemoveTarget
        return;
    }

    // Check if battle ID == $01B7 (zeromus)
    if (ram[0x1800] == 0xB7 && ram[0x1801] == 0x01) {
        // lda $1800 / cmp #$b7 / bne @d855
        // lda $1801 / bne @d862
        return;  // Skip increment for zeromus
    }

    // Increment ram[$272A] by 5, cap at $FF
    uint16_t val = (uint16_t)ram[0x272A] + 5;  // clc / adc #$05
    if (val > 0xFF) {
        val = 0xFF;  // bcc @d85f / lda #$ff
    }
    ram[0x272A] = (uint8_t)val;  // sta $272a
}

// PITFALLS: 1 (DB=$7E required for WRAM access), 3 (CMP/BCS-style logic)
// HELPERS: remove_target_emu(snes) — delegates RemoveTarget @ $00:E030
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  0x272A=1, 0x1800=1, 0x1801=1
//   output_ram:  0x272A=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: auto
// REVERSED_FUNCTION: battle::MagicEffect_0d ($D8:3F)