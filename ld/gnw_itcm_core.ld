/*
 * Shared ITCM "core segment" region definition.
 *
 * INCLUDEd by both the main firmware linker script (STM32H7B0VBTx_SDCARD.ld)
 * and any standalone core's linker script that opts into placing CPU-hot
 * code in ITCM (see cores/pce/pce_core.ld) — same reasoning as
 * ld/gnw_ram_emu.ld: a fixed address shared between both sides so a core
 * can never link at a VMA the firmware won't actually load it at (see
 * run_dynamic_core() in rg_emulators.c).
 *
 * ITCM RAM is 64K total (__ITCMRAM_LENGTH__ in STM32H7B0VBTx_SDCARD.ld). In
 * every SD_CARD=1 build the firmware places zero of its own code/data in
 * ITCM (._itcram / ._itcram_hot are both empty sections there — see the
 * comment above ._itcram_hot), so a core's ITCM segment can start right at
 * address 0 and use the whole region. STM32H7B0VBTx_SDCARD.ld ASSERTs that
 * __ITCM_CORE_START__ matches __itcram_hot_start__, so if that ever stops
 * being true (firmware ITCM usage added) the build fails loudly instead of
 * silently corrupting a core's loaded segment.
 *
 * If you change any value here, rebuild BOTH the firmware and every core
 * that uses a GNW_CORE_REGION_ITCM segment.
 */

__ITCM_CORE_START__  = 0x00000000;
__ITCM_CORE_LENGTH__ = 64K;
