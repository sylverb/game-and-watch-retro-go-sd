#pragma once

#include <SDL.h>
#include "odroid_input.h"

/* Poll SDL keyboard; out_state reflects held keys for the whole frame. */
void pce_sdl_input_poll(odroid_gamepad_state_t *out_state);

extern int linux_quit_req;

int pce_sdl_key_pressed(SDL_Scancode sc);
