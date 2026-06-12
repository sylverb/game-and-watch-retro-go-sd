#include "snes/snes.h"

// Sets up targeting for AI command 0x1F:
//   ram[$AD] = 0x10 (target type)
//   ram[$AE] = 0x00
//   ram[$AF] = 0x00
//   ram[$B0] = 0x00
// Then jumps to GetMonsterWithStatus to resolve the target.
//
// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// No input registers; all state passed via WRAM.
static void AITarget_1f_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xAD] = 0x10;
    ram[0xAE] = 0x00;
    ram[0xAF] = 0x00;
    ram[0xB0] = 0x00;
    get_monster_with_status_emu(snes); // tail call to GetMonsterWithStatus
}

// PITFALLS: 1 (DB=$7E required for WRAM access)
// HELPERS: get_monster_with_status_emu(snes)
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AITarget_1f ($BA:67)