#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: ram[$3558] = timer condition (0 or non-zero)
// Output: $a9 = 0 if $3558 == 0, else 1; then ApplySpeedMod and SetTimerDur
static void TimerDur_02_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint16_t x;

    if (ram[0x3558] != 0) {  // bne @9e7a
        x = 1;
    } else {
        x = 0;  // clr_ax → tdc / tax (D=0 in battle → A/X = 0)
    }
    write16(ram, 0xA9, x);   // stx $a9 (X is 16-bit)
    apply_speed_mod_emu(snes); // jsr ApplySpeedMod
    set_timer_dur_emu(snes);   // jmp SetTimerDur (delegate tail call)
}

// PITFALLS: 1 (DB=$7E for writes to $A9), 8 (X is 16-bit per xf=0)
// HELPERS: apply_speed_mod_emu(snes), set_timer_dur_emu(snes)
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x3558=1
//   output_ram:  0xA9=2
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::TimerDur_02 ($9E:71)