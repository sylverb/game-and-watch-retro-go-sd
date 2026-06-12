#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// This function checks a timer and sets a flag if a pending action should occur.
// It reads from WRAM and calls two helper functions to select an object and get a timer pointer.
static void CheckTimer_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    Cpu *cpu = snes->cpu;

    // lda $a9 / sta $d2
    ram[0xD2] = ram[0xA9];

    // jsr SelectObj
    select_obj_emu(snes);

    // lda $ad / sta $d3
    ram[0xD3] = ram[0xAD];

    // asl / clc / adc $ad / sta $af
    uint8_t ad_val = ram[0xAD];
    uint8_t doubled = (uint8_t)(ad_val << 1); // asl (8-bit)
    uint8_t tripled = (uint8_t)(doubled + ad_val); // clc / adc (8-bit)
    ram[0xAF] = tripled;

    // lda $af / jsr GetTimerPtr
    cpu->a = ram[0xAF];
    cpu->z = (cpu->a == 0);
    cpu->n = (cpu->a & 0x80) != 0;
    get_timer_ptr_emu(snes);

    // ldx $3598
    cpu->x = read16(ram, 0x3598);

    // lda $2a04,x / ora $2a05,x / bne @97b2
    uint16_t base = 0x2A04 + cpu->x;
    uint8_t low = ram[base];
    uint8_t high = ram[base + 1];
    if ((low | high) != 0) return; // bne @97b2

    // lda $2a06,x / and #$01 / beq @97b2
    uint8_t byte_2a06 = ram[base + 2];
    if ((byte_2a06 & 1) == 0) return; // beq @97b2

    // inc $d1
    ram[0xD1]++;
}

// PITFALLS: 1 (DB must be $7E for WRAM access), 2 (flags Z/N set before GetTimerPtr call),
//           7 (8-bit arithmetic truncation on ASL/ADC chain)
// HELPERS: select_obj_emu(snes), get_timer_ptr_emu(snes)
// CONTRACT:
//   inputs_ram: 0xA9=1, 0xAD=1
//   output_ram: 0xD1=1
//   entry_mode: mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::CheckTimer ($97:88)