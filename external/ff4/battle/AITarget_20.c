#include "snes/snes.h"

// This function sets up specific RAM locations with fixed values and then
// jumps to GetMonsterWithStatus. It appears to be an AI targeting routine
// that looks for a monster with a specific status (indicated by the preset
// values in $ad-$b0).

static void AITarget_20_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    
    ram[0xAD] = 0x08;  // Set $ad to 8
    ram[0xAE] = 0x00;  // Clear $ae
    ram[0xAF] = 0x00;  // Clear $af
    ram[0xB0] = 0x00;  // Clear $b0
    
    // Jump to GetMonsterWithStatus (delegate since it's not translated)
    get_monster_with_status_emu(snes);
}

// PITFALLS: 1 (DB must be $7E for correct RAM addressing)
// HELPERS: get_monster_with_status_emu(snes)
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AITarget_20 ($BA:74)