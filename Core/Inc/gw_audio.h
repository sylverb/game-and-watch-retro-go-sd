#ifndef _GW_AUDIO_H_
#define _GW_AUDIO_H_

#include "main.h"

extern SAI_HandleTypeDef hsai_BlockA1;
extern DMA_HandleTypeDef hdma_sai1_a;

// Default to 50Hz as it results in more samples than at 60Hz
#define AUDIO_SAMPLE_RATE   (48000)
// Must be large enough for any emulator's half-buffer.  Gwenesis PAL emits
// floor(313*3420/1008) = 1061 samples per frame (== GWENESIS_AUDIO_BUFFER_LENGTH_PAL),
// so the SAI/DMA half-buffer must hold at least 1061 (full buffer = 1061*2 = 2122).
// Use 1077 (== GWENESIS_AUDIO_BUFFER_CAPACITY) to match with a small margin.
/* 2026-07-14: enlarged for FF4's deep audio halves (3 frames of 800
 * mono samples per half = 50 ms) -- burst-absorbing playback so a slow
 * rendered frame no longer underruns the SAI (title / mode-7 crackle).
 * Only the static array grows (AHB .audio); every port keeps requesting
 * its own length via audio_start_playing. */
#define AUDIO_BUFFER_LENGTH (2400)
extern uint32_t audio_mute;

typedef enum {
    DMA_TRANSFER_STATE_HF = 0x00,
    DMA_TRANSFER_STATE_TC = 0x01,
} dma_transfer_state_t;

extern int16_t audiobuffer_dma[AUDIO_BUFFER_LENGTH * 2] __attribute__((section (".audio")));
extern dma_transfer_state_t dma_state;
extern uint32_t dma_counter;

uint16_t audio_get_buffer_full_length(void);
uint16_t audio_get_buffer_length(void);
uint16_t audio_get_buffer_size(void);
int16_t *audio_get_active_buffer(void);
int16_t *audio_get_inactive_buffer(void);
void audio_clear_active_buffer(void);
void audio_clear_inactive_buffer(void);
void audio_clear_buffers(void);
void audio_set_buffer_length(uint16_t length);
void audio_start_playing(uint16_t length);
void audio_start_playing_full_length(uint16_t length);
void audio_stop_playing(void);

#endif
