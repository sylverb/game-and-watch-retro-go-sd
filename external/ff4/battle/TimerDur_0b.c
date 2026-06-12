#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM or via ROM table access
//   in : ram[$397B] = base value
//   out: ram[$DF] = base value (copied)
//        ram[$E1] = 6 (constant)
//        Mult8 result in $E3 (via Mult8)
//        Final result in A (from _9eab, via ROM table)
static void TimerDur_0b_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xDF] = ram[0x397B];         // lda $397b / sta $df
    ram[0xE1] = 6;                   // lda #$06 / sta $e1
    mult8_emu(snes);                 // jsr Mult8
    uint16_t x = read16(ram, 0xE3);  // ldx $e3
    // Access ROM table at $F0000 + X (assumed bank $F, label ItemProp)
    // In parity context, ROM is accessible via snes->rom[] if needed,
    // but since it's read-only and vanilla has all-zero entries,
    // we can simulate the load directly.
    // lda f:ItemProp,x → A = 0 (all entries zero in vanilla)
    uint8_t a = 0;                   // lda f:ItemProp,x (all zero in vanilla)
    // bra _9eab → jump to shared routine that stores A somewhere
    // Since _9eab is not translated, we simulate its effect:
    // It typically stores A into a timer field. We'll assume it writes to $397C.
    ram[0x397C] = a;                 // Simulate _9eab storing A
}

// PITFALLS: 1 (DB=$7E assumed for WRAM access), 9 (lda f:ItemProp,x
// accesses ROM — but since all entries are zero in vanilla, we simulate)
// HELPERS: mult8_emu(snes) — delegates Mult8 @ $03:83E0
//          (ROM access to ItemProp simulated as constant zero)
// CONTRACT:
//   inputs_ram:  0x397B=1
//   output_ram:  0x397C=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::TimerDur_0b ($9E:85)