#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// No register inputs; all state is in WRAM.
// Calls two sub-routines:
//   MagicDmgEffect — applies damage
//   SetMagicStatus2 — applies status effect (tail call)
static void MagicEffect_28_c(Snes *snes) {
    magic_dmg_effect_emu(snes);  // jsr MagicDmgEffect
    set_magic_status2_emu(snes); // jmp SetMagicStatus2 (tail call)
}

// PITFALLS: 1 (DB=$7E required for WRAM access)
// HELPERS: magic_dmg_effect_emu, set_magic_status2_emu
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_28 ($DD:95)