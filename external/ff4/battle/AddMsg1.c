#include "snes/snes.h"

// Sets up a battle message to display "1" (likely for item usage or single target)
// by writing the message type and index to the message buffer.
static void AddMsg1_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0x33C2] = 0xF8;  // message index for displaying "1"
    ram[0x33C3] = 0x03;  // message type: battle message
}

// PITFALLS: 1 (DB must be $7E for correct RAM writes)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  0x33C2=1, 0x33C3=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AddMsg1 ($85:9B)