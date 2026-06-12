#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM:
//   in : ram[$2724] (1 byte, base mag.def), ram[$1800]-$1801 (battle id)
//   out: ram[$2724] updated or target removed
static void MagicEffect_0e_c(Snes *snes) {
    uint8_t *ram = snes->ram;

    uint8_t mag_def = ram[0x2724];
    if (mag_def == 0xFF) {               // cmp #$ff / bne @d86d
        remove_target_emu(snes);         // jmp RemoveTarget
        return;
    }

    // Check if battle ID is $01b7 (zeromus)
    if (ram[0x1800] == 0xB7 && ram[0x1801] == 0x01) {
        return;  // zeromus: no change
    }

    // Increase mag.def by 3, cap at 0xFF
    uint16_t new_mag_def = (uint16_t)mag_def + 3;
    if (new_mag_def > 0xFF) {
        new_mag_def = 0xFF;
    }
    ram[0x2724] = (uint8_t)new_mag_def;
}

// PITFALLS: 1 (DB=$7E required for WRAM access),
//           7 (arithmetic truncation in 8-bit mode — but not triggered here)
// HELPERS: remove_target_emu(snes) — delegates RemoveTarget @ $00:E030
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x2724=1, 0x1800=1, 0x1801=1
//   output_ram:  0x2724=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_0e ($D8:63)