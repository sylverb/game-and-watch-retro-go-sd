/*
 * Game Boy Advance (gpSP) core linker script — custom (not the generic
 * cores/_template/core_ram_emu.ld) because this core needs three memory
 * regions beyond the default:
 *
 *   RAM_EMU  — entry + main_gba + hot video/text + most .data/.bss
 *   ITCM     — cpu.o (.text*), the ARM7 interpreter (zero wait states)
 *   AHB      — large BSS (bios_rom, cheats, sound_buffer); code_size 0
 *
 * Plus a non-packaged XIP sidecar (.xip_gba + .rodata_gba @ GBA_CODE
 * 0xDEC00000) shipped as cores/gba.xip and cached into QSPI at runtime
 * (see main_gba.c's gba_cache_xip_to_flash()).
 *
 * Layout mirrors the historical STM32H7B0VBTx_SDCARD.ld .overlay_gba /
 * .overlay_gba_itc / .gba_ahbram / .xip_gba / .rodata_gba split, adapted
 * for the dynamic-core segment model (see cores/pce/pce_core.ld).
 *
 * Objects land in a flat build/ dir (basenames only) — see cores/gba/Makefile.
 */

INCLUDE gnw_ram_emu.ld
INCLUDE gnw_itcm_core.ld
INCLUDE gnw_ahb_core.ld

MEMORY
{
  RAM_EMU  (xrw) : ORIGIN = __RAM_EMU_START__,  LENGTH = __RAM_EMU_LENGTH__
  ITCM     (xrw) : ORIGIN = __ITCM_CORE_START__, LENGTH = __ITCM_CORE_LENGTH__
  AHB      (rw)  : ORIGIN = __AHB_CORE_START__,  LENGTH = __AHB_CORE_LENGTH__
  /* Sentinel / XIP link address only — payload ships as gba.xip, not in the CORE bin. */
  GBA_CODE (x)   : ORIGIN = 0xDEC00000,         LENGTH = 512K
}

SECTIONS
{
  /* ------------------------------------------------------------------ */
  /* Segment 0: RAM_EMU — entry, main_gba (whole), hot video, hot text, */
  /* cpu/m4a rodata. BSS follows after AHB claims its large arrays.     */
  /* ------------------------------------------------------------------ */
  .core_entry __RAM_EMU_START__ :
  {
    KEEP(*(.core_entry))
    KEEP(*(.gw_core_bridge_probe))
    . = ALIGN(4);

    /* main_gba.o whole — holds GBA_CODE_BASE and the sentinel patch pass;
     * the pass must not walk itself. Symbols bound the patch range start. */
    _GBA_MAIN_CODE_START = .;
    build/main_gba.o (.data .data* .text .text* .rodata .rodata*)
    . = ALIGN(4);
    _GBA_MAIN_CODE_END = .;

    /* Hot video scanline path (section names, not symbols — redefine-syms
     * leaves sections alone). Remaining video.o text goes to .xip_gba. */
    build/video.o (.text._Z20render_scanline_text*)
    build/video.o (.text._Z13render_object*)
    build/video.o (.text._ZL16render_w_effects*)
    build/video.o (.text._ZL9order_obj*)
    build/video.o (.text.update_scanline)

    /* Remaining .data from every object (main_gba already claimed). */
    *(.data .data*)

    /* Hot .text: everything except cpu (→ITCM) and cold XIP files. */
    EXCLUDE_FILE(
      build/cpu.o
      build/video.o
      build/serial.o
      build/serial_proto.o
      build/gbp.o
      build/rfu.o
      build/input.o
      build/gba_frontend.o
      build/m4a_hle.o
    ) *(.text .text*)

    /* cpu.o runs from ITCM outside the sentinel scan — its rodata must
     * already be at the final RAM address. */
    build/cpu.o (.rodata .rodata*)
    /* m4a_gpsp.o is the thin side cpu.o calls; m4a_hle.o's rodata (mixer
     * signatures + variant table) is hot. m4a_hle.o TEXT → .xip_gba. */
    build/m4a_hle.o (.rodata .rodata*)
    build/m4a_gpsp.o (.rodata .rodata*)

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
    /* Anything not claimed above (normally empty — data is in .core_entry). */
    *(.data*)
  } >RAM_EMU

  __CORE_CODE_END__ = .;
  /* Sentinel patch walks [_GBA_MAIN_CODE_END, _OVERLAY_GBA_BSS_START). */
  _OVERLAY_GBA_BSS_START = __CORE_CODE_END__;
  _GBA_SENTINEL_PATCH_END = __CORE_CODE_END__;

  /* ------------------------------------------------------------------ */
  /* Segment AHB (NOLOAD, code_size 0): large BSS claimed BEFORE RAM    */
  /* .bss so the wildcard below does not swallow them.                  */
  /* ------------------------------------------------------------------ */
  .core_ahb __AHB_CORE_START__ (NOLOAD) :
  {
    __CORE_AHB_CODE_START__ = .;
    __CORE_AHB_CODE_END__ = .;
    . = ALIGN(8);
    __gba_ahb_start__ = .;
    build/gba_memory.o (.bss.bios_rom)
    build/cheats.o (.bss.cheats)
    build/sound.o (.bss.sound_buffer)
    . = ALIGN(8);
    __gba_ahb_end__ = .;
    __CORE_AHB_BSS_END__ = .;
    /* Room for 240*160*2 framebuffer via ahb_malloc after this segment. */
    ASSERT(__CORE_AHB_BSS_END__ + 240 * 160 * 2 <=
             __AHB_CORE_START__ + __AHB_CORE_LENGTH__,
           "Error: GBA AHB SRAM overflow (no room left for the framebuffer)");
  } >AHB

  /* Explicit VMA: .core_ahb moved `.' into AHB; pin BSS back to RAM_EMU. */
  .bss _OVERLAY_GBA_BSS_START (NOLOAD) :
  {
    . = ALIGN(8);
    *(.bss*)
    *(COMMON)
  } >RAM_EMU

  __CORE_BSS_END__ = .;

  ASSERT(__CORE_BSS_END__ <= __RAM_EMU_END__,
         "Error: GBA core RAM_EMU segment (code+data+bss) overflows RAM_EMU")

  /* ------------------------------------------------------------------ */
  /* Segment ITCM: cpu.o interpreter. Linked at region base 0 so        */
  /* run_dynamic_core's load-at-base matches the VMA (no +4 padding).   */
  /* ------------------------------------------------------------------ */
  .core_itcm __ITCM_CORE_START__ :
  {
    . = ALIGN(4);
    __CORE_ITCM_CODE_START__ = .;
    build/cpu.o (.text*)
    . = ALIGN(4);
    __CORE_ITCM_CODE_END__ = .;
  } >ITCM

  __CORE_ITCM_BSS_END__ = __CORE_ITCM_CODE_END__;

  ASSERT(__CORE_ITCM_BSS_END__ <= __ITCM_CORE_START__ + __ITCM_CORE_LENGTH__,
         "Error: GBA core ITCM segment (cpu.o) overflows the 64K ITCM region")

  /* ------------------------------------------------------------------ */
  /* XIP sidecar (NOT a pack segment) — cold text then remaining rodata */
  /* as one contiguous GBA_CODE image → cores/gba.xip.                  */
  /* ------------------------------------------------------------------ */
  .xip_gba :
  {
    . = ALIGN(8);
    __xip_gba_start__ = .;
    build/video.o (.text .text*)
    build/serial.o (.text .text*)
    build/serial_proto.o (.text .text*)
    build/gbp.o (.text .text*)
    build/rfu.o (.text .text*)
    build/input.o (.text .text*)
    build/gba_frontend.o (.text .text*)
    build/m4a_hle.o (.text .text*)
  } >GBA_CODE

  .rodata_gba :
  {
    . = ALIGN(8);
    __rodata_gba_start__ = .;
    /* Leftovers: main_gba / cpu / m4a_* rodata already claimed above. */
    *(.rodata .rodata*)
    . = ALIGN(4);
    __rodata_gba_end__ = .;
    __xip_gba_end__ = .;
  } >GBA_CODE

  ASSERT(__xip_gba_end__ <= ORIGIN(GBA_CODE) + LENGTH(GBA_CODE),
         "Error: GBA XIP (.xip_gba + .rodata_gba) overflows 512K GBA_CODE")

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
