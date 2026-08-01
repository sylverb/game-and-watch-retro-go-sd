/* PC Engine CD-ROM2 ($1800-$180F) SCSI target — Phase 2.2, iteration 1.
 *
 * The System Card BIOS drives this register block as a SCSI-1 initiator to read
 * the data track off the disc. This module is the SCSI *target* state machine:
 * it accepts a 6-byte CDB, runs the COMMAND -> DATA-IN -> STATUS -> MESSAGE
 * phases, and feeds sector data from the CUE/BIN layer (pce_cd). Audio (ADPCM /
 * CD-DA) is stubbed for now — boot/data first. Expect on-device iteration. */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "pce_cd.h"

/* Attach (or detach) the mounted disc. Called at .cue launch. */
void pce_scsi_set_disc(const pce_cd_toc_t *toc, bool present);

/* Reset the SCSI/CD state (called from ResetPCE). */
void pce_scsi_reset(void);

/* CD-ROM2 register block at $1800-$180F. `reg` is the low nibble (A & 0x0F). */
uint8_t pce_scsi_read(uint8_t reg);
void    pce_scsi_write(uint8_t reg, uint8_t val);

/* Per-frame poll: advances pending reads and asserts IRQ when data/status is
 * ready (kept out of the hot CPU read path). Returns true if an IRQ is pending. */
void pce_scsi_run(void);

/* Fill `frames` stereo int16 samples of CD-DA (Red Book audio / BGM).
 * Volume is already fader-scaled. Returns frames produced, 0 if not playing. */
int pce_scsi_cdda_fill(int16_t *out, int frames);

/* Current Q16 fader volume (0=silent, 65536=full) for CD-DA and ADPCM.
 * The mixer in main_pce.c / pce_audio.c should scale ADPCM samples
 * by pce_scsi_adpcm_volume() >> 16. CD-DA is already scaled inside cdda_fill. */
uint32_t pce_scsi_cdda_volume(void);
uint32_t pce_scsi_adpcm_volume(void);

/* Opportunistic FIFO prefetch for the sound-sync wait loop: one small batched
 * SD read (<=2 sectors) into the CD-DA FIFO. Returns true if a read happened.
 * Main-loop context only (same context as pce_scsi_cdda_fill). */
bool pce_scsi_cdda_prefetch(void);

/* CD-DA playback snapshot for savestates: [0]=play [1]=lba [2]=start [3]=end
 * [4]=mode. LoadState resets the SCSI to idle (correct for data transfers), which
 * used to also kill the BGM — the game believes its music is still playing, so a
 * resume stayed silent until the next track change. Save/restore these 5 words to
 * re-arm the stream. */
#define PCE_SCSI_CDDA_STATE_WORDS 5
void pce_scsi_cdda_get(uint32_t out[PCE_SCSI_CDDA_STATE_WORDS]);
void pce_scsi_cdda_set(const uint32_t in[PCE_SCSI_CDDA_STATE_WORDS]);

/* Full SCSI-engine snapshot (savestate): phase, in-flight READ, data-in buffer,
 * IRQ ports, chunked-ADPCM-DMA progress. Games (Cotton) save mid-transfer; a
 * plain reset on load leaves them polling $1802/$1803 forever for a reply that
 * no longer exists. Fixed-layout struct, streamed as a tagged 'SCSX' block. */
typedef struct {
    uint8_t  phase, db, message, cmd_idx;
    uint8_t  flags;            /* bit0..5 = bsy,req,msg,cd,io,ack */
    uint8_t  reading, bulk, adpcm_dma_active;
    uint8_t  cmd[16];
    int32_t  din_pos, din_len;
    uint32_t read_lba, read_remain;
    uint8_t  port2, port3, adpcm_ctrl, pad0;
    uint32_t adpcm_dma_total;
    /* $180F fader state (added for savestate completeness) */
    uint32_t fader_cdda_vol;   /* Q16: 0=silent, 65536=full */
    uint32_t fader_adpcm_vol;
    uint32_t fader_step;       /* per-frame decrement (0 = not fading) */
    uint8_t  fader_adpcm;      /* 1 = ADPCM fade target, 0 = CD-DA */
    uint8_t  pad1[3];
    uint8_t  din[2048];
} pce_scsi_state_t;
void pce_scsi_state_get(pce_scsi_state_t *st);
void pce_scsi_state_set(const pce_scsi_state_t *st);
void pce_scsi_post_restore(void);   /* update IRQ2 after CD savestate blocks */
