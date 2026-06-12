#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X/Y 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM (no register I/O — convention battle):
//   in : ram[$2683-$2685] command flags, ram[$26D0-$26D5] command data
//   out: ram[$34C7-$34C8] item display, ram[$33C5] backup item (if needed)
//        ram[$352A] incremented if summon blocked
static void Cmd_01_c(Snes *snes) {
    uint8_t *ram = snes->ram;

    // Check command flags
    if ((ram[0x2683] & 0xC0) != 0) return;  // bne @ec11
    if ((ram[0x2684] & 0x3C) != 0) return;  // bne @ec11
    if ((ram[0x2685] & 0xC6) != 0) return;  // bne @ec11

    uint8_t saved_a = ram[0x26D2];          // pha (save original value)
    uint8_t a = ram[0x26D2];

    if (a == 0xCA) {                        // cmp #$ca / bne @ebc7
        rand_summon_emu(snes);              // jsr RandSummon
        // fall through to DoMagicAttack
    } else {
        if (a >= 0xB0) {                    // bcc @ebd2
            if ((ram[0x26D0] & 0x10) != 0) { // lda $26d0 / and #$10 / beq @ebf7
                ram[0x352A]++;              // inc $352a
                do_magic_attack_emu(snes);  // jsr DoMagicAttack
                if (ram[0x38ED] == 0) {     // lda $38ed / beq @ec05
                    ram[0x33C5] = saved_a;  // pla / sta $33c5
                }
                ram[0x34C8] = saved_a;      // sta $34c8
                ram[0x34C7] = 0x20;         // sta $34c7
                return;
            }
        }

        write16(ram, 0x80, read16(ram, 0x26D5)); // ldx $26d5 / stx $80
        a = ram[0x26D2];

        if (a >= 0x61) {                    // cmp #$61 / bcc @ebe2
            a = 0;                          // lda #$00
        } else {
            a = ram[0x10000 + 0x8580 + a];  // lda f:WeaponMagicHits,x (assumed bank $00)
            ram[0x38EC] = a;                // sta $38ec
            ram[0x38EB]++;                  // inc $38eb
            a = ram[read16(ram, 0x80) + 3]; // ldy #$0003 / lda ($80),y
        }

        ram[0x26D2] = a;                    // sta $26d2
        // fall through to DoMagicAttack
    }

    do_magic_attack_emu(snes);              // jsr DoMagicAttack

    if (ram[0x38ED] != 0) {                 // lda $38ed / beq @ec05
        ram[0x34C8] = saved_a;              // pla / sta $34c8
    } else {
        ram[0x33C5] = saved_a;              // pla / sta $33c5
        ram[0x34C8] = saved_a;              // sta $34c8
    }

    ram[0x34C7] = 0x20;                     // sta $34c7
}

// PITFALLS: 1 (DB=$7E required for absolute accesses),
//           6 (mode A assumed 8-bit based on context and short routines),
//           8 (X/Y 16-bit assumed from battle module convention)
// HELPERS: rand_summon_emu(snes), do_magic_attack_emu(snes)
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  0x2683=1, 0x2684=1, 0x2685=1, 0x26d0=1, 0x26d2=1, 0x26d5=2
//   output_ram:  0x34c7=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: auto
// REVERSED_FUNCTION: battle::Cmd_01 ($EB:A2)