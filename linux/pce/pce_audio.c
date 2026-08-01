#include "pce_audio.h"

#include "sound_pce.h"
#include "pce_scsi.h"
#include "pce_adpcm.h"
#include "odroid_audio.h"

#include <SDL.h>

/* Defined in odroid_audio_pce.c — live depth of the SDL playback queue. */
extern uint32_t odroid_audio_get_queued_bytes(void);

#define FPS_NTSC 60
#define AUDIO_BUFFER_LENGTH_PCE (PCE_SAMPLE_RATE / FPS_NTSC)

static short audioBuffer_pce[AUDIO_BUFFER_LENGTH_PCE * 2];
static int16_t mix_mono[AUDIO_BUFFER_LENGTH_PCE];

void pce_audio_init(void)
{
	odroid_audio_init(PCE_SAMPLE_RATE);
	pce_snd_init();
}

void pce_audio_shutdown(void)
{
	pce_snd_term();
	odroid_audio_terminate();
}

void pce_pcm_submit(void)
{
	pce_snd_update(audioBuffer_pce, AUDIO_BUFFER_LENGTH_PCE);

	static int16_t cdda_buf[AUDIO_BUFFER_LENGTH_PCE * 2];
	static int16_t adpcm_buf[AUDIO_BUFFER_LENGTH_PCE * 2];
	int cdda_n = pce_scsi_cdda_fill(cdda_buf, AUDIO_BUFFER_LENGTH_PCE);
	int adpcm_n = pce_adpcm_fill(adpcm_buf, AUDIO_BUFFER_LENGTH_PCE);

	uint32_t adpcm_vol = pce_scsi_adpcm_volume();  /* Q16 fader volume for ADPCM */

	for (int i = 0; i < AUDIO_BUFFER_LENGTH_PCE; i++) {
		int32_t sample = (int32_t)audioBuffer_pce[i * 2] + (int32_t)audioBuffer_pce[i * 2 + 1];
		if (cdda_n && i < cdda_n)
			sample += (int32_t)cdda_buf[i * 2] + (int32_t)cdda_buf[i * 2 + 1];
		if (adpcm_n && i < adpcm_n) {
			int32_t a = adpcm_buf[i * 2];
			if (adpcm_vol < 65536)
				a = (int32_t)(((int64_t)a * adpcm_vol) >> 16);
			sample += a;
		}
		if (sample > 32767)
			sample = 32767;
		else if (sample < -32768)
			sample = -32768;
		mix_mono[i] = (int16_t)sample;
	}

	odroid_audio_submit(mix_mono, AUDIO_BUFFER_LENGTH_PCE);
}

/* Pace the emulation from the real SDL audio-device queue rather than from a
 * wall-clock timer. pce_pcm_submit() queues exactly one frame worth of samples
 * every iteration; the device drains it at its true hardware rate. By sleeping
 * only while the queue is comfortably filled we lock the frame rate to that
 * consumption rate, so the queue never underruns (the source of the clicks/pops)
 * nor grows without bound. Mirrors gwenesis' common_emu_sound_sync(). */
void pce_audio_pace(void)
{
	const uint32_t bytes_per_sample = sizeof(int16_t); /* mono S16 */
	uint32_t rate = (uint32_t)odroid_audio_sample_rate_get();
	if (rate == 0)
		rate = PCE_SAMPLE_RATE;
	const uint32_t frame_bytes  = (rate / FPS_NTSC) * bytes_per_sample;
	const uint32_t target_bytes = frame_bytes * 3;  /* keep ~3 frames buffered */
	const uint32_t max_bytes    = frame_bytes * 6;  /* hard ceiling */

	uint32_t queued = odroid_audio_get_queued_bytes();
	if (queued == 0) {
		/* Audio not flowing (no device, or a genuine underrun): fall back to a
		 * wall-clock frame limit so we neither busy-spin nor drift off realtime. */
		static uint32_t last_tick;
		uint32_t now = SDL_GetTicks();
		uint32_t elapsed = now - last_tick;
		if (last_tick != 0 && elapsed < (1000 / FPS_NTSC))
			SDL_Delay((1000 / FPS_NTSC) - elapsed);
		last_tick = SDL_GetTicks();
		return;
	}

	while (odroid_audio_get_queued_bytes() > max_bytes)
		SDL_Delay(1);
	if (odroid_audio_get_queued_bytes() > target_bytes)
		SDL_Delay(1);
}
