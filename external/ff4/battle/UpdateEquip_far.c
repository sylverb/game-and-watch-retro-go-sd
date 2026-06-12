#include "snes/snes.h"

// This function is a far call wrapper that sets up the environment for UpdateEquip.
// It saves all registers and CPU state, switches to native mode with 16-bit A and I,
// sets up the data bank to $7E, calls UpdateEquip, then restores everything.
static void UpdateEquip_far_c(Snes *snes, uint16_t a) {
    Cpu *cpu = snes->cpu;
    uint8_t *ram = snes->ram;
    
    // Save current state
    uint16_t old_flags = (cpu->n << 7) | (cpu->v << 6) | (cpu->mf << 5) | 
                         (cpu->xf << 4) | (cpu->d << 3) | (cpu->i << 2) | 
                         (cpu->z << 1) | cpu->c;
    uint8_t old_db = cpu->db;
    uint16_t old_dp = cpu->dp;
    uint16_t old_a = cpu->a;
    uint16_t old_x = cpu->x;
    uint16_t old_y = cpu->y;
    uint16_t old_sp = cpu->sp;
    
    // Store A in $7E:3975
    write16(ram, 0x3975, a);
    
    // Set up new state (mimicking the assembly)
    cpu->mf = false;  // longa
    cpu->xf = false;  // longi
    cpu->db = 0x7E;   // data bank = $7E
    cpu->dp = 0;      // direct page = 0
    cpu->i = true;    // sei
    
    // Call UpdateEquip
    update_equip_emu(snes);
    
    // Restore previous state
    cpu->n = (old_flags >> 7) & 1;
    cpu->v = (old_flags >> 6) & 1;
    cpu->mf = (old_flags >> 5) & 1;
    cpu->xf = (old_flags >> 4) & 1;
    cpu->d = (old_flags >> 3) & 1;
    cpu->i = (old_flags >> 2) & 1;
    cpu->z = (old_flags >> 1) & 1;
    cpu->c = old_flags & 1;
    cpu->db = old_db;
    cpu->dp = old_dp;
    cpu->a = old_a;
    cpu->x = old_x;
    cpu->y = old_y;
    cpu->sp = old_sp;
}

// PITFALLS: 1 (DB must be $7E for WRAM access), 8 (mode A/X inheritance - 
// this routine explicitly sets its own mode rather than inheriting)
// HELPERS: update_equip_emu(snes)
// CONTRACT:
//   inputs_reg:  a=16, x=none, y=none
//   inputs_ram:  none
//   output_ram:  none
//   entry_mode:  mf=auto, xf=auto, dp=auto, db=auto
//   entry_flags: z=auto, n=auto
// REVERSED_FUNCTION: battle::UpdateEquip_far ($80:0036)