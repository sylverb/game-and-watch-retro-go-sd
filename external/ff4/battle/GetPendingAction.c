#include "snes/snes.h"

// Scans through characters/monsters and their timers to find the next
// pending action. Sets $d1=1 if a timer expired, and $ad/$ae hold the
// index of the entity and timer respectively.
//
// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All state in WRAM; no register inputs or outputs.
static void GetPendingAction_c(Snes *snes) {
    uint8_t *ram = snes->ram;

    ram[0xD1] = 0;  // disable pending action
    ram[0x00] = 0;  // loop counter
    uint8_t a9 = ram[0x38F6];  // character/monster with priority
    ram[0xA9] = a9;

loop_974a:;
    uint8_t a = ram[0x3601];
    if (a == 0xFF) goto check_match;
    if (a != a9) goto next_character;

check_match:
    ram[0xAD] = 0;  // timer index
    ram[0xAE] = 0;
    a = ram[0xA9];
    uint16_t x = (uint16_t)(a << 1);
    uint8_t ab = ram[0x29EB + x];  // enabled timers
    ram[0xAB] = ab;

loop_9762:
    ab = ram[0xAB];
    uint8_t carry = (ab >> 7) & 1;
    ram[0xAB] = (uint8_t)(ab << 1);  // asl $ab
    if (carry == 0) goto no_carry;   // bcc @976d

    check_timer_emu(snes);           // jsr CheckTimer
    if (ram[0xD1] != 0) return;      // bne @9787 (timer expired)

no_carry:
    ram[0xAD]++;                     // inc $ad
    if (ram[0xAD] != 7) goto loop_9762;  // cmp #$07 / bne @9762

next_character:
    ram[0xA9]++;                     // inc $a9
    if (ram[0xA9] == 13) ram[0xA9] = 0;  // cmp #$0d / bne @977f / stz
    ram[0x00]++;                     // inc $00
    if (ram[0x00] != 13) goto loop_974a;  // cmp #$0d / bne @974a
}

// PITFALLS: 1 (DB=$7E required for WRAM access), 6 (A is 8-bit throughout),
//           7 (ASL truncation to 8 bits)
// HELPERS: check_timer_emu(snes) — delegates CheckTimer @ $97:88
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  0x38f6=1, 0x3601=1
//   output_ram:  0xd1=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::GetPendingAction ($97:41)