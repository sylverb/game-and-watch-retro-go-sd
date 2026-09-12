#include "gw_audio.h"
#include <string.h>

uint32_t audio_mute;

int16_t audiobuffer_dma[AUDIO_BUFFER_LENGTH * 2] __attribute__((section(".audio")));

dma_transfer_state_t dma_state;
uint32_t dma_counter;

static uint16_t audiobuffer_full_length = AUDIO_BUFFER_LENGTH * 2;

/* --- Homebrew ISR-fed PCM ring -------------------------------------------
 * pcm_fill lives HERE so the SAI DMA ISR only ever runs firmware code. The
 * decoded ring itself lives in the homebrew (RAM_EMU); it registers via
 * pcm_attach(). pcm_fill only READS that buffer (RAM_EMU is always mapped)
 * and is gated by pcm_owns (0 for emulators/launcher). */
static int16_t           *pcm_ring;
static volatile uint16_t *pcm_head_p, *pcm_tail_p;
static int                pcm_mask;
static volatile int16_t   pcm_vol;
static volatile uint8_t   pcm_owns;
static volatile uint8_t   pcm_silent;
static volatile uint32_t  pcm_played;

void pcm_attach(int16_t *ring, int size, volatile uint16_t *head, volatile uint16_t *tail)
{
    pcm_ring = ring;
    pcm_mask = size - 1;
    pcm_head_p = head;
    pcm_tail_p = tail;
}

void pcm_audio_enable(int on)
{
    pcm_owns = on ? 1 : 0;
}

void pcm_audio_set(int vol, int play)
{
    pcm_vol = (int16_t)vol;
    pcm_silent = play ? 0 : 1;
}

void pcm_audio_setpos(uint32_t p)
{
    pcm_played = p;
}

uint32_t pcm_audio_pos(void)
{
    return pcm_played;
}

static void pcm_fill(void)
{
    if (!pcm_owns)
        return;
    int16_t *buf = audio_get_active_buffer();
    int len = (int)audio_get_buffer_length();
    if (pcm_silent || !pcm_ring) {
        memset(buf, 0, (size_t)len * sizeof(int16_t));
        return;
    }
    int vol = pcm_vol, mask = pcm_mask;
    uint16_t tail = *pcm_tail_p, head = *pcm_head_p;
    for (int i = 0; i < len; i++) {
        int16_t s = 0;
        if (tail != head) {
            s = pcm_ring[tail];
            tail = (uint16_t)((tail + 1) & mask);
        }
        buf[i] = (int16_t)((s * vol) >> 8);
    }
    *pcm_tail_p = tail;
    pcm_played += (uint32_t)len;
}

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    (void)hsai;
    dma_counter++;
    dma_state = DMA_TRANSFER_STATE_HF;
    pcm_fill();
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
    (void)hsai;
    dma_counter++;
    dma_state = DMA_TRANSFER_STATE_TC;
    pcm_fill();
}

uint16_t audio_get_buffer_full_length()
{
    return audiobuffer_full_length;
}

static void audio_set_buffer_full_length(uint16_t full_length)
{
    audiobuffer_full_length = full_length;
}

uint16_t audio_get_buffer_length()
{
    bool isFirstHalf = (dma_state == DMA_TRANSFER_STATE_HF) ? true : false;

    return isFirstHalf ? audiobuffer_full_length / 2 : (audiobuffer_full_length + 1) / 2;
}

uint16_t audio_get_buffer_size()
{
    return audio_get_buffer_length() * sizeof(int16_t);
}

int16_t *audio_get_active_buffer(void)
{
    size_t offset = (dma_state == DMA_TRANSFER_STATE_HF) ? 0 : audiobuffer_full_length / 2;

    return &audiobuffer_dma[offset];
}

int16_t *audio_get_inactive_buffer(void)
{
    size_t offset = (dma_state == DMA_TRANSFER_STATE_TC) ? 0 : audiobuffer_full_length / 2;

    return &audiobuffer_dma[offset];
}

void audio_clear_active_buffer()
{
    bool isFirstHalf = (dma_state == DMA_TRANSFER_STATE_HF) ? true : false;

    memset(audio_get_active_buffer(), 0,
           (isFirstHalf ? audiobuffer_full_length / 2 : (audiobuffer_full_length + 1) / 2) *
               sizeof(audiobuffer_dma[0]));
}

void audio_clear_inactive_buffer()
{
    bool isFirstHalf = (dma_state == DMA_TRANSFER_STATE_HF) ? true : false;

    memset(audio_get_inactive_buffer(), 0,
           (isFirstHalf ? (audiobuffer_full_length + 1) / 2 : audiobuffer_full_length / 2) *
               sizeof(audiobuffer_dma[0]));
}

void audio_clear_buffers()
{
    memset(audiobuffer_dma, 0, sizeof(audiobuffer_dma));
}

void audio_start_playing(uint16_t length)
{
    audio_start_playing_full_length(length * 2);
}

void audio_start_playing_full_length(uint16_t full_length)
{
    /* Drop any stale homebrew ownership before (re)starting DMA so a prior
     * GWHB's pcm_owns cannot fill from a freed ring. */
    pcm_owns = 0;
    audio_clear_buffers();
    audio_set_buffer_full_length(full_length);
    HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)audiobuffer_dma, full_length);
}

void audio_stop_playing()
{
    pcm_owns = 0;
    audio_clear_buffers();
    HAL_SAI_DMAStop(&hsai_BlockA1);
}
