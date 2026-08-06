/*
 * FCEUmm (NES) core linker script.
 *
 * Mappers load into the first 48K of RAM_EMU at runtime
 * (fceumm_load_mapper → __RAM_EMU_START__). The core itself must live
 * after that window (historical __RAM_FCEUMM_START__), but run_dynamic_core()
 * always jumps to (__RAM_EMU_START__ | 1). So we keep the entry trampoline
 * (+ ABI probe symbols) at offset 0, then advance the location counter to
 * the FCEUmm start before placing .text — the 48K hole is zeros in the
 * packed payload and is overwritten when a mapper blob is loaded.
 *
 * Mapper board .o files are claimed first as VMA-overlapping overlays
 * (build/nes_mapper_overlays.ld) so ines.o's bmap gets correct absolute
 * Init addresses and wildcards below do not pull them into the core image.
 * LMA lives in MAPPER_STORE (extracted to mapper_*.bin, not packed).
 * Requires -Wl,--no-check-sections (template default).
 */

INCLUDE gnw_ram_emu.ld

/* Same values as STM32H7B0VBTx_SDCARD.ld — must stay in sync. */
__RAM_FCEUMM_MAPPER_LENGTH__ = 48K;
__RAM_FCEUMM_START__ = __RAM_EMU_START__ + __RAM_FCEUMM_MAPPER_LENGTH__;

MEMORY
{
  RAM_EMU (xrw) : ORIGIN = __RAM_EMU_START__, LENGTH = __RAM_EMU_LENGTH__
  /* LMA-only store for per-mapper overlays (extracted to mapper_*.bin). */
  MAPPER_STORE (r) : ORIGIN = 0x60000000, LENGTH = 16M
}

SECTIONS
{
  /* Claim mapper input sections first (explicit file paths). */
  INCLUDE build/nes_mapper_overlays.ld

  /* Overlays leave `.' near the end of the mapper window — pin core. */
  .core_entry __RAM_EMU_START__ :
  {
    KEEP(*(.core_entry))
    KEEP(*(.gw_core_bridge_probe))
    . = ALIGN(4);

    /* Skip the mapper window via relative advance (NOT `. = abs_addr`,
     * which can mis-fire after an OVERLAY and inflate the section). */
    . = . + __RAM_FCEUMM_MAPPER_LENGTH__;

    *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)

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
         "Error: NES FCEUmm core overflows RAM_EMU")
  ASSERT(ABSOLUTE(__CORE_CODE_END__) > __RAM_FCEUMM_START__,
         "Error: NES core code did not land past the mapper window")

  __exidx_start = .;
  __exidx_end = .;

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
