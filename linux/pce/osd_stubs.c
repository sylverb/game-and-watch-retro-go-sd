#include <stdint.h>

/* pce.c exposes pce_run() which calls these; the Linux harness drives gfx_run()
 * directly from its own main loop, so these are no-ops. */
void osd_gfx_blit(void) {}
void osd_vsync(void) {}
void osd_input_read(uint8_t joypads[8])
{
	(void)joypads;
}
