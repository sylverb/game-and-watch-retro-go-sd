#include "snes/snes.h"

// Toggles a flag and conditionally draws MP if the flag is zero.
// Uses a frame counter to control drawing rate.
static void DrawMP_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0x353E] ^= 1;              // lda $353e / eor #1 / sta $353e
    if (ram[0x353E] != 0) return;  // bne @8084
    if (ram[0x353F] == 2) {        // lda $353f / cmp #$02 / bne @8075
        ram[0x353F] = 0;           // lda #0 / sta $353f
    }
    if (ram[0x353F] != 0) {        // lda $353f / bne @807c
        // bra @8081 is here in asm, but logically part of the if-block
        snes->cpu->a = 0x0D;          // lda #$0d
        ExecBtlGfx_emu(snes); // jsr ExecBtlGfx
    }
    ram[0x353F]++;                 // inc $353f
} // rts

// PITFALLS: 1 (DB=$7E assumed), 2 (Z/N flags not involved at entry)
// HELPERS: ExecBtlGfx_emu(snes) — delegates ExecBtlGfx @ $80:85
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  0x353E=1, 0x353F=1
//   output_ram:  0x353F=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::DrawMP ($80:5F)