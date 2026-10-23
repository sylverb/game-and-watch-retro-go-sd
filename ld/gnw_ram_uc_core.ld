/*
 * Shared LUT8 LCD-bonus "core segment" region.
 *
 * INCLUDEd by both firmware linker scripts and any standalone core that
 * places a GNW_CORE_REGION_RAM_UC segment here — same reasoning as
 * ld/gnw_ram_emu.ld / ld/gnw_itcm_core.ld: a fixed VMA shared between both
 * sides so a core can never link at an address the firmware won't load.
 *
 * AXI SRAM layout (1 MiB at 0x24000000):
 *
 *   __RAM_START__                 LCD pool / RAM_UC origin
 *   + 2*320*240  (150 KiB)        LUT8 double framebuffer (MPU-uncached)
 *   __RAM_UC_CORE_START__         optional core segment + leftover heap
 *   + 2*320*240  (150 KiB)        __RAM_UC_CORE_LENGTH__
 *   __RAM_EMU_START__             classic core segment 0
 *
 * RGB565 uses the whole 300 KiB LCD pool as framebuffers, so this window
 * is only valid after the firmware has switched the LTDC to LUT8
 * (run_dynamic_core() does that before memcpy'ing a RAM_UC segment).
 * lcd_get_bonus_pool() then returns whatever is left after the loaded
 * code+bss prefix.
 *
 * Requires __RAM_START__ and __RAM_UC_LENGTH__ (from gnw_ram_emu.ld, or
 * defined locally in STM32H7B0VBTx_FLASH.ld). Rebuild firmware AND every
 * core that uses GNW_CORE_REGION_RAM_UC if you change these values.
 */

/* 2 × 320 × 240 × 1 byte — LUT8 double framebuffer, 150 KiB exactly. */
__RAM_UC_FB_BYTES__    = 2 * 320 * 240;
__RAM_UC_CORE_START__  = __RAM_START__ + __RAM_UC_FB_BYTES__;
__RAM_UC_CORE_LENGTH__ = __RAM_UC_LENGTH__ - __RAM_UC_FB_BYTES__;
__RAM_UC_CORE_END__    = __RAM_UC_CORE_START__ + __RAM_UC_CORE_LENGTH__;
