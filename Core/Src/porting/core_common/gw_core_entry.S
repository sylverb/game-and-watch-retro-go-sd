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
    b.w GNW_CORE_ENTRY_TARGET
