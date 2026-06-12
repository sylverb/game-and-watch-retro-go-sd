#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: none (no registers read at entry)
// Logic:
//   1. Call RandXA(0, 1) to get 0 or 1
//   2. If result is 0:
//        set $28a3 = 0x80 and jump to SetMagicStatus2
//      Else:
//        set $28a4 = 0x20 and jump to SleepParalyzeEffect
static void MagicEffect_21_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    Cpu *cpu = snes->cpu;

    // Set up for RandXA call: X=0, A=1
    cpu->x = 0;
    cpu->a = 1;
    cpu->mf = true;  // A 8-bit
    cpu->xf = false; // X 16-bit
    cpu->db = 0x7E;  // Required for WRAM access
    RandXA_emu(snes); // jsr RandXA

    uint16_t rand_result = cpu->a; // Result returned in A
    if (rand_result == 0) {        // tax / bne @dcce
        ram[0x28A3] = 0x80;        // lda #$80 / sta $28a3
        SetMagicStatus2_emu(snes); // jmp SetMagicStatus2
    } else {
        ram[0x28A4] = 0x20;        // lda #$20 / sta $28a4
        SleepParalyzeEffect_emu(snes); // jmp SleepParalyzeEffect
    }
}

// PITFALLS: 1 (DB=$7E for WRAM access), 8 (mode A/X must match caller)
// HELPERS: RandXA_emu(snes), SetMagicStatus2_emu(snes),
//          SleepParalyzeEffect_emu(snes)
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: none
// REVERSED_FUNCTION: battle::MagicEffect_21 ($DC:BB)