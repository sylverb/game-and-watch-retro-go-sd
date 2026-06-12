#include "snes/snes.h"

// Sets up a barrier spell (armor) and performs a magic attack.
// Always displays "no effect" message afterward.
static void Cmd_0f_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0x26D2] = 0x05;  // barrier spell type
    ram[0x33C4] = 0;     // clear some state
    do_magic_attack_emu(snes);  // perform the magic attack
    add_msg3_emu(snes);         // add message slot 3
    ram[0x34CA] = 0x3A;  // こうかが　なかった (no effect)
    ram[0x34C8] = 0x0F;  // set command name index
    ram[0x34C7] = 0x10;  // show command name
}

// PITFALLS: 1 (DB=$7E for WRAM access)
// HELPERS: do_magic_attack_emu, add_msg3_emu
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::Cmd_0f ($E6:99)