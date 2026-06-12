#include "snes/snes.h"

// Entry mode: A 8-bit (mf=1), X 16-bit (xf=0), DB=$7E, DP=0
// This function saves $269C and $2729, sets them to specific values,
// calls DoFightCmd, then restores the original values.
static void Cmd_0c_c(Snes *snes) {
    uint8_t *ram = snes->ram;
    // Save original values
    uint8_t saved_269c = ram[0x269C];
    uint8_t saved_2729 = ram[0x2729];

    // Push values to stack (emulating pha)
    ram[0x100 + snes->cpu->sp--] = saved_2729;
    ram[0x100 + snes->cpu->sp--] = saved_269c;

    // Set new values: $2729 = 0, $269C = 0xFF
    ram[0x2729] = 0;
    ram[0x269C] = 0xFF;

    // Call subroutine
    do_fight_cmd_emu(snes);

    // Restore original values from stack (emulating pla)
    ram[0x269C] = ram[0x100 + ++snes->cpu->sp];
    ram[0x2729] = ram[0x100 + ++snes->cpu->sp];
}

// PITFALLS: 1 (DB must be $7E for correct RAM access), 4 (stack addressing depends on E flag)
// HELPERS: do_fight_cmd_emu(snes)
// CONTRACT:
//   inputs_reg:  a=none, x=none, y=none
//   inputs_ram:  0x269c=1, 0x2729=1
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::Cmd_0c ($E8:39)