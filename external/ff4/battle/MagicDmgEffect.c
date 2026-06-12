#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM (no register I/O — convention battle):
//   in : ram[$38FE] = damage multiplier (signed), ram[$2704] = enemy flags,
//        ram[$352A] = some flag, ram[$26D2] = enemy ID
//   out: modifies ram as per CalcDmg or jumps to _d416
static void MagicDmgEffect_c(Snes *snes) {
    uint8_t *ram = snes->ram;

    check_strong_elem_emu(snes);         // jsr CheckStrongElem
    uint8_t a = ram[0x38FE];
    if ((a & 0x80) != 0) {               // bpl @d388 → not taken if negative
        ram[0x38FE] = a & 0x7F;          // and #$7f / sta $38fe
        // JMP to _d416 (not translated) — delegate
        Cpu *cpu = snes->cpu;
        cpu->k = 0xD3;
        cpu->pc = 0xD416;
        run_emulated_func(snes, 0xD3D416u);
        return;
    }

    check_weak_elem_emu(snes);           // jsr CheckWeakElem
    if ((ram[0x2704] & 0x40) == 0) {     // and #$40 / beq _d3ae
        goto _d3ae;
    }

    if (ram[0x352A] != 0) {              // lda $352a / bne @d3a7
        uint8_t enemy_id = ram[0x26D2];
        if (enemy_id == 0x28 || enemy_id == 0x55 || enemy_id == 0xA1) {
            // cmp #$28 / beq _d3b1 etc. → RTS
            return;
        }
    } else {
        uint8_t enemy_id = ram[0x26D2];
        if (enemy_id == 0xC7) {
            // cmp #$c7 / beq _d3b1 → RTS
            return;
        }
    }

_d3ae:
    calc_dmg_emu(snes);                  // jsr CalcDmg
_d3b1:
    return;                              // rts
}

// PITFALLS: 1 (DB=$7E required for delegated calls), 9 (JMP to _d416
// delegated because target unknown — requires full CPU state)
// HELPERS: check_strong_elem_emu, check_weak_elem_emu, calc_dmg_emu
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  0x38FE=1, 0x2704=1, 0x352A=1, 0x26D2=1
//   output_ram:  none (side effects via CalcDmg or _d416)
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: auto
// REVERSED_FUNCTION: battle::MagicDmgEffect ($D3:78)