#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM:
//   in : ram[$397B] = base value (8-bit)
//   out: ram[$A9..$AA] = final timer duration (16-bit)
static void TimerDur_08_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint8_t base = ram[0x397B];         // lda $397b
    ram[0xAD] = base;                   // sta $ad
    ram[0xAE] = 0;                      // stz $ae
    uint8_t ad = (uint8_t)(base << 1);  // asl $ad (8-bit, truncates)
    ram[0xAD] = ad;
    uint8_t ae = (uint8_t)(base >> 7);  // rol $ae (carry from asl)
    ram[0xAE] = ae;
    uint16_t sum = (uint16_t)ad + 0x1E; // clc / adc #$1e
    ram[0xA9] = sum & 0xFF;             // sta $a9
    uint8_t ae_sum = (uint8_t)(ae + (sum > 0xFF ? 1 : 0)); // adc #$00 with carry
    ram[0xAA] = ae_sum;                 // sta $aa
    apply_speed_mod_emu(snes);          // jsr ApplySpeedMod
    set_timer_dur_emu(snes);            // jmp SetTimerDur
}

// PITFALLS: 1 (DB=$7E), 7 (8-bit shifts truncate), 8 (A 8-bit, X 16-bit)
// HELPERS: apply_speed_mod_emu, set_timer_dur_emu
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x397B=1
//   output_ram:  0xA9=2
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::TimerDur_08 ($9F:1C)