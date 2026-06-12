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


extern uint32_t _siramdata;
extern uint32_t __ram_exec_start__;
extern uint32_t __ram_exec_end__;

extern uint32_t _sitcram_hot;
extern uint32_t __itcram_hot_start__;
extern uint32_t __itcram_hot_end__;


// If this is not an array the compiler might put in a memory_chk with dest_size 1...
extern void * __RAM_EMU_START__[];
extern void * _OVERLAY_NES_LOAD_START[];
extern uint8_t _OVERLAY_NES_SIZE;
extern void * _OVERLAY_NES_BSS_START[];
extern uint8_t _OVERLAY_NES_BSS_SIZE;
extern uint8_t __RAM_FCEUMM_MAPPER_LENGTH__;
extern void * __RAM_FCEUMM_START__[];
extern void * _OVERLAY_NES_FCEU_LOAD_START[];
extern uint8_t _OVERLAY_NES_FCEU_SIZE;
extern void * _OVERLAY_NES_FCEU_BSS_START[];
extern void * _OVERLAY_NES_FCEU_BSS_END[];
extern uint8_t _OVERLAY_NES_FCEU_BSS_SIZE;
extern void * _OVERLAY_GB_LOAD_START[];
extern uint8_t _OVERLAY_GB_SIZE;
extern void * _OVERLAY_GB_BSS_START[];
extern uint8_t _OVERLAY_GB_BSS_SIZE;
extern void * _OVERLAY_GB_BSS_END[];
extern void * _OVERLAY_TGB_LOAD_START[];
extern uint8_t _OVERLAY_TGB_SIZE;
extern void * _OVERLAY_TGB_BSS_START[];
extern void * _OVERLAY_TGB_BSS_END[];
extern uint8_t _OVERLAY_TGB_BSS_SIZE;
extern void * _OVERLAY_SMS_LOAD_START[];
extern uint8_t _OVERLAY_SMS_SIZE;
extern void * _OVERLAY_SMS_BSS_START[];
extern void * _OVERLAY_SMS_BSS_END[];
extern uint8_t _OVERLAY_SMS_BSS_SIZE;
extern void * _OVERLAY_PCE_LOAD_START[];
extern uint8_t _OVERLAY_PCE_SIZE;
extern void * _OVERLAY_PCE_BSS_START[];
extern void * _OVERLAY_PCE_BSS_END[];
extern uint8_t _OVERLAY_PCE_BSS_SIZE;
extern void * _OVERLAY_GW_LOAD_START[];
extern uint8_t _OVERLAY_GW_SIZE;
extern void * _OVERLAY_GW_BSS_START[];
extern void * _OVERLAY_GW_BSS_END[];
extern uint8_t _OVERLAY_GW_BSS_SIZE;
extern void * _OVERLAY_MSX_LOAD_START[];
extern uint8_t _OVERLAY_MSX_SIZE;
extern void * _OVERLAY_MSX_BSS_START[];
extern uint8_t _OVERLAY_MSX_BSS_SIZE;
extern void * _OVERLAY_MSX_BSS_END[];
extern void * _OVERLAY_WSV_LOAD_START[];
extern uint8_t _OVERLAY_WSV_SIZE;
extern void * _OVERLAY_WSV_BSS_START[];
extern void * _OVERLAY_WSV_BSS_END[];
extern uint8_t _OVERLAY_WSV_BSS_SIZE;
extern void * _OVERLAY_MD_LOAD_START[];
extern uint8_t _OVERLAY_MD_SIZE;
extern void * _OVERLAY_MD_BSS_START[];
extern uint8_t _OVERLAY_MD_BSS_SIZE;
extern void * _OVERLAY_A7800_LOAD_START[];
extern uint8_t _OVERLAY_A7800_SIZE;
extern void * _OVERLAY_A7800_BSS_START[];
extern void * _OVERLAY_A7800_BSS_END[];
extern uint8_t _OVERLAY_A7800_BSS_SIZE;
extern void * _OVERLAY_AMSTRAD_LOAD_START[];
extern uint8_t _OVERLAY_AMSTRAD_SIZE;
extern void * _OVERLAY_AMSTRAD_BSS_START[];
extern uint8_t _OVERLAY_AMSTRAD_BSS_SIZE;
extern void * _OVERLAY_ZELDA3_LOAD_START[];
extern uint8_t _OVERLAY_ZELDA3_SIZE;
extern void * _OVERLAY_ZELDA3_BSS_START[];
extern void * _OVERLAY_ZELDA3_BSS_END[];
extern uint8_t _OVERLAY_ZELDA3_BSS_SIZE;
extern void * _ZELDA3_MAIN_CODE_START[];
extern void * _ZELDA3_MAIN_CODE_END[];
extern void * _OVERLAY_SMW_LOAD_START[];
extern uint8_t _OVERLAY_SMW_SIZE;
extern void * _OVERLAY_SMW_BSS_START[];
extern void * _OVERLAY_SMW_BSS_END[];
extern uint8_t _OVERLAY_SMW_BSS_SIZE;
extern void * _OVERLAY_VIDEOPAC_LOAD_START[];
extern uint8_t _OVERLAY_VIDEOPAC_SIZE;
extern void * _OVERLAY_VIDEOPAC_BSS_START[];
extern uint8_t _OVERLAY_VIDEOPAC_BSS_SIZE;
extern void * _OVERLAY_CELESTE_LOAD_START[];
extern uint8_t _OVERLAY_CELESTE_SIZE;
extern void * _OVERLAY_CELESTE_BSS_START[];
extern uint8_t _OVERLAY_CELESTE_BSS_SIZE;
extern void * _OVERLAY_PICO8_LOAD_START[];
extern uint8_t _OVERLAY_PICO8_SIZE;
extern void * _OVERLAY_PICO8_BSS_START[];
extern void * _OVERLAY_PICO8_BSS_END[];
extern uint8_t _OVERLAY_PICO8_BSS_SIZE;
extern void * __pico8_code_start__[];
extern void * __pico8_code_end__[];
extern void * _PICO8_MAIN_CODE_START[];
extern void * _PICO8_MAIN_CODE_END[];
extern void * _OVERLAY_TAMA_LOAD_START[];
extern uint8_t _OVERLAY_TAMA_SIZE;
extern void * _OVERLAY_TAMA_BSS_START[];
extern uint8_t _OVERLAY_TAMA_BSS_SIZE;
extern void * _OVERLAY_PKMINI_LOAD_START[];
extern uint8_t _OVERLAY_PKMINI_SIZE;
extern void * _OVERLAY_PKMINI_BSS_START[];
extern void * _OVERLAY_PKMINI_BSS_END[];
extern uint8_t _OVERLAY_PKMINI_BSS_SIZE;
extern void * _OVERLAY_A2600_LOAD_START[];
extern uint8_t _OVERLAY_A2600_SIZE;
extern void * _OVERLAY_A2600_BSS_START[];
extern void * _OVERLAY_A2600_BSS_END[];
extern uint8_t _OVERLAY_A2600_BSS_SIZE;
extern void * __itcram_emu_wswan_start__[];
extern void * __itcram_emu_wswan_end__[];
extern uint8_t _ITCM_WSWAN_SIZE;
extern void * _OVERLAY_WSWAN_BSS_START[];
extern void * _OVERLAY_WSWAN_BSS_END[];
extern uint8_t _OVERLAY_WSWAN_BSS_SIZE;

extern void * _MSX_ROM_UNPACK_BUFFER[];
extern uint8_t _MSX_ROM_UNPACK_BUFFER_SIZE;
extern uint8_t *_PCE_ROM_UNPACK_BUFFER;
extern uint8_t _PCE_ROM_UNPACK_BUFFER_SIZE;

extern void * __RAM_END__[];

extern uint8_t __ahbram_start__[];
extern uint8_t __ahbram_heap_start__[];
extern uint8_t __ahbram_audio_start__[];
extern uint8_t __ahbram_end__[];

/* C++ init array constructors */
extern void (* __init_array_tgb_start__[])(void);
extern void (* __init_array_tgb_end__[])(void);
extern void (* __init_array_a2600_start__[])(void);
extern void (* __init_array_a2600_end__[])(void);