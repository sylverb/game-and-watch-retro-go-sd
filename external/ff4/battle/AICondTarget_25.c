#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM (no register I/O — convention battle):
//   out: ram[$AB] = 0 (cleared before jumping to TargetMonsterType)
//   Note: TargetMonsterType is expected to update ram[$AB] based on monster type logic
static void AICondTarget_25_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xAB] = 0;                    // stz $ab
    target_monster_type_emu(snes);    // jmp TargetMonsterType (delegated)
}

// PITFALLS: 1 (DB=$7E required for correct stz $ab)
// HELPERS: target_monster_type_emu(snes) — delegates TargetMonsterType @ $00:BFFB
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  0xAB=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AICondTarget_25 ($00:0BF6)