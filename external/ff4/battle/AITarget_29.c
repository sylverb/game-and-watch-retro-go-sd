#include "snes/snes.h"

// This function initializes targeting flags for a monster with a specific status.
// It sets up a bitfield in $ad-$b0 and then jumps to GetMonsterWithStatus.
static void AITarget_29_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    
    ram[0xAD] = 0;  // stz $ad
    ram[0xAE] = 0;  // stz $ae
    ram[0xAF] = 0x80;  // lda #$80 / sta $af
    ram[0xB0] = 0x80;  // sta $b0
    get_monster_with_status_emu(snes);  // jmp GetMonsterWithStatus
}

// PITFALLS: 1 (DB must be $7E for WRAM access)
// HELPERS: get_monster_with_status_emu(snes) - delegates GetMonsterWithStatus @ $00:BA11
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AITarget_29 ($BB:6B)