/*
 * Shared AHB SRAM region markers (legacy core-segment symbols).
 *
 * AHB is no longer a valid gnw_core_segment_t load target: the firmware
 * uses it for .persistent (boot_magic/logbuf), firmware .data/.bss, the
 * newlib malloc heap, and the .audio DMA reserve. Cores allocate from the
 * heap at runtime via ahb_malloc/malloc (ABI mem_ctl GW_MEM_AHB), never by
 * linking a fixed .core_ahb segment.
 *
 * These symbols remain so STM32H7B0VBTx_SDCARD.ld can ASSERT that the
 * documented window still starts at AHBRAM ORIGIN and ends before .audio.
 * Do not INCLUDE this from a core linker script to place executable code.
 */

__AHB_CORE_START__  = 0x30000000;
__AHB_CORE_LENGTH__ = 128K - 8K;
