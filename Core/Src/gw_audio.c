#include "gw_audio.h"
#include <string.h>

uint32_t audio_mute;

int16_t audiobuffer_dma[AUDIO_BUFFER_LENGTH * 2] __attribute__((section(".audio")));

dma_transfer_state_t dma_state;
uint32_t dma_counter;

static uint16_t audiobuffer_full_length = AUDIO_BUFFER_LENGTH * 2;

/* Opt-in ISR refill hook (see gw_audio.h). NULL → legacy frame-pumped path. */
static audio_dma_refill_t dma_refill_cb = NULL;

void audio_set_dma_refill_callback(audio_dma_refill_t cb) {
    dma_refill_cb = cb;
}

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai) {
    dma_counter++;
    dma_state = DMA_TRANSFER_STATE_HF;
    /* DMA just finished the first half; it is now safe to refill offset 0. */
    if (dma_refill_cb)
        dma_refill_cb(&audiobuffer_dma[0], audiobuffer_full_length / 2);
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai) {
    dma_counter++;
    dma_state = DMA_TRANSFER_STATE_TC;
    /* DMA just finished the second half; refill from its start offset. */
    if (dma_refill_cb)
        dma_refill_cb(&audiobuffer_dma[audiobuffer_full_length / 2],
                      (audiobuffer_full_length + 1) / 2);
}

uint16_t audio_get_buffer_full_length() {
    return audiobuffer_full_length;
}

static void audio_set_buffer_full_length(uint16_t full_length) {
    audiobuffer_full_length = full_length;
}

// returns length of active buffer
uint16_t audio_get_buffer_length() {
    bool isFirstHalf = (dma_state == DMA_TRANSFER_STATE_HF) ? true : false;

    return isFirstHalf ? audiobuffer_full_length/2 : (audiobuffer_full_length+1)/2;
}

uint16_t audio_get_buffer_size() {
    return audio_get_buffer_length() * sizeof(int16_t);
}

int16_t *audio_get_active_buffer(void) {
    size_t offset = (dma_state == DMA_TRANSFER_STATE_HF) ? 0 : audiobuffer_full_length/2;

    return &audiobuffer_dma[offset];
}

int16_t *audio_get_inactive_buffer(void) {
    size_t offset = (dma_state == DMA_TRANSFER_STATE_TC) ? 0 : audiobuffer_full_length/2;

    return &audiobuffer_dma[offset];
}

void audio_clear_active_buffer() {
    bool isFirstHalf = (dma_state == DMA_TRANSFER_STATE_HF) ? true : false;

    memset(audio_get_active_buffer(), 0, (isFirstHalf ? audiobuffer_full_length/2 : (audiobuffer_full_length+1)/2) * sizeof(audiobuffer_dma[0]));
}

void audio_clear_inactive_buffer() {
    bool isFirstHalf = (dma_state == DMA_TRANSFER_STATE_HF) ? true : false;

    memset(audio_get_inactive_buffer(), 0, (isFirstHalf ? (audiobuffer_full_length+1)/2 : audiobuffer_full_length/2) * sizeof(audiobuffer_dma[0]));
}

void audio_clear_buffers() {
    memset(audiobuffer_dma, 0, sizeof(audiobuffer_dma));
}

void audio_start_playing(uint16_t length) {
    audio_start_playing_full_length(length*2);
}

void audio_start_playing_full_length(uint16_t full_length) {
    audio_clear_buffers();
    audio_set_buffer_full_length(full_length);
    HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *) audiobuffer_dma, full_length);
}

void audio_stop_playing() {
    audio_clear_buffers();
    HAL_SAI_DMAStop(&hsai_BlockA1);
}
