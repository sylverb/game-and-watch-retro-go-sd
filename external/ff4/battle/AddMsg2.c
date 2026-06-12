#include "snes/snes.h"

// This function sets up battle message type 3 with display flag $F8
// in the battle message buffer at $33C6-$33C7.
static void AddMsg2_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0x33C6] = 0xF8;  // display text flag
    ram[0x33C7] = 0x03;  // battle message type
}

// PITFALLS: 1 (DB=$7E assumed for correct RAM addressing)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  0x33C6=1, 0x33C7=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AddMsg2 ($85:A6)