/* The BIOS replacement gpSP ships (a clean-room reimplementation — see
 * external/gpsp/bios/README.md), 16 KB, .incbin'd.
 *
 * gpSP's own bios_data.S puts it in .data, which on this device means 16 KB of
 * the 724 KB overlay pool spent on something the core only ever reads. Here it is
 * .rodata, so the linker script can hand it to whichever region has the room.
 *
 * The section name is spelled out rather than left to -fdata-sections, because
 * the linker script places this object by name.
 *
 * Path is basename-only: cores/gba/Makefile passes
 * CORE_ASFLAGS=-I../../external/gpsp/bios so the assembler finds the file.
 */
  .section .rodata.open_gba_bios_rom, "a", %progbits
  .align 4
  .globl open_gba_bios_rom
  .type open_gba_bios_rom, %object
open_gba_bios_rom:
  .incbin "open_gba_bios.bin"
  .size open_gba_bios_rom, . - open_gba_bios_rom

  .section .note.GNU-stack, "", %progbits
