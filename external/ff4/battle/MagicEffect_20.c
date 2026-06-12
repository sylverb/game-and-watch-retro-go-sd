#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: none (no inputs from registers)
// Logic:
//   1. Generate random number in 0..3, store in $00A9
//   2. Rotate carry into $28A4, repeat Y times (Y = random)
//   3. If random number was 5 (impossible with X=3), jump to SetMagicStatus2
//   4. Else jump to SleepParalyzeEffect
static void MagicEffect_20_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    Cpu *cpu = snes->cpu;

    cpu->x = 3;                          // ldx #3
    cpu->a = 5;                          // lda #5
    cpu->mf = true;                      // 8-bit A for RandXA
    RandXA_emu(snes);                    // jsr RandXA
    uint8_t rand = cpu->a;               // returned in A
    cpu->y = rand;                       // tay
    ram[0x00A9] = rand;                  // sta a:$00a9

    cpu->c = true;                       // sec
    do {
        uint16_t val = read16(ram, 0x28A4);
        uint8_t carry = cpu->c ? 1 : 0;
        cpu->c = val & 1;                // lsb → C
        val = (val >> 1) | (carry << 15); // ror
        write16(ram, 0x28A4, val);
        cpu->y--;                        // dey
    } while (cpu->y != 0);               // bne @dca8

    uint8_t stored = ram[0x00A9];        // lda a:$00a9
    if (stored == 5) {                   // cmp #$05 / beq @dcb8
        // jmp SetMagicStatus2
        SetMagicStatus2_emu(snes);
    } else {
        // jmp SleepParalyzeEffect
        SleepParalyzeEffect_emu(snes);
    }
}

// PITFALLS: 1 (DB=$7E for WRAM access), 6 (mode A 8-bit assumed),
//           7 (16-bit rotate must preserve carry chain)
// HELPERS: RandXA_emu(snes), SetMagicStatus2_emu(snes),
//          SleepParalyzeEffect_emu(snes)
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: none
// REVERSED_FUNCTION: battle::MagicEffect_20 ($DC:9B)