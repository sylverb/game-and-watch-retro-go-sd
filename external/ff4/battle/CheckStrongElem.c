#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM:
//   in : ram[$2725] = weak element flags
//        ram[$2726] = strong element flags
//        ram[$28A2] = attack element flags
//   out: ram[$38FE] = damage multiplier code
static void CheckStrongElem_c(Snes *snes) {
    uint8_t *ram = snes->ram;

    uint8_t strong = ram[0x2726];
    uint8_t attack = ram[0x28A2];
    if ((strong & attack) != 0) {     // and $28a2 / beq @e119
        ram[0x38FE] = 0;              // sta $38fe (zero damage)
        if ((strong & 0x40) != 0) {   // and #$40 / beq @e132
            ram[0x38FE] = 0x84;       // 2x hp restored
            return;
        }
        return;
    }

    uint8_t weak = ram[0x2725];
    if ((weak & attack) != 0) {       // and $28a2 / beq @e132
        ram[0x38FE] = 1;              // 1/2x damage
        if ((weak & 0x40) != 0) {     // and #$40 / beq @e132
            ram[0x38FE] = 0x82;       // 1x hp restored
        }
    }
}

// PITFALLS: 1 (DB=$7E assumed for all battle routines)
// HELPERS: none
// CONTRACT:
//   inputs_ram: 0x2725=1, 0x2726=1, 0x28A2=1
//   output_ram: 0x38FE=1
//   entry_mode: mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::CheckStrongElem ($E1:00)