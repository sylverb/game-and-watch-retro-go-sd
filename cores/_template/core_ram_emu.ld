/*
 * Generic linker script template for a standalone "core" binary.
 *
 * Copy this file (and Makefile) into cores/<system>/ and rename as needed
 * (or just point CORE_LDSCRIPT at a copy) — nothing here should need
 * per-core edits, the memory layout is entirely driven by the shared
 * ld/gnw_ram_emu.ld fragment (also INCLUDEd by the main firmware's
 * STM32H7B0VBTx_SDCARD.ld) so a core can never link at the wrong VMA.
 *
 * Bare-metal, single translation-unit-like image: no crt0, no crt crti/
 * crtn, no crt fini/init. Everything the core needs from the outside world
 * routes through Core/Src/porting/core_common (see docs there) instead of
 * a libc.
 *
 * C++ global constructors ARE supported (see .core_entry below and
 * gw_core_entry.S) — needed by cores/gb_tgbdual (the first C++ core).
 * Destructors are NOT: this image never "exits", nothing would ever call
 * .fini_array, so it's still discarded below.
 */

INCLUDE gnw_ram_emu.ld

MEMORY
{
  RAM_EMU (xrw) : ORIGIN = __RAM_EMU_START__, LENGTH = __RAM_EMU_LENGTH__
}

SECTIONS
{
  /* Firmware jumps to (__RAM_EMU_START__ | 1) knowing nothing about this
   * core's symbols — see run_dynamic_core() in Core/Src/retro-go/
   * rg_emulators.c. KEEP + place-first guarantees the single `b.w
   * app_main_<system>` trampoline from gw_core_entry.S always sits at
   * payload offset 0, regardless of link order of everything else. */
  .core_entry __RAM_EMU_START__ :
  {
    KEEP(*(.core_entry))
    /* tools/pack_core.py reads GW_CORE_BUILT_ABI_VERSION/SIZE (see
     * gw_core_bridge.c) straight out of the packaged payload bytes at
     * link-time-known offsets — KEEP so --gc-sections doesn't drop them
     * as "unreferenced". */
    KEEP(*(.gw_core_bridge_probe))
    . = ALIGN(4);
    *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)

    /* C++ global constructor table, walked by gw_core_entry.S before
     * jumping to CORE_ENTRY (see its header comment). Nested inside
     * .core_entry (rather than its own output section) so it rides along
     * with cores/_template/Makefile's existing
     * `--only-section=.core_entry --only-section=.data` packaging rule
     * without needing a third --only-section flag. Empty for a plain-C
     * core (start == end) — the entry trampoline's loop is then a no-op. */
    . = ALIGN(4);
    __CORE_INIT_ARRAY_START__ = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    __CORE_INIT_ARRAY_END__ = .;
  } >RAM_EMU

  . = ALIGN(8);
  .data :
  {
    *(.data*)
  } >RAM_EMU

  /* End of the file-backed payload: tools/pack_core.py reads this symbol
   * (via `nm`) to compute code_size (everything from __RAM_EMU_START__ up
   * to here gets written into the .bin's payload). */
  __CORE_CODE_END__ = .;

  /* NOLOAD: the firmware zeroes exactly bss_size bytes after loading the
   * payload (see run_dynamic_core()) instead of this being present in the
   * .bin file — same convention as PICO-8/GWHB. */
  . = ALIGN(8);
  .bss (NOLOAD) :
  {
    *(.bss*)
    *(COMMON)
  } >RAM_EMU

  __CORE_BSS_END__ = .;

  ASSERT(__CORE_BSS_END__ <= __RAM_EMU_END__,
         "Error: core code+data+bss overflows RAM_EMU")

  /* Deliberately not supported (no crt0 links this core, see Makefile):
   * C++ destructors, exception unwinding/RTTI tables. cores/gb_tgbdual (and
   * any future C++ core) builds with -fno-exceptions -fno-rtti precisely
   * so nothing ever lands in .ARM.exidx/.ARM.extab in the first place —
   * discarding them here is a safety net, not the primary mechanism. */
  /DISCARD/ :
  {
    *(.fini_array*)
    *(.preinit_array*)
    *(.ARM.exidx*)
    *(.ARM.extab*)
    *(.comment)
    *(.ARM.attributes)
  }
}
