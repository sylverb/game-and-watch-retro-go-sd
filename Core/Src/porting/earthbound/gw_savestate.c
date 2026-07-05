/*
 * G&W savestate storage backend — file-backed crash-safe ping-pong slots.
 *
 * Implements the platform_savestate_* contract (src/platform/platform.h): the
 * upstream state_dump engine writes a ~141 KiB suspend/resume snapshot through
 * two SAVESTATE_SLOTS, targeting the inactive slot and reading the newest valid
 * one, so a power loss mid-write leaves the prior slot intact.
 *
 * Unlike the Unix port's fixed "savestate.bin.<slot>" names, a retro-go build
 * has MANY user-visible save slots (and a separate sleep snapshot). We therefore
 * key the two ping-pong files on a caller-supplied *base path* set via
 * eb_savestate_set_base() before each capture/load:
 *   slot 0 -> "<base>"        (the launcher's own slot path, so its existing
 *                              file-exists slot-occupancy check still works)
 *   slot 1 -> "<base>.1"      (the second ping-pong copy)
 *
 * I/O goes through FatFs directly (f_open/f_lseek/f_write/f_sync/f_close) rather
 * than newlib stdio. The stdio path (fopen + hundreds of buffered fseek+fwrite
 * + a final fflush) failed for the large streaming snapshot: every fwrite
 * reported success but the closing fflush() returned -1 on a nearly-empty card —
 * a newlib buffering/seek interaction, not a disk error. FatFs is what stdio
 * calls underneath, is natively offset-addressed (1:1 with this API), and
 * returns precise FRESULT codes.
 */

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "platform/platform.h"
#include "main_earthbound.h"
#include "odroid_audio.h"

/* Base path for the current logical savestate; the two ping-pong files derive
 * from it. Set by eb_savestate_set_base() before host_request_capture/load. */
static char ss_base[256];
static char ss_slot_path[260];

static void ss_read_close(void);   /* defined below, by the read-handle cache */

void eb_savestate_set_base(const char *path)
{
    /* A new logical capture/load is about to begin — drop any cached read handle
     * from a prior operation so a stale path can't be reused. */
    ss_read_close();
    if (!path) {
        ss_base[0] = '\0';
        return;
    }
    strncpy(ss_base, path, sizeof(ss_base) - 1);
    ss_base[sizeof(ss_base) - 1] = '\0';
}

static const char *slot_path(int slot)
{
    if (ss_base[0] == '\0')
        return NULL;
    if (slot == 0)
        return ss_base;
    snprintf(ss_slot_path, sizeof(ss_slot_path), "%s.%d", ss_base, slot);
    return ss_slot_path;
}

/* One open writer at a time. The engine writes a single slot to completion
 * (begin -> writes -> commit) before touching another, so one FIL suffices;
 * ss_wslot tracks which slot owns it (-1 = none open). FF_FS_TINY makes FIL
 * small (no per-file sector buffer), so this costs almost no RAM. */
static FIL ss_wfil;
static int ss_wslot = -1;

/* One cached read handle, reused across the many small reads a single load makes.
 * The engine reads each slot ~100+ times per load (4 KiB CRC chunks over BOTH
 * ping-pong slots during the peek, the chosen slot's CRC again in read_slot, plus
 * the per-section reads). Re-opening per call re-walks the directory tree every
 * time — tens of ms each on the slow SD, ~5 s total. Keeping the file open for
 * consecutive same-path reads collapses that to a handful of opens. ss_ropen holds
 * the path the handle is open for ("" = closed). */
static FIL ss_rfil;
static char ss_ropen[sizeof ss_slot_path];

static void ss_read_close(void)
{
    if (ss_ropen[0]) {
        f_close(&ss_rfil);
        ss_ropen[0] = '\0';
    }
}

bool platform_savestate_begin(int slot)
{
    if (slot < 0 || slot >= SAVESTATE_SLOTS)
        return false;
    const char *path = slot_path(slot);
    if (!path)
        return false;
    ss_read_close();   /* don't hold a read handle open while we truncate/rewrite */
    if (ss_wslot >= 0) {            /* a prior capture never committed — drop it */
        f_close(&ss_wfil);
        ss_wslot = -1;
    }
    FRESULT r = f_open(&ss_wfil, path, FA_WRITE | FA_CREATE_ALWAYS); /* truncate */
    if (r != FR_OK)
        return false;
    ss_wslot = slot;
    return true;
}

bool platform_savestate_write(int slot, size_t offset, const void *src, size_t size)
{
    if (slot != ss_wslot)
        return false;
    if (f_lseek(&ss_wfil, (FSIZE_t)offset) != FR_OK)
        return false;
    UINT bw = 0;
    if (f_write(&ss_wfil, src, (UINT)size, &bw) != FR_OK || bw != size)
        return false;
    return true;
}

bool platform_savestate_commit(int slot)
{
    if (slot != ss_wslot)
        return false;
    FRESULT rs = f_sync(&ss_wfil);
    FRESULT rc = f_close(&ss_wfil);
    ss_wslot = -1;
    return (rs == FR_OK && rc == FR_OK);
}

size_t platform_savestate_read(int slot, size_t offset, void *dst, size_t size)
{
    if (slot < 0 || slot >= SAVESTATE_SLOTS)
        return 0;
    const char *path = slot_path(slot);
    if (!path)
        return 0;
    /* (Re)open only when the target path changes; successive reads of the same
     * slot reuse the cached handle. Closed on a write (begin) or a new logical
     * operation (eb_savestate_set_base). */
    if (strcmp(ss_ropen, path) != 0) {
        ss_read_close();
        if (f_open(&ss_rfil, path, FA_READ) != FR_OK)
            return 0;
        strncpy(ss_ropen, path, sizeof ss_ropen - 1);
        ss_ropen[sizeof ss_ropen - 1] = '\0';
    }
    UINT br = 0;
    if (f_lseek(&ss_rfil, (FSIZE_t)offset) == FR_OK)
        f_read(&ss_rfil, dst, (UINT)size, &br);
    return br;
}

/* Gate the SAI ring's output across the blocking save/load I/O. state_dump_*_slots()
 * runs at the root boundary with the game loop (and thus eb_audio_pump) stalled, so
 * the free-running SAI ISR would otherwise drain the ring in ~133 ms and then drone
 * the last frame for the rest of the ~0.6-1 s of SD I/O. Muting makes eb_audio_dma_refill
 * emit silence instead; the producer is frozen so head/tail don't move and the ring is
 * intact on unmute (save resumes seamlessly; load then refills over the stale frames).
 * Safe here precisely because the producer isn't spinning — muting while eb_audio_pump
 * runs would deadlock it (the muted ISR stops freeing ring slots). */
void platform_savestate_freeze_audio(bool freeze)
{
    odroid_audio_mute(freeze);
}
