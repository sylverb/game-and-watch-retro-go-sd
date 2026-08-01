#include "pce_input_sdl.h"

#include <SDL.h>
#include <string.h>

extern int linux_savestate_req;
extern int linux_loadstate_req;

static int key_down(SDL_Scancode sc)
{
	const Uint8 *k = SDL_GetKeyboardState(NULL);
	return k && k[sc];
}

int linux_quit_req = 0;

void pce_sdl_input_poll(odroid_gamepad_state_t *out_state)
{
	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT)
			linux_quit_req = 1;
		if (event.type == SDL_KEYDOWN) {
			if (event.key.keysym.sym == SDLK_ESCAPE)
				linux_quit_req = 1;
			if (event.key.keysym.sym == SDLK_F2)
				linux_savestate_req = 1;
			if (event.key.keysym.sym == SDLK_F4)
				linux_loadstate_req = 1;
		}
	}

	SDL_PumpEvents();
	memset(out_state->values, 0, sizeof(out_state->values));

	if (key_down(SDL_SCANCODE_LEFT)  || key_down(SDL_SCANCODE_J))
		out_state->values[ODROID_INPUT_LEFT] = 1;
	if (key_down(SDL_SCANCODE_RIGHT) || key_down(SDL_SCANCODE_L))
		out_state->values[ODROID_INPUT_RIGHT] = 1;
	if (key_down(SDL_SCANCODE_UP)    || key_down(SDL_SCANCODE_I))
		out_state->values[ODROID_INPUT_UP] = 1;
	if (key_down(SDL_SCANCODE_DOWN)  || key_down(SDL_SCANCODE_K))
		out_state->values[ODROID_INPUT_DOWN] = 1;

	if (key_down(SDL_SCANCODE_X))
		out_state->values[ODROID_INPUT_A] = 1;
	if (key_down(SDL_SCANCODE_Z))
		out_state->values[ODROID_INPUT_B] = 1;

	if (key_down(SDL_SCANCODE_LSHIFT) || key_down(SDL_SCANCODE_RSHIFT) ||
	    key_down(SDL_SCANCODE_RETURN) || key_down(SDL_SCANCODE_SPACE))
		out_state->values[ODROID_INPUT_START] = 1;

	if (key_down(SDL_SCANCODE_LCTRL) || key_down(SDL_SCANCODE_RCTRL))
		out_state->values[ODROID_INPUT_SELECT] = 1;
}

int pce_sdl_key_pressed(SDL_Scancode sc)
{
	SDL_PumpEvents();
	return key_down(sc);
}
