#ifndef _INCLUDE_SOUND_H
#define _INCLUDE_SOUND_H

/* 44100: CD-DA passes through BIT-PERFECT (no decimation filter at all — the
 * 22050 path halved the bandwidth through a 4-tap low-pass) and the PSG renders
 * natively at 44.1k. Costs ~+0.15ms/frame (PSG+mix at 2x samples) minus the
 * removed decimation work — affordable since auto-OC lvl2 + the tier-1 perf
 * pass. DMA buffer fits: needs 735*2 halfwords <= AUDIO_BUFFER_LENGTH(1077)*2. */
#define PCE_SAMPLE_RATE   (44100)

int  pce_snd_init(void);
void pce_snd_term(void);
void pce_snd_update(short *buffer, unsigned length);

#endif
