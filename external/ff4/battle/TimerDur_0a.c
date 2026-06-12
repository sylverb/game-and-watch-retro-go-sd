#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM:
//   in : ram[$397B] = base value
//   out: ram[$A9] = result from Mult8, modified by ApplySpeedMod, then passed to SetTimerDur
static void TimerDur_0a_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0xDF] = ram[0x397B];     // lda $397b / sta $df
    ram[0xE1] = 3;               // lda #$03 / sta $e1
    mult8_emu(snes);             // jsr Mult8
    uint16_t x = read16(ram, 0xE3); // ldx $e3
    write16(ram, 0xA9, x);       // stx $a9
    apply_speed_mod_emu(snes);   // jsr ApplySpeedMod
    set_timer_dur_emu(snes);     // jmp SetTimerDur
}

// PITFALLS: 1 (DB=$7E required for helpers), 8 (A 8-bit, X 16-bit assumed)
// HELPERS: mult8_emu, apply_speed_mod_emu, set_timer_dur_emu
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x397B=1
//   output_ram:  none  // output handled by SetTimerDur
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::TimerDur_0a ($9F:75)