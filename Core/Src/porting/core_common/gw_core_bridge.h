/*
 * core_common — reusable ABI bridge for standalone "core" binaries.
 *
 * Every classic emulator core built outside the main firmware ELF (see
 * cores/_template/ and cores/wsv/) links this bridge instead of talking to
 * firmware symbols directly. It mirrors, in generic form, the trampoline
 * pattern already used for the PICO-8 engine (Core/Src/porting/pico8/
 * p8_firmware_bridge.cpp / docs/PICO8_EXTERNAL_MODULE.md):
 *
 *   1. gw_core_bridge.c defines a `core_<name>` trampoline for every libc /
 *      G&W-hardware / retro-go function a core is allowed to call.
 *   2. gw_core_bridge_redefine_syms.txt maps the *real* name (memcpy, fopen,
 *      lcd_swap, ...) to `core_<name>` via `objcopy --redefine-syms`, applied
 *      to every other object file that makes up the core (potator, bilinear,
 *      main_wsv.c, ...) — see cores/_template/Makefile.
 *   3. The linker then resolves the renamed references against the
 *      trampolines defined here, so the core binary never contains a direct
 *      call to a firmware address baked in at this firmware's link time.
 *
 * Data globals (structs accessed with `.field`, not simple functions) can't
 * be redirected through a renamed function pointer. For those we go one
 * step further than PICO-8's per-build snapshot and expose them as macros
 * that dereference the ABI's data pointer on every access — this is a few
 * extra cycles per access, but stays correct regardless of the core's own
 * BSS layout (PICO-8 instead relies on its overlay's BSS landing at the
 * *same* address in both builds, which isn't a safe assumption to bake into
 * a generic multi-core SDK). Include this header AFTER the normal firmware
 * headers (common.h, rom_manager.h, gw_malloc.h) in the porting .c file so
 * their own `extern` declarations are parsed first and only later *uses* of
 * the identifiers get macro-substituted.
 */
#pragma once

#include "gw_firmware_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* common_emu_state (common.h): live view of the firmware's global through
 * common_emu_state_ptr. */
#define common_emu_state (*(common_emu_state_t *)(gw_firmware_abi()->common_emu_state_ptr))

/* ACTIVE_FILE (rom_manager.h): the ROM currently selected by the launcher,
 * read through ACTIVE_FILE_ptr (&firmware's ACTIVE_FILE global). */
#define ACTIVE_FILE (*(retro_emulator_file_t **)(gw_firmware_abi()->ACTIVE_FILE_ptr))

/* ram_start (gw_malloc.h): bump pointer into the shared RAM pool, read AND
 * written by cores through ram_start_ptr so ram_malloc()/ram_get_free_size()
 * (both firmware-side, see below) see the same live value. */
#define ram_start (*(gw_firmware_abi()->ram_start_ptr))

/* frame_counter (gw_lcd.h): incremented by the LCD vsync ISR (firmware-side,
 * always-resident). Only Mega Drive (gwenesis) reads this live value so far
 * (A/V-sync overflow detection); read-only from a core's point of view. */
#define frame_counter (*(gw_firmware_abi()->frame_counter_ptr))

/* dma_counter (gw_audio.h) / common_emu_sound_dma_marker (common.h): audio
 * DMA pacing counters, read AND written (PC Engine's CD-DA prefetch loop
 * advances common_emu_sound_dma_marker itself, mirroring what
 * common_emu_sound_sync() does internally) through the firmware's live
 * globals — same reasoning as ram_start above. */
#define dma_counter (*(gw_firmware_abi()->dma_counter_ptr))
#define common_emu_sound_dma_marker (*(gw_firmware_abi()->common_emu_sound_dma_marker_ptr))

/* Defined by every core's own linker script (cores/_template/core_ram_emu.ld)
 * right after the loaded code+data and right after BSS, respectively.
 * tools/pack_core.py reads these two (via `nm`) to compute code_size/
 * bss_size for the CORE-header metadata; a core's C code can also take
 * their address directly (e.g. to seed ram_start past its own BSS, see
 * main_wsv.c) without depending on any firmware-side symbol. */
extern uint32_t __CORE_CODE_END__;
extern uint32_t __CORE_BSS_END__;

/* One-time bridge setup. Currently a no-op placeholder (all state above is
 * accessed live through macros, nothing to snapshot at init) — kept so a
 * future core/bridge extension has an obvious place to hook into without
 * changing every core's entry trampoline. */
void gw_core_bridge_init(void);

#ifdef __cplusplus
}
#endif
