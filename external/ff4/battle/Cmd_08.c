#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM (no register I/O — convention battle):
//   in : ram[$2683] (bit 0x20 checked)
//   out: ram[$34CA] = spell_id_lo
//        ram[$26D2] = spell_id_hi
//        ram[$34C8] = command name (0x08)
//        ram[$34C7] = show command name flag (0x10)
static void Cmd_08_c(Snes *snes) {
    uint8_t *ram = snes->ram;

    // Check bit 0x20 of $2683
    if (ram[0x2683] & 0x20) {
        ram[0x34CA] = 0x0B;
        ram[0x26D2] = 0x19;
    } else {
        uint8_t r = rand_emu(snes);  // jsr Rand

        if (r < 0xC0) {
            if (r < 0x80) {
                if (r < 0x40) {
                    ram[0x34CA] = 0x0C;
                    ram[0x26D2] = 0x29;
                } else {
                    ram[0x34CA] = 0x0D;
                    ram[0x26D2] = 0x03;
                }
            } else {
                ram[0x34CA] = 0x0E;
                ram[0x26D2] = 0x02;
            }
        } else {
            ram[0x34CA] = 0x00;
            ram[0x26D2] = 0x00;
        }
    }

    do_magic_attack_emu(snes);       // jsr DoMagicAttack
    ram[0x34C8] = 0x08;              // lda #$08 / sta $34c8
    ram[0x34C7] = 0x10;              // lda #$10 / sta $34c7
    add_msg3_emu(snes);              // jmp AddMsg3
}

// PITFALLS: 1 (DB=$7E required for WRAM access)
// HELPERS: rand_emu(snes), do_magic_attack_emu(snes), add_msg3_emu(snes)
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x2683=1
//   output_ram:  0x34CA=1, 0x26D2=1, 0x34C8=1, 0x34C7=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::Cmd_08 ($E9:03)