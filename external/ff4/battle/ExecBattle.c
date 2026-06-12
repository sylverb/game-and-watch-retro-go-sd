#include "snes/snes.h"

// Entry mode: A 16-bit (mf=0), X 16-bit (xf=0), DB=0, DP=0 (inherited)
// This function is a setup/teardown wrapper that configures hardware registers
// and calls InitBattle. It preserves all registers across the call.
static void ExecBattle_c(Snes *snes) {
    Cpu *cpu = snes->cpu;
    uint8_t *ram = snes->ram;

    // Save processor status and all registers
    uint16_t saved_flags = cpu->mf | (cpu->xf << 1) | (cpu->c << 8) | (cpu->z << 9) | (cpu->v << 10) | (cpu->n << 11);
    uint16_t saved_a = cpu->a;
    uint16_t saved_x = cpu->x;
    uint16_t saved_y = cpu->y;
    uint16_t saved_dp = cpu->dp;
    uint8_t  saved_db = cpu->db;
    uint16_t saved_sp = cpu->sp;

    // Set up for InitBattle call
    cpu->mf = false;  // longa
    cpu->xf = false;  // longi
    cpu->a = 0;       // lda #0
    cpu->mf = true;   // shorta
    cpu->xf = false;  // longi (already set)

    // Call InitBattle
    init_battle_emu(snes);

    // Write hardware registers (all zero)
    ram[0x4200] = 0;  // hNMITIMEN
    ram[0x420B] = 0;  // hMDMAEN
    ram[0x420C] = 0;  // hHDMAEN
    ram[0x2100] = 0;  // hINIDISP

    // Restore registers and processor status
    cpu->a = saved_a;
    cpu->x = saved_x;
    cpu->y = saved_y;
    cpu->dp = saved_dp;
    cpu->db = saved_db;
    cpu->sp = saved_sp;
    cpu->mf = (saved_flags & 1) != 0;
    cpu->xf = (saved_flags & 2) != 0;
    cpu->c = (saved_flags >> 8) & 1;
    cpu->z = (saved_flags >> 9) & 1;
    cpu->v = (saved_flags >> 10) & 1;
    cpu->n = (saved_flags >> 11) & 1;
}

// PITFALLS: 1 (DB=0 assumed, hardware registers accessed via $00:xxxx),
//           8 (mode A/X inheritance from caller)
// HELPERS: init_battle_emu(snes)
// CONTRACT:
//   inputs_reg:  none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=false, xf=false, dp=0x0, db=0x0
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::ExecBattle ($80:0009)