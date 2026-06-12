#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: X = entity index (16-bit), ram[$3558] = flag
// Logic:
//   if (ram[$3558] != 0)
//       base = ram[$202F + X]
//   else
//       base = ram[$2016 + X]
//   value = base + 0x14
//   ram[$A9] = value
//   ApplySpeedMod()
//   SetTimerDur()
static void TimerDur_07_c(Snes *snes, uint16_t x) {
    uint8_t *ram = snes->ram;
    uint8_t base;
    if (ram[0x3558] != 0) {              // beq @9f0d
        base = ram[0x202F + x];          // lda $202f,x
    } else {
        base = ram[0x2016 + x];          // lda $2016,x
    }
    uint8_t sum = (uint8_t)(base + 0x14); // clc / adc #$14 (8-bit)
    uint16_t value = (uint16_t)sum;
    write16(ram, 0xA9, value);           // tax / stx $a9
    apply_speed_mod_emu(snes);           // jsr ApplySpeedMod
    set_timer_dur_emu(snes);             // jmp SetTimerDur
}

// PITFALLS: 1 (DB=$7E required for WRAM access), 7 (8-bit ADC truncation)
// HELPERS: apply_speed_mod_emu(snes), set_timer_dur_emu(snes)
// CONTRACT:
//   inputs_reg:  a=none, x=16, y=none
//   inputs_ram:  0x3558=1, 0x202f=1, 0x2016=1
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::TimerDur_07 ($9F:03)