#include "snes/snes.h"

// This function selects a random target by first attempting to target
// a character, and if that fails, targeting a monster instead.
// It acts as a fallback chain: character → monster.
static void AnyTarget_c(Snes *snes) {
    target_character_emu(snes);  // jsr TargetCharacter
    target_monster_emu(snes);    // jmp TargetMonster (tail call)
}

// PITFALLS: none (simple delegation chain)
// HELPERS: target_character_emu, target_monster_emu
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AnyTarget ($C0:0049)