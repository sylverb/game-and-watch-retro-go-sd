#include "snes/snes.h"

// 16-bit multiplication: result = arg1 * arg2 (all 16-bit, unsigned)
// Inputs:  ram[$393D] = multiplicand, ram[$393F] = multiplier
// Outputs: ram[$3941] = low 16 bits of product, ram[$3943] = high 16 bits
// Entry mode: A 16-bit (mf=0), X 16-bit (xf=0), DB=$7E, DP=0
static void Mult16_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint16_t multiplicand = read16(ram, 0x393D);
    uint16_t multiplier = read16(ram, 0x393F);
    uint32_t product = (uint32_t)multiplicand * multiplier;
    write16(ram, 0x3941, (uint16_t)(product & 0xFFFF));      // low word
    write16(ram, 0x3943, (uint16_t)((product >> 16) & 0xFFFF)); // high word
}

// PITFALLS: 1 (DB=$7E required for correct RAM access)
// HELPERS: read16/write16 for 16-bit little-endian access
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  0x393D=2, 0x393F=2
//   output_ram:  0x3941=2, 0x3943=2
//   entry_mode:  mf=false, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::Mult16 ($03:83B9)