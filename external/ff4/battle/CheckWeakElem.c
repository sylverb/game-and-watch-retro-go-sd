#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM (no register I/O — convention battle):
//   in : ram[$38FE] = action type, ram[$2720] and ram[$2721] = target elemental props
//   out: ram[$38FE] = updated action type (e.g. 0x04 or 0x08 for healing)
static void CheckWeakElem_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint8_t action = ram[0x38FE];
    if (action != 2) return;              // cmp #$02 / bne @e155
    uint8_t elem1 = ram[0x2721];
    uint8_t mask = ram[0x28A2];
    if ((elem1 & mask) != 0) {            // and / beq @e148
        ram[0x38FE] = 8;                  // lda #$08 / sta $38fe
        return;
    }
    uint8_t elem0 = ram[0x2720];
    if ((elem0 & mask) != 0) {            // and / beq @e155
        ram[0x38FE] = 4;                  // lda #$04 / sta $38fe
    }
}

// PITFALLS: 1 (DB=$7E assumed for all RAM accesses)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x38FE=1, 0x2720=1, 0x2721=1, 0x28A2=1
//   output_ram:  0x38FE=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::CheckWeakElem ($E1:33)