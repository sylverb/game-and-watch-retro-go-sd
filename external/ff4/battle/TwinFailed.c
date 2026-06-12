#include "snes/snes.h"

// Sets twin failure flags and invokes AddMsg2 to display a message.
// No conditional logic or register inputs — fully deterministic.
static void TwinFailed_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    ram[0x357B] = 0xFF;  // lda #$ff / sta $357b
    ram[0x34CA] = 0x11;  // lda #$11 / sta $34ca
    add_msg2_emu(snes);  // jmp AddMsg2 (delegated)
}

// PITFALLS: 1 (DB=$7E required for AddMsg2 call)
// HELPERS: add_msg2_emu(snes) — delegates AddMsg2 @ $00:85A6
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=auto, xf=auto, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::TwinFailed ($E4:D9)