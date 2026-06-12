#include "snes/snes.h"

// Entry mode: A 8-bit (inherited mf=1), X 16-bit (inherited xf=0)
// No input registers; returns random monster index in A (0-6)
static void RandMonster_c(Snes *snes) {
    Cpu *cpu = snes->cpu;
    cpu->x = 0;
    cpu->a = 7;
    RandXA_emu(snes);
}

// PITFALLS: 1 (DB must be $7E for WRAM access if any — but this routine
// uses no WRAM), 2 (no flags needed — no branch on entry)
// HELPERS: RandXA_emu(snes) — delegates RandXA @ $03:8379
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::RandMonster ($03:8579)