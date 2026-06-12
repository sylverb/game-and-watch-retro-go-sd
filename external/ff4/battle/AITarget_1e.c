#include "snes/snes.h"

// This function sets up status flags for targeting and jumps to a routine
// that finds a monster with those flags.
// Entry mode: A 8-bit (inherited mf=1), X 16-bit (inherited xf=0)
// No inputs from registers; all state is set internally.
// Output is in RAM at $AD-$B0 (status flags for targeting)
static void AITarget_1e_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xAD] = 0x20;  // Set specific status flag for targeting
    ram[0xAE] = 0;     // Clear other status flags
    ram[0xAF] = 0;
    ram[0xB0] = 0;
    // Jump to GetMonsterWithStatus - delegate since it's not translated
    get_monster_with_status_emu(snes);
}

// PITFALLS: 1 (DB must be $7E for WRAM access), 8 (A 8-bit inherited)
// HELPERS: get_monster_with_status_emu(snes)
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  0xad=1, 0xae=1, 0xaf=1, 0xb0=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AITarget_1e ($BA:04)