/*
 * G&W debug platform — all dumps are no-ops on device.
 *
 * The platform.h debug hooks (PPU dump, VRAM-as-BMP, full state dump)
 * write files to a "debug/" directory on desktop builds. There's no
 * equivalent path on device, and the SD card driver is busy serving
 * asset loads, so we intentionally do nothing here.
 */

#include "platform/platform.h"

void platform_debug_dump_ppu(const pixel_t *framebuffer)
{
    (void)framebuffer;
}

void platform_debug_dump_vram_image(void) {}

void platform_debug_dump_state(void) {}
