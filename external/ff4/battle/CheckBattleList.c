#include "snes/snes.h"

// Scans a battle list (indirect pointer at $AB) for a match against
// the 16-bit value at $1800-$1801. Increments $A9 if found.
// Entry mode: A 8-bit (mf=1), X/Y 16-bit (xf=0), DB=$7E, DP=0
static void CheckBattleList_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xA9] = 0;                    // stz $a9
    uint16_t y = 0;                   // clr_ay (A=Y=0, A is 8-bit so Y=0)

    for (;;) {
        uint16_t addr = read16(ram, 0xAB) + y;  // [$ab],y
        uint8_t a = ram[addr];                // lda [$ab],y
        if (a == 0xFF) break;                 // cmp #$ff / beq @8802
        if (a != ram[0x1800]) goto next2;     // cmp $1800 / bne @87fe
        y++;                                  // iny
        addr++;                               // update address for next byte
        if (ram[addr] != ram[0x1801]) {       // lda [$ab],y / cmp $1801 / bne @87ff
            y++;                              // iny
            continue;                         // bra @87e8
        }
        ram[0xA9]++;                          // inc $a9
        return;                               // rts
next2:
        y += 2;                               // iny2 (iny + iny)
    }
}

// PITFALLS: 1 (DB=$7E required for indirect accesses to WRAM),
//           6 (A 8-bit mode assumed from context — no longa/shorta but
//              battle module defaults to 8-bit A)
// HELPERS: read16 (for indirect pointer access)
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  0x00AB=2, 0x1800=1, 0x1801=1
//   output_ram:  0x00A9=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::CheckBattleList ($87:E4)