#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// No input registers; writes a constant to RAM and jumps to TargetMonsterType.
// Output: ram[$AB] = 1, then continues into TargetMonsterType logic.
static void AICondTarget_26_c(Snes *snes) {
    snes->ram[0xAB] = 1;         // lda #$01 / sta $ab
    target_monster_type_emu(snes); // jmp TargetMonsterType
}

// PITFALLS: 1 (DB must be $7E to access WRAM), 9 (no hidden upper byte issue here)
// HELPERS: target_monster_type_emu(snes)
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  0x00AB=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AICondTarget_26 ($C0:003B)