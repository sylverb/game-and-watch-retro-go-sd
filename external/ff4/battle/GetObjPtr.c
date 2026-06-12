#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: cpu->a = object_id (7-bit index + monster flag in bit 7)
// Output: ram[$80-$81] = pointer to object (16-bit LE)
static void GetObjPtr_c(Snes *snes, uint8_t obj_id) {
    uint8_t *ram = snes->ram;
    uint8_t index = obj_id & 0x7F;       // and #$7f
    ram[0xDF] = index;
    ram[0xE1] = 0x80;                    // lda #$80 / sta $e1
    mult8_emu(snes);                     // jsr Mult8 (delegated)

    // Entry Z/N flags not required: routine starts with `pla / bmi`,
    // which sets flags from the accumulator value.
    if (obj_id & 0x80) {                 // bmi @b180 (branch if monster)
        // Monster case
        uint16_t addr = read16(ram, 0xE3);
        addr += 0x80;                    // adc #$80
        write16(ram, 0x80, addr);        // sta $80 (16-bit store)
        addr = read16(ram, 0xE3) + 0x80;
        uint8_t hi = (addr >> 8) + 0x22; // adc #$22 with carry from low byte
        ram[0x81] = hi;
    } else {
        // Player case
        uint16_t addr = read16(ram, 0xE3);
        addr += 0x00;                    // adc #$00
        write16(ram, 0x80, addr);        // sta $80 (16-bit store)
        addr = read16(ram, 0xE3) + 0x00;
        uint8_t hi = (addr >> 8) + 0x20; // adc #$20 with carry from low byte
        ram[0x81] = hi;
    }
}

// PITFALLS: 1 (DB=$7E required for Mult8), 7 (arithmetic truncation in 8-bit mode)
// HELPERS: mult8_emu(snes) — delegates Mult8 @ $03:83E0
// CONTRACT:
//   inputs_reg:  a=8, x=none, y=none
//   inputs_ram:  none
//   output_ram:  0x80=2
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::GetObjPtr ($B1:63)