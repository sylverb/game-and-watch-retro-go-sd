
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Input Input;

#include "snes.h"
#include "statehandler.h"

struct Input {
  Snes* snes;
  uint8_t type;
  // latchline
  bool latchLine;
  // for controller
  uint16_t currentState; // actual state
  uint16_t latchedState;
};

Input* input_init(Snes* snes);
void input_free(Input* input);
void input_reset(Input* input);
void input_handleState(Input* input, StateHandler* sh);
void input_latch(Input* input, bool value);
uint8_t input_read(Input* input);

#endif
