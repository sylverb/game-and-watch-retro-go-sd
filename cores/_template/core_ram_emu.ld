/*
 * ============================================================================
 *  Retro-Go SD — default linker script for a standalone core
 * ============================================================================
 *
 * This file is both the default link script AND the memory-layout reference
 * for core authors. Read it once when you start a core; only fork it when
 * you need an extra ITCM or RAM_UC region — see "Extending" below.
 *
 * Related pieces (not duplicated here):
 *   cores/_template/Makefile              — build / objcopy / pack rules
 *   tools/pack_core.py                    — CORE-header packaging
 *   Core/Src/porting/core_common/         — entry, bridge, redefine-syms
 *   Core/Src/porting/core_common/CLAUDE.md
 *   ld/gnw_ram_emu.ld                     — shared __RAM_EMU_* addresses
 *                                           (must match firmware; rebuild
 *                                           ALL cores if changed)
 *
 * Default usage (do nothing special):
 *   CORE_LDSCRIPT ?= $(TEMPLATE_DIR)/core_ram_emu.ld
 *   → one segment in RAM_EMU, packaged as cores/<name>.bin
 *
 * ============================================================================
 *  Mental model
 * ============================================================================
 *
 *   SD card:  /cores/<name>.bin
 *      │  "CORE" + gnw_core_meta_t + logos + raw payload(s)
 *      ▼
 *   Firmware: emulators_scan_cores() → launcher tab
 *      │  on launch: memcpy payload → RAM_EMU, zero BSS, jump
 *      ▼
 *   CPU:      (__RAM_EMU_START__ | 1)   ← Thumb bit set
 *             .core_entry runs constructors, then CORE_ENTRY
 *
 * There is NO crt0, NO libc link against firmware, NO shared .overlay_*
 * with the launcher ELF. Your image is freestanding; every external call
 * goes through gw_firmware_abi_t (via the bridge + redefine-syms map).
 *
 * Budget: code + .data + .bss must fit in __RAM_EMU_LENGTH__ (~724 KiB:
 * 1024 KiB AXI SRAM minus the double framebuffer). ASSERT below fails
 * the link if you overflow — that is cheaper than a silent runtime smash.
 *
 * ============================================================================
 *  Symbols you will use from C
 * ============================================================================
 *
 *   __RAM_EMU_START__ / __RAM_EMU_END__ / __RAM_EMU_LENGTH__
 *       Region bounds (from gnw_ram_emu.ld). Same VMA for EVERY core —
 *       gdb/addr2line of 0x24xxxxxx must use THIS core's .elf, not another.
 *
 *   __CORE_CODE_END__
 *       End of the file-backed payload (after .core_entry + .data).
 *       pack_core.py:  code_size = __CORE_CODE_END__ - __RAM_EMU_START__
 *       Must equal the byte size of objcopy --only-section=.core_entry
 *       --only-section=.data (see Makefile). If they disagree, packaging
 *       is wrong — usually an empty ALIGN between sections (see Pitfalls).
 *
 *   __CORE_BSS_END__
 *       End of .bss (NOLOAD). Firmware zeroes
 *       bss_size = __CORE_BSS_END__ - __CORE_CODE_END__ after load.
 *       Typical seed for the bump allocator:
 *         ram_start = (uint32_t)&__CORE_BSS_END__;
 *
 *   __CORE_INIT_ARRAY_START__ / __CORE_INIT_ARRAY_END__
 *       Walked by gw_core_entry.S before calling CORE_ENTRY. Empty for
 *       pure-C cores (loop is a no-op).
 *
 * Declared for the compiler in gw_core_bridge.h (as uint32_t).
 *
 * ============================================================================
 *  Section layout (file order = load order)
 * ============================================================================
 *
 *   [.core_entry]  LOAD  RX-ish content, MUST start at __RAM_EMU_START__
 *       KEEP(.core_entry)           entry trampoline (offset 0)
 *       KEEP(.gw_core_bridge_probe) ABI version/size constants for packer
 *       .text* / .rodata* / glue    your code + constants
 *       .init_array*                C++ global constructors
 *
 *   [.data]        LOAD  RW initialized data (may be empty)
 *
 *   __CORE_CODE_END__   ← end of bytes written into the .bin payload
 *
 *   [.bss]         NOLOAD  zeroed by firmware, not stored in the .bin
 *
 *   __CORE_BSS_END__
 *
 * Packaging extracts ONLY .core_entry and .data. Anything you need in the
 * payload must live in one of those two output sections (or you extend
 * the Makefile --only-section list AND pack_core's assumptions).
 *
 * ============================================================================
 *  Pitfalls (do not re-discover the hard way)
 * ============================================================================
 *
 *   - Do NOT `. = ALIGN(n)` between loaded sections when a section may be
 *     empty. An ADVANCE without CONTENTS makes the VMA advance while
 *     objcopy output does not — __CORE_CODE_END__ then disagrees with the
 *     flat binary and pack_core refuses (or worse, silently mis-sizes).
 *
 *   - KEEP(.gw_core_bridge_probe): --gc-sections drops "unused" consts
 *     even with __attribute__((used)). The packer reads these via nm /
 *     payload offsets; nothing in the core references them.
 *
 *   - .fini_array / .preinit_array / .ARM.exidx are discarded. Cores never
 *     exit; build C++ with -fno-exceptions -fno-rtti (template Makefile).
 *
 *   - Custom scripts (GBA, PCE, …) that add MEMORY regions must keep
 *     segment 0 = RAM_EMU with .core_entry at __RAM_EMU_START__, and mark
 *     each segment's .bss NOLOAD. Extra segments are declared to pack_core
 *     via Makefile CORE_EXTRA_SEGMENTS / --segment (see pack_core.py).
 *
 *   - Changing gnw_ram_emu.ld addresses requires rebuilding firmware AND
 *     every core — otherwise the jump target or payload lands wrong.
 *
 * ============================================================================
 *  Extending (multi-segment cores)
 * ============================================================================
 *
 * Most cores need only this script. Extra regions:
 *
 * ITCM (hot code):
 *   1. Copy this file to your core dir (e.g. my_core.ld) and set
 *      CORE_LDSCRIPT := my_core.ld
 *   2. INCLUDE ld/gnw_itcm_core.ld.
 *   3. Add MEMORY { ITCM ... } and output sections for it.
 *   4. Define __CORE_ITCM_CODE_END__ / __CORE_ITCM_BSS_END__.
 *   5. Wire CORE_EXTRA_SEGMENTS=itcm:core_itcm / let pack_core auto-detect
 *      .core_itcm. See cores/msx/msx_core.ld for a worked example.
 *
 * RAM_UC (LUT8 LCD bonus, 150 KiB at __RAM_UC_CORE_START__):
 *   Same recipe with ld/gnw_ram_uc_core.ld, MEMORY { RAM_UC_CORE ... },
 *   output section .core_ram_uc, symbols __CORE_RAM_UC_CODE_END__ /
 *   __CORE_RAM_UC_BSS_END__, and CORE_EXTRA_SEGMENTS=ram_uc:core_ram_uc.
 *   Firmware switches the LTDC to LUT8 before loading this segment.
 *   The leftover after code+bss is lcd_get_bonus_pool().
 *
 * Do NOT place core segments in AHB or DTCM — those are firmware dynamic
 * pools (malloc heap / dtcm_* bump + stack). pack_core and gnw_core_probe
 * reject them.
 *
 * Segment 0 is always RAM_EMU and always carries the entry trampoline.
 * Max segments: GNW_CORE_MAX_SEGMENTS (4) in gnw_core_meta.h.
 *
 * ============================================================================
 *  ELF program headers (binutils 2.39+)
 * ============================================================================
 *
 * Without PHDRS, .core_entry (executable) and .data (writable) merge into
 * one LOAD with RWE and ld warns:
 *   "has a LOAD segment with RWX permissions"
 * Runtime ignores ELF flags: firmware memcpy's a contiguous blob into
 * RAM_EMU. PHDRS only keep the ELF tidy for the toolchain.
 */

INCLUDE gnw_ram_emu.ld

MEMORY
{
  /* (xrw) matches reality: the image executes from writable AXI SRAM. */
  RAM_EMU (xrw) : ORIGIN = __RAM_EMU_START__, LENGTH = __RAM_EMU_LENGTH__
}

PHDRS
{
  text PT_LOAD FLAGS(5); /* PF_R | PF_X */
  data PT_LOAD FLAGS(6); /* PF_R | PF_W */
}

SECTIONS
{
  /* ------------------------------------------------------------------ */
  /* Segment 0 payload (file-backed): entry + text/rodata + init_array  */
  /* ------------------------------------------------------------------ */
  .core_entry __RAM_EMU_START__ :
  {
    /* Offset 0: Thumb trampoline — firmware jumps here blindly. */
    KEEP(*(.core_entry))

    /* ABI bake-ins for pack_core.py (KEEP: otherwise --gc-sections drops). */
    KEEP(*(.gw_core_bridge_probe))

    . = ALIGN(4);
    *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)

    /* Nested in .core_entry so packaging needs no third --only-section. */
    . = ALIGN(4);
    __CORE_INIT_ARRAY_START__ = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    __CORE_INIT_ARRAY_END__ = .;
  } >RAM_EMU :text

  /* ------------------------------------------------------------------ */
  /* Initialized data (file-backed). May be empty — do not ALIGN before. */
  /* ------------------------------------------------------------------ */
  .data :
  {
    *(.data*)
  } >RAM_EMU :data

  /* End of bytes stored in the CORE .bin for segment 0. */
  __CORE_CODE_END__ = .;

  /* ------------------------------------------------------------------ */
  /* BSS (NOT in the .bin). Firmware zeroes [__CORE_CODE_END__, here).  */
  /* ------------------------------------------------------------------ */
  .bss (NOLOAD) :
  {
    . = ALIGN(8);
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
  } >RAM_EMU :data

  __CORE_BSS_END__ = .;

  ASSERT(__CORE_BSS_END__ <= __RAM_EMU_END__,
         "Error: core code+data+bss overflows RAM_EMU")

  /* Empty unwind table bounds (Stella / -lstdc++ may still reference). */
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
