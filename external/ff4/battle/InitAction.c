#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: $d2 = acting entity index
// Logic:
//   1. Store acting entity in $38f6, increment and wrap at 0x0D
//   2. Compute $a9 = $d3 * 3
//   3. Get timer pointer for that value
//   4. Check $2a06,x for action/timer flags to determine action type:
//      - If ($2a06,x & 0x7E) == 0:
//          - If $d2 < 5: action = 0 (character action)
//          - Else:       action = 1 (monster action)
//      - Else if ($2a06,x & 0x08) != 0:
//          - action = 2 (do attack)
//      - Else:
//          - action = 3 (do timer effect)
//   5. Store action type in $352e, clear $d1, return
static void InitAction_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint8_t d2 = ram[0xD2];
    ram[0x38F6] = d2;
    select_obj_emu(snes);              // jsr SelectObj
    ram[0x38F6]++;                     // inc $38f6
    if (ram[0x38F6] == 0x0D) {         // cmp #$0d / bne @97c8
        ram[0x38F6] = 0;               // stz $38f6
    }
    uint8_t d3 = ram[0xD3];
    uint8_t a9 = (uint8_t)((d3 << 1) + d3); // asl / adc $d3 (8-bit truncation)
    ram[0xA9] = a9;
    get_timer_ptr_emu(snes);           // jsr GetTimerPtr
    uint16_t x = read16(ram, 0x3598);
    uint8_t timer_flags = ram[0x2A06 + x];
    if ((timer_flags & 0x7E) != 0) {   // and #$7e / bne @97ed
        if ((timer_flags & 0x08) != 0) { // and #$08 / beq @97f5
            ram[0x352E] = 2;           // lda #$02 / sta $352e
        } else {
            ram[0x352E] = 3;           // lda #$03 / sta $352e
        }
    } else {
        if (d2 < 5) {                  // cmp #$05 / bcc @97e9
            ram[0x352E] = 0;           // lda #$00 / sta $352e
        } else {
            ram[0x352E] = 1;           // lda #$01 / sta $352e
        }
    }
    ram[0xD1] = 0;                     // stz $d1
}

// PITFALLS: 1 (DB=$7E for WRAM access), 6 (8-bit A mode assumed),
//           7 (8-bit arithmetic truncation on shifts/adds)
// HELPERS: select_obj_emu(snes), get_timer_ptr_emu(snes)
// CONTRACT:
//   inputs_ram:  0xd2=1, 0xd3=1
//   output_ram:  0x352e=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::InitAction ($97:B3)