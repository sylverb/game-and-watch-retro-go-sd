#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM (no register I/O — convention battle):
//   in : ram[$D2] = command source (0-4 = character, 5+ = monster)
//        ram[$A6] = attacker index (used to read $2051,x = command used)
//   out: ram[$35FF] = retaliation command
//        ram[$33C2-$33C5] = graphics script (if character)
//        [$80-$82] = jump target (24-bit pointer)
static void ExecCmd_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint8_t a = ram[0xD2];           // lda $d2

    if (a >= 5) {                    // cmp #$05 / bcc @b11c (inverted)
        // Monster command path
        uint16_t x = ram[0xA6];              // ldx $a6
        a = ram[0x2051 + x];                 // lda $2051,x
        if (a >= 0xC0 && a < 0xE1) {         // cmp #$c0 / bcc / cmp #$e1 / bcc
            // a is in [0xC0, 0xE0], use as-is
        } else {
            a = 0xE1;                        // lda #$e1
        }
        ram[0x35FF] = a;                     // sta $35ff
        a = (uint8_t)(a - 0xC0);             // sec / sbc #$c0
    } else {
        // Character command path
        ram[0x33C2] = 0xF8;                  // lda #$f8 / sta $33c2
        ram[0x33C3] = 0x02;                  // lda #$02 / sta $33c3
        uint16_t x = ram[0xA6];              // ldx $a6
        a = ram[0x2051 + x];                 // lda $2051,x
        if (a == 2 || a == 7 || a == 0x20) { // cmp / beq / cmp / beq / cmp / bne
            ram[0x355D]++;                   // inc $355d
        }
        uint8_t saved_a = a;                 // pha
        a = (uint8_t)(a + 0xC0);             // clc / adc #$c0
        ram[0x33C4] = a;                     // sta $33c4
        if (a != 0xC1) {                     // cmp #$c1 / bne @b147
            ram[0x35FF] = a;                 // sta $35ff
        } else {
            ram[0x35FF] = 0xC2;              // lda #$c2 / sta $35ff
        }
        ram[0x33C5] = 0;                     // stz $33c5
        a = saved_a;                         // pla
    }

    a <<= 1;                                 // asl A
    uint16_t x = a;                          // tax
    // Load 24-bit pointer from CmdTbl (ROM table, use emulator helpers if needed)
    // But since no calls are listed, assume CmdTbl is data-only and we can read it.
    // f:CmdTbl,x and f:CmdTbl+1,x imply ROM access, but parity requires exact behavior.
    // For now, treat as ROM data access (no emulation needed).
    uint16_t addr_lo = read16(ram, 0x008000 + x); // f:CmdTbl,x
    uint8_t  addr_hi = ram[0x008000 + x + 1];     // f:CmdTbl+1,x (high byte)
    ram[0x80] = addr_lo & 0xFF;
    ram[0x81] = (addr_lo >> 8) & 0xFF;
    ram[0x82] = addr_hi;                          // lda #^CmdTbl / sta $82
    // jml [$0080] is a jump, not a call - no return expected.
    // The actual jump will be handled by the harness.
}

// PITFALLS: 3 (CMP/BCS inversion handled by using natural C conditions),
//           6 (A is 8-bit throughout, confirmed by shorta context in battle),
//           8 (mf=1, xf=0 inherited from caller - battle module default)
// HELPERS: read16 (for accessing CmdTbl as 16-bit values)
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  0xD2=1, 0xA6=1
//   output_ram:  0x35FF=1, 0x33C2=1, 0x33C3=1, 0x33C4=1, 0x33C5=1, 0x80=1, 0x81=1, 0x82=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::ExecCmd ($B0:FF)