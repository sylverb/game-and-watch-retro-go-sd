// FF4 native C port — Phase 5 scaffold stub.
// Minimal entry symbols so the linker can populate the .overlay_ff4
// section. Real init / step loop will be wired in a later phase
// alongside the LakeSnes core integration.

#include <stddef.h>
#include "snes/snes.h"

Snes *ff4_snes = NULL;

void ff4_init(void) {}
void ff4_step(void) {}
