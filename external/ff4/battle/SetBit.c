#include "snes/snes.h"

// Sets a bit in A using a bit mask from BitOrTbl indexed by X
// Entry: A = value to set bit in, X = bit index (0-7)
// Exit: A = value with bit set
static void SetBit_c(Snes *snes, uint8_t val, uint16_t bit_index) {
    uint8_t *ram = snes->ram;
    // BitOrTbl is at $7E85A0-$7E85A7 (bit masks for bits 0-7)
    uint8_t bit_mask = ram[0x85A0 + bit_index];  // ora f:BitOrTbl,x
    snes->cpu->a = (uint16_t)(val | bit_mask);   // result in A
}

// PITFALLS: 1 (DB must be $7E to access BitOrTbl in WRAM bank)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=8, x=16, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::SetBit ($85:5F)