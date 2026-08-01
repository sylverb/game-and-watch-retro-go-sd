#pragma once

#include <stdint.h>

void pce_audio_init(void);
void pce_audio_shutdown(void);
void pce_pcm_submit(void);
/* Keep audio queue from growing without bound when video hiccups. */
void pce_audio_pace(void);
