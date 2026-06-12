#include "snes/snes.h"

// Sets up a battle message to display "Not enough MP" (index 0x03F8)
// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
static void AddMsg3_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0x33C8] = 0xF8;  // message index low byte
    ram[0x33C9] = 0x03;  // message index high byte (0x03F8)
}

// PITFALLS: 1 (DB must be $7E for writes to $33C8-$33C9 to target WRAM)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  0x33C8=1, 0x33C9=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AddMsg3 ($85:B1)