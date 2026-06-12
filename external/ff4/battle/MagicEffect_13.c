#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM (no register I/O — convention battle):
//   in : ram[$38E5] bit0 (0=ally, 1=enemy)
//   out: ram[$38F3], ram[$38D3], ram[$35A3] (if ally)
//        ram[$3550], ram[$34CA] (if enemy)
static void MagicEffect_13_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint8_t a = ram[0x38E5] & 1;  // lda $38e5 / and #$01
    if (a != 0) {                 // bne @d984
        ram[0x3550] = 0;          // stz $3550
        add_msg3_emu(snes);       // jsr AddMsg3
        ram[0x34CA] = 0x22;       // lda #$22 / sta $34ca
        return;
    }
    ram[0x38F3]++;                // inc $38f3
    a++;                          // inc A
    ram[0x38D3] = a;              // sta $38d3
    ram[0x35A3] = a;              // sta $35a3
}

// PITFALLS: 1 (DB=$7E required for WRAM access)
// HELPERS: add_msg3_emu(snes) — delegates AddMsg3 @ $03:85B1
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  0x38E5=1
//   output_ram:  none (multiple outputs, caller reads)
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_13 ($D9:72)