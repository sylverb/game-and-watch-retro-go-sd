#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// All inputs/outputs in WRAM (no register I/O — convention battle):
//   in : ram[$29CD] counter
//   out: ram[$AF] = 5, ram[$B0] = 12, ram[$AD] = ram[$D2]
//        OR jump to SkipAITurn/RandAITarget (no return)
static void AITarget_25_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    uint8_t counter = ram[0x29CD];
    if ((counter - 1) != 0) {     // dec / bne → if (counter != 1)
        ram[0xAF] = 5;
        ram[0xB0] = 12;
        ram[0xAD] = ram[0xD2];
        RandAITarget_emu(snes);   // jmp RandAITarget
    } else {
        SkipAITurn_emu(snes);     // jmp SkipAITurn
    }
}

// PITFALLS: 1 (DB=$7E required for correct RAM access)
// HELPERS: SkipAITurn_emu(snes), RandAITarget_emu(snes)
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  0x29CD=1
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AITarget_25 ($BB:0D)