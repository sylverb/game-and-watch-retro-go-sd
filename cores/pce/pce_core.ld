/*
 * PC Engine / PC Engine CD core linker script — custom (not the generic
 * cores/_template/core_ram_emu.ld) because this core needs a second
 * memory-region segment: PCE's CPU-hot files (gfx, h6280, pce, sound_pce,
 * pce_cd, pce_scsi, pce_adpcm) run from ITCM (zero-wait-state, no I-cache
 * miss) — same split as the old (disabled) STM32H7B0VBTx_FLASH.ld
 * .overlay_pce_itc section, just retargeted at the shared
 * ld/gnw_itcm_core.ld fragment instead of a firmware-private symbol.
 *
 * main_pce.o (segment 0's entry point + the one file that talks to the
 * ABI/launcher — settings, overlay, audio) stays in RAM_EMU on purpose,
 * matching the old script's own comment: it avoids ever taking the
 * ITCM->RAM_EMU direction as the FIRST call the trampoline makes (see
 * gw_core_entry.S), even though in this ABI-based model every firmware
 * call is an indirect call through a data pointer (no branch veneer
 * possible in the first place) so the risk that comment described no
 * longer applies to firmware calls — only to PCE's *own* internal
 * ITCM<->RAM_EMU calls (main_pce.c <-> gfx.c/pce.c/...), which DO need
 * the linker's standard long-branch veneers (BL range is +-16MB in
 * Thumb-2, ITCM @0x0 and RAM_EMU @0x2400xxxx are ~604MB apart).
 */

INCLUDE gnw_ram_emu.ld
INCLUDE gnw_itcm_core.ld

MEMORY
{
  RAM_EMU (xrw) : ORIGIN = __RAM_EMU_START__, LENGTH = __RAM_EMU_LENGTH__
  ITCM    (xrw) : ORIGIN = __ITCM_CORE_START__, LENGTH = __ITCM_CORE_LENGTH__
}

/* Every PCE object listed by EXACT relative path below (as the Makefile's
 * flat BUILD_DIR=build passes them to the linker) has its .text pulled out
 * of segment 0 (RAM_EMU) and placed in the ITCM segment instead — main_pce.o
 * and the core_common bridge are deliberately NOT in that list, see file
 * header. NOTE: these must be exact paths, NOT a leading-`*` glob like
 * `*pce.o` — that also matches `build/main_pce.o` (both end in "pce.o"),
 * which is exactly the bug this comment is warning you not to reintroduce
 * (caught by the cross-region linking spike, see the plan). */

SECTIONS
{
  /* Segment 0: RAM_EMU — entry trampoline, main_pce.o, and every PCE
   * object's .data/.rodata (only .text moves to ITCM, below). */
  .core_entry __RAM_EMU_START__ :
  {
    KEEP(*(.core_entry))
    KEEP(*(.gw_core_bridge_probe))
    . = ALIGN(4);
    EXCLUDE_FILE(build/gfx.o build/h6280.o build/pce.o build/sound_pce.o build/pce_cd.o build/pce_scsi.o build/pce_adpcm.o) *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)

    /* See cores/_template/core_ram_emu.ld's identical block — PCE is
     * plain C so this range is always empty (start == end), kept here
     * only so gw_core_entry.S's generic trampoline resolves the same two
     * symbols in every core. */
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

  __CORE_CODE_END__ = .;

  . = ALIGN(8);
  .bss (NOLOAD) :
  {
    *(.bss*)
    *(COMMON)
  } >RAM_EMU

  __CORE_BSS_END__ = .;

  ASSERT(__CORE_BSS_END__ <= __RAM_EMU_END__,
         "Error: PCE core RAM_EMU segment (code+data+bss) overflows RAM_EMU")

  /* Segment 1: ITCM — CPU-hot PCE code only (no .rodata/.data/.bss here;
   * those all stay in RAM_EMU above, see PCE_ITCM_OBJECTS comment). This
   * output section becomes tools/pack_core.py's second --segment payload
   * via cores/_template/Makefile's CORE_EXTRA_SEGMENTS = itcm:core_itcm. */
  .core_itcm __ITCM_CORE_START__ :
  {
    __CORE_ITCM_CODE_START__ = .;
    build/gfx.o(.text*)
    build/h6280.o(.text*)
    build/pce.o(.text*)
    build/sound_pce.o(.text*)
    build/pce_cd.o(.text*)
    build/pce_scsi.o(.text*)
    build/pce_adpcm.o(.text*)
    . = ALIGN(4);
    __CORE_ITCM_CODE_END__ = .;
  } >ITCM AT>RAM_EMU

  /* PCE's hot objects' .data/.bss/COMMON are already claimed by the RAM_EMU
   * .bss section above (its `*(.bss*) *(COMMON)` wildcard has no EXCLUDE_FILE,
   * unlike .core_entry's .text* rule) — same split as the disabled classic
   * build's .overlay_pce_bss. So this segment is code-only: 0 bss. */
  __CORE_ITCM_BSS_END__ = __CORE_ITCM_CODE_END__;

  ASSERT(__CORE_ITCM_BSS_END__ <= __ITCM_CORE_START__ + __ITCM_CORE_LENGTH__,
         "Error: PCE core ITCM segment (code) overflows the 64K ITCM region")

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
