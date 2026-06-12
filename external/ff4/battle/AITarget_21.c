#include "snes/snes.h"

// This function initializes a 32-bit value at $AD-$B0 to 1 and then
// jumps to GetMonsterWithStatus. The purpose is to set up a search
// criterion for a monster with a specific status (value 1).
static void AITarget_21_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xAD] = 0;  // stz $ad
    ram[0xAE] = 1;  // lda #$01 / sta $ae
    ram[0xAF] = 0;  // stz $af
    ram[0xB0] = 0;  // stz $b0
    get_monster_with_status_emu(snes);  // jmp GetMonsterWithStatus
}

// PITFALLS: 1 (DB must be $7E for WRAM access), 4 (stack addr irrelevant here)
// HELPERS: get_monster_with_status_emu(snes) — delegates GetMonsterWithStatus @ $BA:11
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AITarget_21 ($BA:81)