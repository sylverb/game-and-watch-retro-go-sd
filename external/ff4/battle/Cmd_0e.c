#include "snes/snes.h"

// This function initializes $C1 to 0 and jumps to DoMultiAttack.
// It's a command handler that clears a flag before proceeding.
static void Cmd_0e_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xC1] = 0;              // stz $c1
    do_multi_attack_emu(snes);  // jmp DoMultiAttack
}

// PITFALLS: 1 (DB must be $7E for WRAM access)
// HELPERS: do_multi_attack_emu(snes)
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: none
// REVERSED_FUNCTION: battle::Cmd_0e ($E6:B7)