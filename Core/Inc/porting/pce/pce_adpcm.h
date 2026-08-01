/* PC Engine CD ADPCM (OKI MSM5205). $1808-$180E register block, 64KB RAM,
 * decode + OUT_RATE resample for the PCE audio mixer. See pce_adpcm.c. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

void    pce_adpcm_reset(void);
void    pce_adpcm_run(int32_t clocks);               /* advance engine (CPU cycles) */
void    pce_adpcm_sync(void);                        /* run since last sync (Cycles) */
void    pce_adpcm_frame_end(void);                   /* after Cycles reset each frame */
void    pce_adpcm_write(uint8_t reg, uint8_t val);   /* reg = A & 0x0F */
uint8_t pce_adpcm_read(uint8_t reg);
void    pce_adpcm_dma_byte(uint8_t val);             /* SCSI->ADPCM DMA: one byte to RAM */
bool    pce_adpcm_playing(void);
bool    pce_adpcm_end_flag(void);                    /* EndReached → $1803 bit3 */
bool    pce_adpcm_half_flag(void);                   /* HalfReached → $1803 bit2 */
void    pce_adpcm_reconcile_load(void);              /* after savestate restore */
int     pce_adpcm_fill(int16_t *out, int frames);    /* stereo @ OUT_RATE; 0 if idle */

/* Savestate: engine registers packed into words + the 64KB sample RAM.
 * Same tagged-block pattern as the CD-DA stream (see main_pce.c SaveState). */
#define PCE_ADPCM_STATE_WORDS 8
void     pce_adpcm_get(uint32_t out[PCE_ADPCM_STATE_WORDS]);
void     pce_adpcm_set(const uint32_t in[PCE_ADPCM_STATE_WORDS]);
uint8_t *pce_adpcm_ram(void);                        /* 64KB, for savestate streaming */
