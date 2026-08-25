/*
 * MSX (blueMSX) core linker script — custom like other multi-region cores.
 *
 * R800 + SlotManager .text run from ITCM. Also park the tiny
 * vdpCommandPortLatchPending() there: R800_IRQ_PENDING polls it from the
 * interpreter loop while VDP IRQ is asserted, and an ITCM→RAM veneer on
 * that call showed up as a measurable cost vs the classic all-RAM_EMU
 * overlay. (-ffunction-sections gives .text.vdpCommandPortLatchPending.)
 *
 * blueMSX's former itc_malloc heap uses dtc_malloc/dtc_calloc (reset via
 * dtc_init on machine switch) so ITCM stays free for code.
 *
 * .core_itcm is listed before .core_entry so GNU ld claims those input
 * sections first; .core_entry's *(.text*) then skips them.
 *
 * main_msx.o, the ABI bridge, the rest of VDP, and everything else stay
 * in RAM_EMU. .rodata/.data/.bss of the ITCM objects also stay in RAM_EMU.
 */

INCLUDE gnw_ram_emu.ld
INCLUDE gnw_itcm_core.ld

MEMORY
{
  RAM_EMU (xrw) : ORIGIN = __RAM_EMU_START__, LENGTH = __RAM_EMU_LENGTH__
  ITCM    (xrw) : ORIGIN = __ITCM_CORE_START__, LENGTH = __ITCM_CORE_LENGTH__
}

SECTIONS
{
  /* Claim ITCM-resident .text first (absolute VMA; order is for matching). */
  .core_itcm __ITCM_CORE_START__ :
  {
    __CORE_ITCM_CODE_START__ = .;
    build/R800.o(.text*)
    build/SlotManager.o(.text*)
    build/VDP_MSX.o(.text.vdpCommandPortLatchPending)
    . = ALIGN(4);
    __CORE_ITCM_CODE_END__ = .;
  } >ITCM AT>RAM_EMU

  __CORE_ITCM_BSS_END__ = __CORE_ITCM_CODE_END__;

  ASSERT(__CORE_ITCM_BSS_END__ <= __ITCM_CORE_START__ + __ITCM_CORE_LENGTH__,
         "Error: MSX core ITCM segment (code) overflows the 64K ITCM region")

  .core_entry __RAM_EMU_START__ :
  {
    KEEP(*(.core_entry))
    KEEP(*(.gw_core_bridge_probe))
    . = ALIGN(4);
    EXCLUDE_FILE(build/R800.o build/SlotManager.o) *(.text*)
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
         "Error: MSX core RAM_EMU segment (code+data+bss) overflows RAM_EMU")

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
