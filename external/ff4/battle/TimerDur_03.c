#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM:
//   in : ram[$397B] = base value, ram[$388B] = auto-battle flag
//   out: ram[$A9] = final timer duration (16-bit)
//        (also calls ApplySpeedMod and SetTimerDur which modify other state)
static void TimerDur_03_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xDF] = ram[0x397B];         // lda $397b / sta $df
    ram[0xE1] = 6;                   // lda #$06 / sta $e1
    mult8_emu(snes);                 // jsr Mult8
    uint16_t x = read16(ram, 0xE3);  // ldx $e3
    uint8_t prop = ram[0x100000 + x]; // lda f:AttackProp,x (bank $00 assumed)
    uint8_t delay = prop & 0x1F;     // and #$1f
    uint16_t v = (uint16_t)delay;    // tax
    write16(ram, 0xA9, v);           // stx $a9
    v <<= 1;                         // asl $a9 / rol $aa
    write16(ram, 0xA9, v);
    if (ram[0x388B] != 0) {          // lda $388b / beq @9ebd
        write16(ram, 0xA9, 0);       // clr_ax / stx $a9
    }
    apply_speed_mod_emu(snes);       // jsr ApplySpeedMod
    set_timer_dur_emu(snes);         // jmp SetTimerDur
}

// PITFALLS: 1 (DB required for f:AttackProp access — assumed bank $00),
//           7 (16-bit arithmetic must truncate to 16-bit on shift)
// HELPERS: mult8_emu, apply_speed_mod_emu, set_timer_dur_emu
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x397B=1, 0x388B=1
//   output_ram:  0xA9=2
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::TimerDur_03 ($9E:99)