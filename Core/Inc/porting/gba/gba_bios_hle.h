/* BIOS SWI high-level emulation — host-side replacements for the common
 * Nintendo BIOS routines.
 *
 * Same contract as the M4A mixer HLE: run the MEANING on the host, charge the
 * guest the cycles the real BIOS would have spent (approximately), leave the
 * observable machine state bit-compatible with interpreting the open/official
 * BIOS. Declining (return 0) is always safe — the caller then enters the BIOS
 * at 0x08 as usual.
 *
 * Not handled here (need real Halt / IRQ / reset semantics):
 *   SoftReset, RegisterRamReset, Halt, Stop, IntrWait, VBlankIntrWait,
 *   CustomHalt, SoundDriver*, MultiBoot, HardReset.
 */
#ifndef GBA_BIOS_HLE_H
#define GBA_BIOS_HLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Try to service SWI `number`. `regs` is gpSP's reg[] (r0..r15+). On success
 * writes results into r0..r3 (and occasionally r12), sets *cycles to the guest
 * cost, and returns 1. Returns 0 if this SWI should fall through to the BIOS. */
int gba_bios_hle(unsigned number, unsigned *regs, int *cycles);

#ifdef __cplusplus
}
#endif

#endif /* GBA_BIOS_HLE_H */
