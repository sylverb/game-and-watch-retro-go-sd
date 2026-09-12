#pragma once

#include <stdint.h>

extern uint8_t __EXTFLASH_START__;
extern uint8_t __EXTFLASH_END__;
extern uint8_t __EXTFLASH_BASE__;
extern uint8_t __EXTFLASH_OFFSET__;  // Bytes reserved at the bottom of extflash by the chainloader (its --defsym value). Read as (uint32_t)&__EXTFLASH_OFFSET__.
extern uint8_t __FILESYSTEM_START__;
extern uint8_t __FILESYSTEM_END__;
extern uint32_t __INTFLASH__;  // From linker, usually value 0x08000000 for bank 1, or 0x08100000 for bank 2

extern uint8_t __NULLPTR_LENGTH__;

extern uint8_t _Stack_Redzone_Size;
extern uint8_t _stack_redzone;

extern uint8_t _heap_start;
extern uint8_t _heap_end;

/* DTCM free region for dtc_malloc bump (below stack). */
extern uint8_t __dtc_padding_start__;
extern uint8_t __dtc_padding_end__;


extern uint32_t _siramdata;
extern uint32_t __ram_exec_start__;
extern uint32_t __ram_exec_end__;

extern uint32_t _sitcram_hot;
extern uint32_t __itcram_hot_start__;
extern uint32_t __itcram_hot_end__;


// If this is not an array the compiler might put in a memory_chk with dest_size 1...
extern void * __RAM_EMU_START__[];
extern uint32_t __RAM_EMU_END__;

/* From ld/gnw_itcm_core.ld / ld/gnw_ram_uc_core.ld — fixed base+length a
 * dynamic core's non-RAM_EMU segments (see gnw_core_region_t) may target.
 * Plain linker-script constants, not section symbols: read as
 * (uint32_t)&__ITCM_CORE_START__ etc., same convention as __RAM_EMU_START__.
 * AHB/DTCM are firmware heaps (ahb_malloc / dtc_*), not load regions. */
extern void * __ITCM_CORE_START__[];
extern uint32_t __ITCM_CORE_LENGTH__;
extern void * __RAM_UC_CORE_START__[];
extern uint32_t __RAM_UC_CORE_LENGTH__;
/* The per-emulator overlay symbols (_OVERLAY_GB/TGB/GW/MSX/WSV/A7800/
 * AMSTRAD/ZELDA3/SMW/VIDEOPAC/CELESTE/TAMA/PKMINI/A2600/WSWAN/PICO8_*,
 * _ZELDA3_MAIN_CODE_*, _MSX_ROM_UNPACK_BUFFER*) are gone along with the
 * .overlay_<system> sections that defined them. Those systems are now
 * standalone cores/<system>/ builds loaded from /cores/*.bin at runtime;
 * a core's code and bss are described by its own gnw_core_meta_t
 * segments[], not by firmware linker symbols. LUT8 extra core code loads
 * as a GNW_CORE_REGION_RAM_UC segment at __RAM_UC_CORE_START__
 * (ld/gnw_ram_uc_core.ld). */


extern void * __RAM_END__[];

extern uint8_t __ahbram_start__[];
extern uint8_t __ahbram_heap_start__[];
extern uint8_t __ahbram_audio_start__[];
extern uint8_t __ahbram_end__[];
