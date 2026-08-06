/*
 * Generic core entry trampoline.
 *
 * Same convention as PICO-8's .pico8_entry (see
 * docs/PICO8_EXTERNAL_MODULE.md, "Entry Point"): the firmware never links
 * against any symbol from the core, it just jumps to
 * (__RAM_EMU_START__ | 1) after loading the payload — see
 * run_dynamic_core() in Core/Src/retro-go/rg_emulators.c. The core's linker
 * script (cores/_template/core_ram_emu.ld) places this .core_entry section
 * first, so this single branch instruction always sits at payload offset 0.
 *
 * GNW_CORE_ENTRY_TARGET is provided by the core's Makefile as a
 * preprocessor define (e.g. -DGNW_CORE_ENTRY_TARGET=app_main_wsv), so this
 * file is reused verbatim by every core instead of duplicating it.
 *
 * Before jumping, walk __CORE_INIT_ARRAY_START__..__CORE_INIT_ARRAY_END__
 * (see cores/_template/core_ram_emu.ld) calling each function pointer —
 * this is the core's own C++ global constructor table (.init_array), no
 * crt0 links this image so nothing else would ever call these. For a
 * plain-C core the linker script still defines both symbols but the range
 * is empty (start == end), so the loop below is a harmless no-op: every
 * core, C or C++, goes through the exact same trampoline.
 *
 * r0/r1/r2 (load_state/start_paused/save_slot) are plain AAPCS argument
 * registers — NOT callee-saved — so they'd be clobbered the moment a
 * global constructor call below touches them (or any register at all,
 * really; a normal C/C++ function is free to trash r0-r3). Stack them
 * across the loop and pop them back right before the tail branch so
 * CORE_ENTRY sees them exactly as the firmware set them up.
 */
#ifndef GNW_CORE_ENTRY_TARGET
#error "GNW_CORE_ENTRY_TARGET must be defined (e.g. -DGNW_CORE_ENTRY_TARGET=app_main_wsv)"
#endif

.syntax unified
.thumb

.section .core_entry, "ax"
.global gnw_core_entry
.type gnw_core_entry, %function
.thumb_func
gnw_core_entry:
    push {r0-r2, r4-r6, lr}
    ldr  r4, =__CORE_INIT_ARRAY_START__
    ldr  r5, =__CORE_INIT_ARRAY_END__
1:
    cmp  r4, r5
    bhs  2f
    ldr  r6, [r4], #4
    blx  r6
    b    1b
2:
    pop  {r0-r2, r4-r6, lr}
    b.w  GNW_CORE_ENTRY_TARGET
