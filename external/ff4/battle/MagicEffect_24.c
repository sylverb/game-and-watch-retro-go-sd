#include "snes/snes.h"

// Entry mode: A 8-bit (inherited), X 16-bit (inherited), DB=$7E, DP=0
// Inputs:
//   $2709, $2707 = 16-bit values (HP before/after or similar)
// Output:
//   $a4 (low) and $a5 (high) = 16-bit difference, high bit set in $a5
static void MagicEffect_24_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    // Compute 16-bit difference with carry set
    uint16_t a = read16(ram, 0x2709);  // lda $2709
    uint16_t b = read16(ram, 0x2707);  // sbc $2707 (carry set)
    uint16_t diff = a - b;             // sec / sbc => subtraction
    write16(ram, 0xA4, diff);          // sta $a4

    // Set high bit of high byte
    ram[0xA5] |= 0x80;                 // ora #$80 / sta $a5

    // Jump to SetMagicStatus (delegate)
    set_magic_status_emu(snes);
}

// PITFALLS: 1 (DB must be $7E for correct RAM access),
//           6 (A mode changes: longa then shorta0),
//           7 (16-bit arithmetic truncation handled by read16/write16)
// HELPERS: set_magic_status_emu(snes)
// CONTRACT:
//   inputs_ram:  0x2707=2, 0x2709=2
//   output_ram:  0xA4=2
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: c=auto
// REVERSED_FUNCTION: battle::MagicEffect_24 ($DD:06)