#include "snes/snes.h"

// Sets $ab to 2, then jumps to TargetMonsterType.
// Entry mode: A 8-bit (inherited), X 16-bit (inherited), DB=$7E, DP=0
// No flags or registers are significant on entry.
static void AICondTarget_27_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xAB] = 2;              // lda #$02 / sta $ab
    target_monster_type_emu(snes); // jmp TargetMonsterType
}

// PITFALLS: 1 (DB must be $7E for correct RAM access)
// HELPERS: target_monster_type_emu(snes)
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  0xAB=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AICondTarget_27 ($C0:0042)