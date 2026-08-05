/*
 * Shared RAM_EMU region definition.
 *
 * INCLUDEd from both the main firmware linker script
 * (STM32H7B0VBTx_SDCARD.ld) and every standalone core's linker script
 * (cores/_template/core_ram_emu.ld, cores/wsv/wsv_core.ld, ...) so the
 * VMA a core is linked at can never drift from where the firmware
 * actually loads it (see run_dynamic_core() in rg_emulators.c).
 *
 * If you change any value here, rebuild BOTH the firmware and every core.
 */

__RAM_UC_LENGTH__   = 300K; /* framebuffer : 2 x 320 x 240 x 16 bits */
__RAM_EMU_LENGTH__  = 1024K - __RAM_UC_LENGTH__;

__RAM_START__       = 0x24000000;
__RAM_EMU_START__   = __RAM_START__ + __RAM_UC_LENGTH__;
__RAM_EMU_END__     = __RAM_EMU_START__ + __RAM_EMU_LENGTH__;
