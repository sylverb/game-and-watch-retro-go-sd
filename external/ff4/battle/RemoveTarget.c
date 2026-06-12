#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: ram[$3554] determines target list, ram[$CE] bit 7 marks a target
// Logic:
//   if $3554 == 0:
//     clear bit (A & $7F) in $3550 (targets)
//   else:
//     clear bit (A & $7F) in $3523 (targets reflected onto)
static void RemoveTarget_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint8_t a = ram[0x3554];
    uint8_t bit = ram[0xCE] & 0x7F;

    if (a != 0) {
        // lda $ce / and #$7f / tax
        // lda $3523 / jsr ClearBit / sta $3523
        ram[0xCE] = bit; // Set up X register input for ClearBit
        ram[0xA4] = ram[0x3523]; // Set up A register input for ClearBit
        ClearBit_emu(snes); // delegate ClearBit
        ram[0x3523] = ram[0xA4]; // Store result back
    } else {
        // lda $ce / and #$7f / tax
        // lda $3550 / jsr ClearBit / sta $3550
        ram[0xCE] = bit; // Set up X register input for ClearBit
        ram[0xA4] = ram[0x3550]; // Set up A register input for ClearBit
        ClearBit_emu(snes); // delegate ClearBit
        ram[0x3550] = ram[0xA4]; // Store result back
    }
}

// PITFALLS: 1 (DB=$7E required for WRAM access), 8 (mode A 8-bit assumed)
// HELPERS: ClearBit_emu(snes) — delegates ClearBit @ $03:855A
// CONTRACT:
//   inputs_ram: 0x3554=1, 0xCE=1
//   output_ram: 0x3550=1, 0x3523=1
//   entry_mode: mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::RemoveTarget ($E0:30)