#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Inputs:
//   ram[$A9] = base duration (16-bit)
//   ram[$3979] = multiplier (16-bit)
// Output:
//   ram[$AB] = modified duration (16-bit)
static void ApplySpeedMod_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint16_t base = read16(ram, 0xA9);
    uint16_t mult = read16(ram, 0x3979);

    write16(ram, 0x393D, base);
    write16(ram, 0x393F, mult);
    mult16_emu(snes); // result in $3941
    uint16_t product = read16(ram, 0x3941);
    write16(ram, 0x3945, product);

    write16(ram, 0x3947, 0x0010);
    div16_emu(snes); // result in $3949
    uint16_t result = read16(ram, 0x3949);
    write16(ram, 0xAB, result);
}

// PITFALLS: 1 (DB must be $7E for correct memory access)
// HELPERS: mult16_emu(snes), div16_emu(snes)
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0xa9=2, 0x3979=2
//   output_ram:  0xab=2
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::ApplySpeedMod ($9F:D8)