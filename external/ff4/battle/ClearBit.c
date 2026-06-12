#include "snes/snes.h"

// ClearBit: Clears a bit in A using a bitmask from BitAndTbl indexed by X.
// Entry: A = value, X = bit index (0-7), DB = $7E, DP = 0
// Exit: A = value with bit cleared
static void ClearBit_c(Snes *snes, uint8_t value, uint8_t bit_index) {
    uint8_t *ram = snes->ram;
    snes->cpu->a = value & ram[0x8580 + bit_index];  // and f:BitAndTbl,x
}

// PITFALLS: 1 (DB must be $7E to access BitAndTbl at $8580-$8587)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=8, x=8, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=true, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::ClearBit ($85:5A)