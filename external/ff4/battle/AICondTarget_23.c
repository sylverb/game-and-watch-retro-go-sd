#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// No inputs in registers. All state passed via WRAM.
// This function calls two subroutines unconditionally.
static void AICondTarget_23_c(Snes *snes) {
    target_monster_emu(snes);   // jsr TargetMonster
    no_self_target_emu(snes);   // jmp NoSelfTarget (tail call)
}

// PITFALLS: 1 (DB=$7E required for both subroutines)
// HELPERS: target_monster_emu, no_self_target_emu
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AICondTarget_23 ($C0:6C)