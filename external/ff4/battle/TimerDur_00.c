#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM:
//   in : ram[$AB] (timer duration, modified by ApplySpeedMod)
//   out: ram[$AB] (final timer duration, >= 1)
// CALLER MUST ensure Z flag reflects the pre-call value of $AB,
// because the routine starts with `bne` (consults Z).
static void TimerDur_00_c(Snes *snes) {
    apply_speed_mod_emu(snes);      // jsr ApplySpeedMod
    uint8_t ab = snes->ram[0xAB];
    if (ab == 0) {                  // bne @9e6e → taken if $ab != 0
        snes->ram[0xAB] = 1;        // inc $ab (min 1)
    }
    set_timer_dur_emu(snes);        // jmp SetTimerDur
}

// PITFALLS: 1 (DB=$7E required for WRAM access),
//           2 (Z flag must reflect $AB on entry for `bne` correctness)
// HELPERS: apply_speed_mod_emu(snes), set_timer_dur_emu(snes)
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0xAB=1
//   output_ram:  0xAB=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::TimerDur_00 ($9E:65)