#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// Entry: ram[$38D3] is tested for zero
// Logic:
//   if (ram[$38D3] != 0) {
//     ram[$DE]++;
//   }
//   return
static void AICond_09_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    if (ram[0x38D3] != 0) {  // beq @beeb → taken when zero
        ram[0xDE]++;         // inc $de
    }
}

// PITFALLS: 1 (DB=$7E required for correct absolute addressing)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x38D3=1
//   output_ram:  0xDE=1
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::AICond_09 ($BE:E4)