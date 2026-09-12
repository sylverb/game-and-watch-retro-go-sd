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
#define AUDIO_BUFFER_LENGTH (1077)
extern uint32_t audio_mute;

typedef enum {
    DMA_TRANSFER_STATE_HF = 0x00,
    DMA_TRANSFER_STATE_TC = 0x01,
} dma_transfer_state_t;

extern int16_t audiobuffer_dma[AUDIO_BUFFER_LENGTH * 2] __attribute__((section (".audio")));
extern dma_transfer_state_t dma_state;
extern uint32_t dma_counter;

/* ISR-fed PCM ring for out-of-tree homebrews (Music, Video, …). The fill
 * routine lives in this firmware so the SAI ISR never jumps into RAM_EMU
 * code; the homebrew owns the ring and registers it via pcm_attach(). */
void     pcm_attach(int16_t *ring, int size, volatile uint16_t *head, volatile uint16_t *tail);
void     pcm_audio_enable(int on);          /* 1 = homebrew owns the DMA half-fill */
void     pcm_audio_set(int vol, int play);  /* play=0 -> ISR outputs silence; vol 0..255 */
void     pcm_audio_setpos(uint32_t samples);
uint32_t pcm_audio_pos(void);

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
