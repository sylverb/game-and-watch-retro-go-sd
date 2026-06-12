#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM:
//   in : ram[$16A8] = row setting
//   out: ram[$2001], ram[$2081], ram[$2101], ram[$2181], ram[$2201] updated
static void InitCharRows_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint8_t row_setting = ram[0x16A8];

    if (row_setting == 0) {           // bne @95fc
        ram[0x2001] &= 0x7F;          // and #$7f / sta $2001
        ram[0x2081] &= 0x7F;          // and #$7f / sta $2081
        ram[0x2101] &= 0x7F;          // and #$7f / sta $2101
        ram[0x2181] |= 0x80;          // ora #$80 / sta $2181
        ram[0x2201] |= 0x80;          // ora #$80 / sta $2201
    } else {
        ram[0x2001] |= 0x80;          // ora #$80 / sta $2001
        ram[0x2081] |= 0x80;          // ora #$80 / sta $2081
        ram[0x2101] |= 0x80;          // ora #$80 / sta $2101
        ram[0x2181] &= 0x7F;          // and #$7f / sta $2181
        ram[0x2201] &= 0x7F;          // and #$7f / sta $2201
    }
}

// PITFALLS: 1 (DB=$7E required for WRAM access)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x16A8=1
//   output_ram:  0x2001=1, 0x2081=1, 0x2101=1, 0x2181=1, 0x2201=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::InitCharRows ($95:CE)