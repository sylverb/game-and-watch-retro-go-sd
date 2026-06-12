#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// No input registers are used; routine loads hardcoded pointer to NoWinAnimTbl
// No output registers; result is in RAM (handled by CheckBattleList)
static void CheckWinAnim_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    // Load 16-bit pointer to NoWinAnimTbl into $ab (LE format)
    write16(ram, 0xAB, 0xA30F);  // NoWinAnimTbl = $7E:A30F
    // Jump to CheckBattleList (tail call)
    check_battle_list_emu(snes);
}

// PITFALLS: 1 (DB must be $7E for absolute stores like sta $ad),
//           9 (upper byte of A preserved in 8-bit mode — but not an issue
//              here since we write full 16-bit value via write16)
// HELPERS: check_battle_list_emu(snes) — delegates CheckBattleList @ $87E4
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::CheckWinAnim ($88:03)