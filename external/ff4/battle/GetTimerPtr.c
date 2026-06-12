#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: cpu->a = offset, ram[$3530] = base pointer (16-bit LE)
// Output: ram[$3598] = base + offset (16-bit LE)
static void GetTimerPtr_c(Snes *snes, uint8_t offset) {
    uint8_t *ram = snes->ram;
    uint16_t base = read16(ram, 0x3530);  // lda $3530 (16-bit)
    uint16_t sum = base + offset;         // clc / adc $3530
    write16(ram, 0x3598, sum);            // sta $3598 (16-bit)
}

// PITFALLS: 1 (DB=$7E required for correct RAM access),
//           6 (A 8-bit on entry, but memory ops are 16-bit via read16/write16)
// HELPERS: read16, write16 (standard 16-bit LE accessors)
// CONTRACT:
//   inputs_reg:  a=8, x=none, y=none
//   inputs_ram:  0x3530=2
//   output_ram:  0x3598=2
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: c=auto, z=auto, n=auto
// REVERSED_FUNCTION: battle::GetTimerPtr ($85:69)