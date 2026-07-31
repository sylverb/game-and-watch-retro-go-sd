// Idle-loop table for the gpSP core — generated in game-and-what, copied here.
//
// gpSP has no automatic idle-loop detection: gba_memory.c defaults
// idle_loop_target_pc to 0xFFFFFFFF and only overrides it when the cart's 4-char code
// is in its own hand-maintained table. A game absent from that table busy-waits through
// the whole frame and cannot reach full speed on the M7.
//
// gpSP's table is also wrong in places, and a Korean patch defeats it entirely — the
// patch keeps the original cart header, so gpSP applies the ORIGINAL game's address,
// which the patch has moved. Nothing in the filename, header or region warns you.
//
// So we override it. gpSP exposes the target as a plain extern, no fork required:
//
//     extern u32 idle_loop_target_pc;
//
//     const u32 pc = gba_idle_loop_lookup(rom + 0xAC);   // the gamepak code
//     if (pc) {
//         idle_loop_target_pc = pc;                      // our measured value wins
//     }
//
// Every address was measured by RUNNING the rom (game-and-what: scripts/idlefind),
// and only kept when gpSP demonstrably skips on it. See docs/GBA_FIRMWARE_HANDOFF.md.
#ifndef GBA_IDLE_LOOP_H
#define GBA_IDLE_LOOP_H

#include <stdint.h>
#include <stddef.h>

// The backward branch that closes the game's VBlank wait loop — what gpSP compares the
// PC against. 0 if we have not measured this game: leave gpSP's own table alone.
uint32_t gba_idle_loop_lookup(const char *gamepak_code);

// Measured CPU work per frame, in GBA cycles out of 280896, with the skip active.
// The M7 leaves the CPU roughly 160000 of them at a 340MHz OC, so a game above that
// cannot hold 60fps however well its idle loop is skipped. 0 if not measured.
// NOTE: a cycle-budget estimate from an emulator — never checked on real hardware.
uint32_t gba_exec_cycles_lookup(const char *gamepak_code);

#endif  // GBA_IDLE_LOOP_H
