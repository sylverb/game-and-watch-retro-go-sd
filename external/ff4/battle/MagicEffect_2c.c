#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM:
//   in : ram[$2707-$2708] 16-bit value
//   out: ram[$a4-$a5] = (ram[$2707-$2708] / 3) | 0x8000
static void MagicEffect_2c_c(Snes *snes) {
    uint8_t *ram = snes->ram;

    // Load 16-bit value from $2707-$2708 into $3945-$3946
    uint16_t val = read16(ram, 0x2707);
    write16(ram, 0x3945, val);

    // Set divisor to 3 in $3947
    write16(ram, 0x3947, 3);

    // Call Div16: result in $3949-$394A
    div16_emu(snes);  // jsr Div16

    // Check if quotient >= $270F, cap it if so
    uint16_t quotient = read16(ram, 0x3949);
    if (quotient >= 0x270F) {
        quotient = 0x270F;
        write16(ram, 0x3949, quotient);
    }

    // Store result in $a4-$a5 with bit 15 set
    ram[0xA4] = quotient & 0xFF;
    ram[0xA5] = ((quotient >> 8) & 0x7F) | 0x80;
}

// PITFALLS: 1 (DB must be $7E for correct RAM access),
//           6 (A 8-bit mode assumed from context),
//           8 (X 16-bit mode for stx operations)
// HELPERS: div16_emu(snes) — delegates Div16 @ $03:83E6
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x2707=2, 0x2708=2
//   output_ram:  0xA4=1, 0xA5=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::MagicEffect_2c ($DD:65)