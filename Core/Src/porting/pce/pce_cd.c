/* PC Engine CD — CUE/BIN disc layer. See pce_cd.h. */
#include "pce_cd.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Resolve a .cue FILE reference (relative) against the cue's own directory. */
static void resolve_bin_path(const char *cue_path, const char *name, char *out, size_t out_size)
{
    const char *slash = strrchr(cue_path, '/');
    if (slash) {
        size_t dir_len = (size_t)(slash - cue_path) + 1; /* keep trailing '/' */
        if (dir_len >= out_size) dir_len = out_size - 1;
        memcpy(out, cue_path, dir_len);
        out[dir_len] = '\0';
        strncat(out, name, out_size - strlen(out) - 1);
    } else {
        snprintf(out, out_size, "%s", name);
    }
}

static long file_size_bytes(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz;
}

/* Strip CR/LF and return pointer to first non-space char. */
static char *trim(char *s)
{
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

bool pce_cd_parse_cue(const char *cue_path, pce_cd_toc_t *toc)
{
    FILE *cue = fopen(cue_path, "rb");
    if (!cue) return false;

    memset(toc, 0, sizeof(*toc));

    char     cur_bin[256] = {0};
    uint32_t cur_file_base_lba = 0;   /* absolute LBA at offset 0 of cur_bin */
    uint32_t running_lba = 0;         /* total sectors across files seen so far */
    int      ti = -1;                 /* current track index */
    char     line[512];

    while (fgets(line, sizeof(line), cue)) {
        char *p = trim(line);

        if (strncmp(p, "FILE", 4) == 0) {
            const char *q1 = strchr(p, '"');
            const char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
            if (!q1 || !q2) continue;
            char name[256];
            size_t n = (size_t)(q2 - q1 - 1);
            if (n >= sizeof(name)) n = sizeof(name) - 1;
            memcpy(name, q1 + 1, n);
            name[n] = '\0';

            resolve_bin_path(cue_path, name, cur_bin, sizeof(cur_bin));
            cur_file_base_lba = running_lba;
            long sz = file_size_bytes(cur_bin);
            if (sz > 0)
                running_lba += (uint32_t)(sz / PCE_CD_SECTOR_RAW);
        }
        else if (strncmp(p, "TRACK", 5) == 0) {
            int num = 0;
            char mode[32] = {0};
            if (sscanf(p, "TRACK %d %31s", &num, mode) != 2) continue;
            if (toc->num_tracks >= PCE_CD_MAX_TRACKS) break;
            ti = toc->num_tracks++;
            pce_cd_track_t *t = &toc->tracks[ti];
            t->number = (uint8_t)num;
            t->type = (strncmp(mode, "AUDIO", 5) == 0) ? PCE_TRACK_AUDIO : PCE_TRACK_DATA;
            t->sector_size = (strstr(mode, "/2048")) ? 2048 : PCE_CD_SECTOR_RAW;
            strncpy(t->bin_path, cur_bin, sizeof(t->bin_path) - 1);
        }
        else if (strncmp(p, "INDEX", 5) == 0 && ti >= 0) {
            int idx = 0, mm = 0, ss = 0, ff = 0;
            if (sscanf(p, "INDEX %d %d:%d:%d", &idx, &mm, &ss, &ff) != 4) continue;
            if (idx != 1) continue;                 /* INDEX 01 = track start */
            uint32_t frames = (uint32_t)((mm * 60 + ss) * 75 + ff);
            pce_cd_track_t *t = &toc->tracks[ti];
            t->start_lba = cur_file_base_lba + frames;
            t->file_offset = frames * t->sector_size;
        }
    }
    fclose(cue);

    if (toc->num_tracks == 0) return false;

    toc->total_lba = running_lba;
    /* Track lengths = gap to next track's start (last reaches end of disc). */
    for (int i = 0; i < toc->num_tracks; i++) {
        uint32_t end = (i + 1 < toc->num_tracks) ? toc->tracks[i + 1].start_lba : toc->total_lba;
        toc->tracks[i].length_lba = (end > toc->tracks[i].start_lba) ? (end - toc->tracks[i].start_lba) : 0;
    }
    return true;
}

int pce_cd_track_at_lba(const pce_cd_toc_t *toc, uint32_t lba)
{
    for (int i = 0; i < toc->num_tracks; i++) {
        uint32_t s = toc->tracks[i].start_lba;
        if (lba >= s && lba < s + toc->tracks[i].length_lba)
            return i;
    }
    return -1;
}

/* Persistent .bin handles. fopen/fclose per sector is a FatFs directory walk each
 * time — fatal for CD-DA streaming (~75 sectors/s). Keep the file open and only reopen
 * when the track's .bin path actually changes.
 *
 * TWO independent handles (FatFs allows up to 10 open files, FF_FS_LOCK=0):
 *   slot 0 = SCSI data reads (program/graphics, the data track .bin)
 *   slot 1 = CD-DA streaming (audio tracks — usually a DIFFERENT .bin per track)
 * With sound ON, cdda_fill() reads an audio track every frame while the SCSI engine
 * reads the data track. With a SINGLE shared handle those two thrash it — fclose+fopen
 * an 8MB .bin 60x/s (FatFs dir walk) — which on-device starved the emulation enough to
 * break the game's CD timing (all 4 CD games ran with SOUND OFF, hung with sound on).
 * Separate handles = no cross-track thrash. */
static FILE *s_bin_f[2];
static char  s_bin_path[2][256];
/* Cached file offsets: skip f_lseek when the next read is sequential (CD-DA
 * streaming is always linear; f_lseek on every batch was pure FatFs overhead). */
static long  s_bin_pos[2] = { -1, -1 };

void pce_cd_close(void)
{
    for (int i = 0; i < 2; i++) {
        if (s_bin_f[i]) { fclose(s_bin_f[i]); s_bin_f[i] = NULL; }
        s_bin_path[i][0] = 0;
        s_bin_pos[i] = -1;
    }
}

/* After sdcard_deinit() + remount on sleep/wake the cached FILE* are
 * dangling — do NOT fclose them (FatFs volume was torn down). Drop the
 * pointers so the next sector read fopen()'s fresh. Reactive reopen-on-
 * failure alone still costs a few bad CD-DA fills → audible dropouts. */
void pce_cd_invalidate_handles(void)
{
    for (int i = 0; i < 2; i++) {
        s_bin_f[i] = NULL;
        s_bin_path[i][0] = 0;
        s_bin_pos[i] = -1;
    }
}

/* Seek only when the cached position diverges (non-sequential jump). */
static bool bin_seek_slot(int slot, FILE *f, long offset)
{
    if (s_bin_pos[slot] == offset)
        return true;
    if (fseek(f, offset, SEEK_SET) != 0) {
        s_bin_pos[slot] = -1;
        return false;
    }
    s_bin_pos[slot] = offset;
    return true;
}

static bool read_sector_slot(int slot, const pce_cd_toc_t *toc, uint32_t lba, uint8_t *buf)
{
    int ti = pce_cd_track_at_lba(toc, lba);
    if (ti < 0) return false;
    const pce_cd_track_t *t = &toc->tracks[ti];

    uint32_t sec_in_track = lba - t->start_lba;
    long offset = (long)t->file_offset + (long)sec_in_track * (long)t->sector_size;

    /* Two attempts: the persistent handle goes STALE when the device sleeps
     * (SD powered down/remounted) — every read then fails forever because the
     * path never changes, so the reopen branch never triggered. On any failure,
     * drop the handle and reopen once ("wake after sleep froze PCE-CD"). */
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!s_bin_f[slot] || strcmp(s_bin_path[slot], t->bin_path) != 0) {
            if (s_bin_f[slot]) fclose(s_bin_f[slot]);
            s_bin_f[slot] = fopen(t->bin_path, "rb");
            if (!s_bin_f[slot]) { s_bin_path[slot][0] = 0; s_bin_pos[slot] = -1; continue; }
            strncpy(s_bin_path[slot], t->bin_path, sizeof(s_bin_path[slot]) - 1);
            s_bin_path[slot][sizeof(s_bin_path[slot]) - 1] = 0;
            s_bin_pos[slot] = -1;
        }
        FILE *f = s_bin_f[slot];
        if (bin_seek_slot(slot, f, offset)) {
            if (t->sector_size == PCE_CD_SECTOR_RAW) {
                if (fread(buf, 1, PCE_CD_SECTOR_RAW, f) == PCE_CD_SECTOR_RAW) {
                    s_bin_pos[slot] += PCE_CD_SECTOR_RAW;
                    return true;
                }
            } else {
                /* 2048-byte user data: place it where a MODE1 sector keeps user
                 * bytes (offset 16) and zero the sync/header/ECC frame around it. */
                memset(buf, 0, PCE_CD_SECTOR_RAW);
                if (fread(buf + 16, 1, 2048, f) == 2048) {
                    s_bin_pos[slot] += 2048;
                    return true;
                }
            }
        }
        /* stale/broken handle: force a fresh fopen on the retry */
        fclose(s_bin_f[slot]);
        s_bin_f[slot] = NULL;
        s_bin_path[slot][0] = 0;
        s_bin_pos[slot] = -1;
    }
    return false;
}

/* SCSI data path (slot 0). */
bool pce_cd_read_sector(const pce_cd_toc_t *toc, uint32_t lba, uint8_t *buf)
{
    return read_sector_slot(0, toc, lba, buf);
}

/* CD-DA streaming path (slot 1) — its own handle so it never thrashes the data handle. */
bool pce_cd_read_sector_audio(const pce_cd_toc_t *toc, uint32_t lba, uint8_t *buf)
{
    return read_sector_slot(1, toc, lba, buf);
}

/* Batched CD-DA read: up to `max_n` CONTIGUOUS raw sectors in a single fread
 * (clamped to the current track so the file/offset stays linear). One 8-sector
 * fread costs far less FatFs overhead than 8 sector-sized ones — the per-sector
 * reads were the "subtly slow with BGM on" drag on device. Returns sectors read. */
int pce_cd_read_sectors_audio(const pce_cd_toc_t *toc, uint32_t lba, uint8_t *buf, int max_n)
{
    int ti = pce_cd_track_at_lba(toc, lba);
    if (ti < 0 || max_n <= 0) return 0;
    const pce_cd_track_t *t = &toc->tracks[ti];
    if (t->sector_size != PCE_CD_SECTOR_RAW)      /* audio must be raw 2352B */
        return pce_cd_read_sector_audio(toc, lba, buf) ? 1 : 0;

    uint32_t track_end = (ti + 1 < toc->num_tracks) ? toc->tracks[ti + 1].start_lba
                                                    : toc->total_lba;
    uint32_t remain = (track_end > lba) ? (track_end - lba) : 1;
    int n = (max_n < (int)remain) ? max_n : (int)remain;

    uint32_t sec_in_track = lba - t->start_lba;
    long offset = (long)t->file_offset + (long)sec_in_track * PCE_CD_SECTOR_RAW;
    /* Same stale-handle self-heal as read_sector_slot (sleep/wake). */
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!s_bin_f[1] || strcmp(s_bin_path[1], t->bin_path) != 0) {
            if (s_bin_f[1]) fclose(s_bin_f[1]);
            s_bin_f[1] = fopen(t->bin_path, "rb");
            if (!s_bin_f[1]) { s_bin_path[1][0] = 0; s_bin_pos[1] = -1; continue; }
            strncpy(s_bin_path[1], t->bin_path, sizeof(s_bin_path[1]) - 1);
            s_bin_path[1][sizeof(s_bin_path[1]) - 1] = 0;
            s_bin_pos[1] = -1;
        }
        if (bin_seek_slot(1, s_bin_f[1], offset)) {
            size_t want = (size_t)n * PCE_CD_SECTOR_RAW;
            size_t got  = fread(buf, 1, want, s_bin_f[1]);
            if (got > 0) {
                s_bin_pos[1] += (long)got;
                return (int)(got / PCE_CD_SECTOR_RAW);
            }
        }
        fclose(s_bin_f[1]);
        s_bin_f[1] = NULL;
        s_bin_path[1][0] = 0;
        s_bin_pos[1] = -1;
    }
    return 0;
}
