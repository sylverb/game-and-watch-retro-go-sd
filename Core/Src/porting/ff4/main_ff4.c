// FF4 retro-go porting glue — Phase 5 scaffold stub.
// No retro-go bindings yet (no input, no LCD, no save-state). The goal
// of this scaffold is to make the cross-compile succeed; gameplay
// integration is the next milestone.

#include <stdint.h>
#include "main_ff4.h"

extern void ff4_init(void);
extern void ff4_step(void);

int app_main_ff4(uint8_t load_state, uint8_t start_paused) {
    (void)load_state;
    (void)start_paused;
    ff4_init();
    return 0;
}
