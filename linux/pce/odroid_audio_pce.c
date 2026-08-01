#include "odroid_audio.h"

#include <SDL.h>

static SDL_AudioDeviceID audio_device;
static bool audio_mute;

void odroid_audio_init(int sample_rate)
{
	SDL_AudioSpec wanted = {0};
	SDL_AudioSpec obtained = {0};

	if (audio_device != 0)
		return;

	if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO))
		SDL_InitSubSystem(SDL_INIT_AUDIO);

	wanted.freq = sample_rate > 0 ? sample_rate : 44100;
	wanted.format = AUDIO_S16SYS;
	wanted.channels = 1;
	wanted.samples = 1024;
	wanted.callback = NULL;

	audio_device = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (audio_device == 0) {
		SDL_Log("SDL_OpenAudioDevice failed: %s", SDL_GetError());
		return;
	}

	SDL_PauseAudioDevice(audio_device, 0);
}

void odroid_audio_terminate(void)
{
	if (audio_device != 0) {
		SDL_ClearQueuedAudio(audio_device);
		SDL_CloseAudioDevice(audio_device);
		audio_device = 0;
	}
}

void odroid_audio_submit(short *stereoAudioBuffer, int frameCount)
{
	if (audio_device == 0 || stereoAudioBuffer == NULL || frameCount <= 0 || audio_mute)
		return;

	SDL_QueueAudio(audio_device, stereoAudioBuffer, (uint32_t)frameCount * sizeof(int16_t));
}

uint32_t odroid_audio_get_queued_bytes(void)
{
	if (audio_device == 0)
		return 0;
	return SDL_GetQueuedAudioSize(audio_device);
}

int odroid_audio_volume_get(void)
{
	return 9;
}

void odroid_audio_volume_set(int level)
{
	(void)level;
}

void odroid_audio_set_sink(ODROID_AUDIO_SINK sink)
{
	(void)sink;
}

ODROID_AUDIO_SINK odroid_audio_get_sink(void)
{
	return ODROID_AUDIO_SINK_SPEAKER;
}

int odroid_audio_sample_rate_get(void)
{
	SDL_AudioSpec spec;
	if (audio_device == 0 || SDL_GetAudioDeviceSpec(audio_device, 0, &spec) != 0)
		return 44100;
	return spec.freq;
}

void odroid_audio_mute(bool mute)
{
	audio_mute = mute;
	if (mute && audio_device != 0)
		SDL_ClearQueuedAudio(audio_device);
}
