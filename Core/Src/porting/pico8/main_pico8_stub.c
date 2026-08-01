/*
 * PICO-8 stub — install-prompt only.
 *
 * The retro-go firmware ships without the PICO-8 engine itself. A third-
 * party engine binary (pico8.bin) can be installed to /cores/pico8.bin
 * on the SD card to enable cart playback. Until that's installed, any
 * selection of a PICO-8 cart lands here.
 *
 * This stub renders a multi-line install-prompt screen, waits for any
 * button press, and returns to the launcher. Nothing is decoded about
 * the cart — it stays untouched on disk.
 */

extern "C" {
#include <odroid_system.h>
#include <string.h>

#include "main.h"
#include "gw_lcd.h"
#include "gw_buttons.h"
#include "common.h"
#include "appid.h"

#include "main_pico8.h"
}

/* Colours match what other emulators' error/info screens use so the
 * stub screen feels consistent with the rest of retro-go. */
#ifndef C_GW_YELLOW
#define C_GW_YELLOW 0xFFC0
#endif
#ifndef C_GW_RED
#define C_GW_RED    0xC804
#endif

static const char *kInstallLines[] = {
    "PICO-8 engine not installed",
    "",
    "To play PICO-8 cartridges:",
    "1. Download the pico8 engine",
    "   binaries from the separate",
    "   third-party distribution at",
    "   github.com/Macs75/pico8_gnw_distro",
    "2. Copy pico8.bin/.itcm/.ro (3 files)",
    "   to your SD card under /cores/",
    "3. Copy .p8 or .p8.png carts",
    "   to /roms/pico8/",
    "4. Re-launch a cart.",
    "",
    "Press any button to return.",
};
#define INSTALL_LINE_COUNT (sizeof(kInstallLines) / sizeof(kInstallLines[0]))

static void draw_install_screen(void)
{
    /* Clear both LCD buffers so the message is visible regardless of
     * which half the LTDC is currently displaying. */
    lcd_clear_active_buffer();
    lcd_clear_inactive_buffer();

    const int line_height = 10;
    const int total_h     = (int)INSTALL_LINE_COUNT * line_height;
    const int y0          = (GW_LCD_HEIGHT - total_h) / 2;

    for (size_t i = 0; i < INSTALL_LINE_COUNT; i++) {
        odroid_overlay_draw_text(4, y0 + (int)i * line_height,
                                 GW_LCD_WIDTH - 8,
                                 (char *)kInstallLines[i],
                                 (i == 0) ? C_GW_YELLOW : 0xFFFF,
                                 C_GW_RED);
    }

    lcd_swap();
}

static void wait_for_any_button(void)
{
    odroid_gamepad_state_t js;
    do {
        wdog_refresh();
        HAL_Delay(50);
        odroid_input_read_gamepad(&js);
    } while (!js.values[ODROID_INPUT_A] &&
             !js.values[ODROID_INPUT_B] &&
             !js.values[ODROID_INPUT_START] &&
             !js.values[ODROID_INPUT_X]);
}

/* Entry trampoline placed at overlay offset 0 via .pico8_entry section.
 * Firmware dispatches through a function pointer at the engine load
 * address so this MUST be the first code in the overlay for both the
 * GPL stub and the separately-distributed engine binary.
 *
 * Self-zero BSS — equivalent of crt0's startup BSS clear. GPL no longer
 * zeroes BSS (so the engine can grow without GPL needing to know its
 * footprint). Hand-written loop because the SD engine's firmware-bridge
 * memset isn't initialised yet; keeping the same code path here in the
 * stub for symmetry. */
__attribute__((section(".pico8_entry"), used, noinline))
void pico8_entry_trampoline(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    /* Local externs declared as bytes so we never collide with the
     * void*[] declaration in gw_linker.h (if it gets pulled in). */
    extern uint8_t _OVERLAY_PICO8_BSS_START[];
    extern uint8_t _OVERLAY_PICO8_BSS_END[];
    uint32_t *p = (uint32_t *)_OVERLAY_PICO8_BSS_START;
    uint32_t *e = (uint32_t *)_OVERLAY_PICO8_BSS_END;
    while (p < e) *p++ = 0;
    app_main_pico8(load_state, start_paused, save_slot);
}

void app_main_pico8(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    (void)load_state;
    (void)start_paused;
    (void)save_slot;

    odroid_system_init(APPID_PICO8, 48000);
    odroid_system_emu_init(NULL, NULL, NULL, NULL, NULL, NULL, NULL);

    draw_install_screen();
    wait_for_any_button();

    /* Return to the retro-go launcher. Does not return. */
    odroid_system_switch_app(0);
    while (1) { wdog_refresh(); HAL_Delay(100); }  /* unreachable */
}
