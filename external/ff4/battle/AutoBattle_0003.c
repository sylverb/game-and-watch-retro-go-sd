#include "snes/snes.h"

// This routine is a data script, not a code routine. It contains a sequence
// of 2-byte command entries (command_id, target_flags) followed by 0xFF.
// It is interpreted by a script runner elsewhere in the battle system.
//
// No execution logic is present — this is pure data. The C translation
// is a placeholder that does nothing, as the script is read directly
// by the interpreter loop in battle scripting code.
static void AutoBattle_0003_c(Snes *snes) {
    // Script data:
    //   $CE,$00 = use command "kick"
    //   $C0,$00 = use command "fight"
    //   $FF     = end of script
    //
    // No execution required; consumed by scripting engine.
    (void)snes;
}

// PITFALLS: none (data-only)
// HELPERS: none
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=true, xf=false, dp=0x0, db=0x7E
//   entry_flags: none
// REVERSED_FUNCTION: battle::AutoBattle_0003 ($FE:0036)