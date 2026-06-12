#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM (no register I/O — convention battle):
//   in : ram[$38FB] base_hits, ram[$38FA] hit_rate
//   out: ram[$38FD] = number of hits
static void CalcHits_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0x38FD] = 0;                // stz $38fd
    uint8_t base = ram[0x38FB];     // lda $38fb
    if (base == 0) return;          // beq @c99e
    uint8_t rate = ram[0x38FA];
    for (uint8_t y = base; y > 0; y--) { // tay / dey / bne loop
        uint8_t r = Rand99_emu(snes);    // jsr Rand99 (delegated)
        if (r < rate) {                  // cmp $38fa / bcs (inverted!)
            ram[0x38FD]++;               // inc $38fd
        }
    }
}

// PITFALLS: 3 (CMP/BCS inversion: bcs branches when A>=mem, so we
// enter the body when A<mem)
// HELPERS: Rand99_emu(snes) — delegates Rand99 @ $03:858B
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x38FB=1, 0x38FA=1
//   output_ram:  0x38FD=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::CalcHits ($C9:87)