#include "snes/snes.h"

// Entry mode: A 8-bit (inherited), X 16-bit (inherited), DB=$7E, DP=0
// Input: none (no registers or RAM inputs)
// Output: A = random number 0-98 (8-bit), X = same as A
// This function clears A/X to 0, then generates a random number 0-98
static void Rand99_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    // clr_ax → tdc / tax (A = DP = 0, X = A = 0)
    // Since DP=0 (battle convention), clr_ax produces A=0, X=0
    // Then lda #98 sets A=98
    snes->cpu->a = 98;
    // jsr RandXA (expects A=upper bound, returns random 0..A in A and X)
    // RandXA is not yet translated, so delegate
    RandXA_emu(snes);
    // After RTS, A and X both hold the random value (0-98)
    // Caller will read A (8-bit), X is set as side-effect
    // No RAM output — register-only result
}

// PITFALLS: 1 (DB=$7E required for WRAM access if any),
//           5 (clr_ax is tdc/tax, not lda #0/ldx #0)
// HELPERS: RandXA_emu(snes) — delegates RandXA @ $03:8379
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::Rand99 ($03:858B)