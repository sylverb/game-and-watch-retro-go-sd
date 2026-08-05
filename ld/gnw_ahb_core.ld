/*
 * Shared AHB SRAM "core segment" region definition.
 *
 * INCLUDEd by both the main firmware linker script (STM32H7B0VBTx_SDCARD.ld)
 * and any standalone core's linker script that opts into placing an extra
 * code+data segment in AHB SRAM (e.g. for capacity, not necessarily speed —
 * unlike ITCM, AHB SRAM has no special I-fetch fast path) — same reasoning
 * as ld/gnw_ram_emu.ld / ld/gnw_itcm_core.ld.
 *
 * AHB SRAM is 128K total; the top 8K stays reserved for the .audio DMA
 * buffer (non-cacheable MPU slot, see __ahbram_audio_start__ in
 * STM32H7B0VBTx_SDCARD.ld) and must never be covered by a fixed core
 * segment. The remaining ~120K is otherwise entirely free of firmware data
 * (ahb_malloc's bump pool normally starts at __ahbram_start__ = this same
 * address) — STM32H7B0VBTx_SDCARD.ld ASSERTs __AHB_CORE_START__ matches
 * __ahbram_start__.
 *
 * If you change any value here, rebuild BOTH the firmware and every core
 * that uses a GNW_CORE_REGION_AHB segment.
 */

__AHB_CORE_START__  = 0x30000000;
__AHB_CORE_LENGTH__ = 128K - 8K;
