#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// No input registers; all state is internal or in WRAM
// Output: writes to $28A3 or $28A4, then jumps to effect handlers
static void MagicEffect_22_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    snes->cpu->x = 0;                  // ldx #0
    snes->cpu->a = 2;                  // lda #2
    RandXA_emu(snes);                  // jsr RandXA (delegated)
    uint16_t a = snes->cpu->a;
    snes->cpu->x = a;                  // tax
    if (a != 0) goto dce9;             // bne @dce9
    ram[0x28A4] = 0x20;                // lda #$20 / sta $28a4
    SleepParalyzeEffect_emu(snes);     // jmp SleepParalyzeEffect
    return;

dce9:
    a--;                               // dec
    if (a != 0) goto dcf3;             // bne @dcf3
    ram[0x28A3] = 0x04;                // lda #$04 / sta $28a3
    goto dcf8;                         // bra @dcf8

dcf3:
    ram[0x28A4] = 0x80;                // lda #$80 / sta $28a4

dcf8:
    SetMagicStatus2_emu(snes);         // jmp SetMagicStatus2
}

// PITFALLS: 1 (DB=$7E required for WRAM access), 2 (flags handled by caller)
// HELPERS: RandXA_emu(snes), SleepParalyzeEffect_emu(snes),
//          SetMagicStatus2_emu(snes)
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_22 ($DC:D6)