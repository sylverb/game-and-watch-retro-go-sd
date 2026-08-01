/* PC Engine CD — CUE/BIN disc layer (Phase 2, step 1: TOC + sector reads).
 *
 * Parses a .cue sheet into a track table and streams 2352-byte sectors from
 * the .bin file(s) on SD by absolute LBA. The $1800 CD-ROM2 (SCSI) handler in
 * the core calls pce_cd_read_sector(); no decompression, just fseek/fread. */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define PCE_CD_MAX_TRACKS   100
#define PCE_CD_SECTOR_RAW   2352      /* bytes per raw sector (MODE1/2352, AUDIO) */
#define PCE_CD_LEADIN_LBA   150       /* 2 s pre-gap: MSF 00:02:00 == LBA 0 */

/* CD-ROM2 program RAM mapped into the HuC6280 bank space. Banks 0x80-0x87 are
 * the 64KB base CD RAM present on every CD system; banks 0x68-0x7F add the
 * 192KB Super CD-ROM2 RAM. They are contiguous (0x68..0x87 = 32 banks), so a
 * single 256KB block backs them all. Without this the banks alias the shared
 * 8KB NULLRAM and a loaded game corrupts itself. */
#define PCE_CD_RAM_FIRST_BANK  0x68
#define PCE_CD_RAM_LAST_BANK   0x87
#define PCE_CD_RAM_BANK_SIZE   0x2000
#define PCE_CD_RAM_SIZE        ((PCE_CD_RAM_LAST_BANK - PCE_CD_RAM_FIRST_BANK + 1) * PCE_CD_RAM_BANK_SIZE) /* 256KB */
#define PCE_CD_RAM_BASE_BANK   0x80   /* base 64KB (0x80-0x87) — minimum for any CD game */

typedef enum {
    PCE_TRACK_AUDIO = 0,
    PCE_TRACK_DATA  = 1,
} pce_track_type_t;

typedef struct {
    uint8_t          number;       /* 1-based track number */
    pce_track_type_t type;
    uint32_t         sector_size;  /* on-disc bytes per sector (2352 or 2048) */
    uint32_t         start_lba;    /* absolute disc LBA of INDEX 01 */
    uint32_t         length_lba;   /* sectors in this track */
    char             bin_path[256];/* full path to the .bin holding this track */
    uint32_t         file_offset;  /* byte offset of INDEX 01 within bin_path */
} pce_cd_track_t;

typedef struct {
    int            num_tracks;
    uint32_t       total_lba;      /* total program-area length in sectors */
    pce_cd_track_t tracks[PCE_CD_MAX_TRACKS];
} pce_cd_toc_t;

/* Parse cue_path into *toc. Returns false on open/parse error. */
bool pce_cd_parse_cue(const char *cue_path, pce_cd_toc_t *toc);

/* Read one PCE_CD_SECTOR_RAW-byte sector at absolute LBA into buf (must be
 * >= 2352 bytes). For 2048-byte data tracks the 2352-byte sync/header frame is
 * synthesised around the 2048 user bytes. Returns false past end / on I/O error. */
bool pce_cd_read_sector(const pce_cd_toc_t *toc, uint32_t lba, uint8_t *buf);

/* Same, but uses an independent file handle for CD-DA streaming so it never thrashes
 * the SCSI data handle (audio + data are usually different .bin files, both read every
 * frame with sound on). FatFs allows it (10 open files). */
bool pce_cd_read_sector_audio(const pce_cd_toc_t *toc, uint32_t lba, uint8_t *buf);
/* Batched variant: up to max_n contiguous raw sectors in one fread (clamped to the
 * track). Returns sectors actually read. */
int pce_cd_read_sectors_audio(const pce_cd_toc_t *toc, uint32_t lba, uint8_t *buf, int max_n);

/* Close the cached .bin handles (call on mount to start fresh, avoid handle leaks). */
void pce_cd_close(void);

/* Drop cached handles without fclose — use after sleep/wake remount when the
 * FatFs volume was torn down and the old FILE* are dangling. */
void pce_cd_invalidate_handles(void);

/* Index of the track containing absolute LBA, or -1. */
int pce_cd_track_at_lba(const pce_cd_toc_t *toc, uint32_t lba);
