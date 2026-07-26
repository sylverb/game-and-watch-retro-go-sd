#ifndef GBA_AUDIO_FILTER_H
#define GBA_AUDIO_FILTER_H

#include <stdint.h>

/* A low-pass for the GBA's mono output — the analog rolloff the real console
 * has and an emulator does not.
 *
 * An M4A game mixes its music at a modest rate (Pokemon: 13,379 Hz) and the
 * FIFO stream is linearly upsampled to the SAI's 48 kHz. Everything above
 * half the mixer rate in the result is resampling images: inharmonic partials
 * that read to the ear as grit with a detuned tinge. Real hardware makes the
 * same images and then hides them behind a PWM DAC and a one-inch speaker;
 * our clean DAC reproduces them faithfully. Measured on Pokemon Ruby's title
 * music: the 16-24 kHz band carried more energy than 10-16 kHz, all of it
 * garbage. (tools/gba_m4a/prove_main.c, M4A_AUDIO_RAW, 2026-07-15.)
 *
 * So: a 4th-order Butterworth (two biquads) whose cutoff FOLLOWS THE GAME —
 * 0.42 x the FIFO rate the cart is actually clocking (sound_fifo_rate_hz()),
 * so a 13 kHz Pokemon gets its images cut while a 36 kHz cart is left alone.
 * The PSG channels pass through the same filter, which is what the real
 * analog path does to them too.
 *
 * Configure is cheap to call every frame: coefficients are only recomputed
 * when the rate actually changes. rate_hz == 0 (no FIFO clocked) or a rate
 * high enough that the images sit above audibility bypasses the filter.
 */

/* Set (or re-set) the source rate. Call once per frame with the current
 * sound_fifo_rate_hz(); a no-op unless the rate changed. */
void gba_lpf_configure(uint32_t rate_hz);

/* Filter n mono samples in place. A bypass (rate 0 / high rate) leaves the
 * buffer untouched. */
void gba_lpf_apply(int16_t *samples, uint32_t n);

/* Zero the delay lines (game start, state load). Keeps the configuration. */
void gba_lpf_reset(void);

/* The cutoff currently in force, Hz. 0 = bypassing. For tests and the HUD. */
uint32_t gba_lpf_cutoff_hz(void);

#endif
