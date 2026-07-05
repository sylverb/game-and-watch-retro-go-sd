#pragma once

#include <stdint.h>

int app_main_earthbound(uint8_t load_state, uint8_t start_paused, int8_t save_slot);

/* Point the platform_savestate_* backend (gw_savestate.c) at the base path for
 * the next capture/load. The two crash-safe ping-pong slot files derive from it
 * ("<base>" and "<base>.1"). Call before host_request_capture()/host_request_load(). */
void eb_savestate_set_base(const char *path);

/* Request a STANDBY sleep: capture a torn-safe native savestate into the private
 * sleep slot, then power down at the next root-loop boundary. Called from
 * gw_input.c's per-frame input poll when POWER is pressed (issuing the capture
 * from there lets in-flight blocking helpers unwind to the boundary). Idempotent
 * while a request is in flight. */
void eb_request_standby(void);
