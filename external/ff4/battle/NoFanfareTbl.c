#include "snes/snes.h"

// NoFanfareTbl is a lookup table of 7 16-bit values followed by a $ff terminator.
// It is accessed by index in other routines, returning the corresponding value.
// Entry mode: A 16-bit (mf=0), X 16-bit (xf=0), DB=$7E, DP=0
// Input: X = index (0-6), each entry is 2 bytes, so offset = index * 2
// Output: A = value from table (16-bit)
static uint16_t NoFanfareTbl_c(Snes *snes, uint16_t index) {
    // Table data: dc 00 dd 00 e1 00 e7 00 a7 01 af 01 b6 01 ff
    static const uint16_t table[] = { 0x00dc, 0x00dd, 0x00e1, 0x00e7, 0x01a7, 0x01af, 0x01b6 };
    if (index >= 7) return 0xffff;  // out of bounds, return $ff as 16-bit
    return table[index];
}

// PITFALLS: none (this is a data table, not a code routine)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  x=16
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=false, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::NoFanfareTbl ($FE:67)