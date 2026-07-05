/*
 * G&W audio platform — drives the lakesnes SPC700/DSP emulator into the SAI DMA
 * through a decoupling ring buffer.
 *
 * Native rate: the SPC700 DSP produces ~533.33 samples per NTSC frame at 32 kHz
 * (dsp_getSamples resamples to exactly 534). The SAI is configured for 32 kHz
 * mono (odroid_audio_init(32000)), so one game frame == one SAI half-buffer.
 *
 * Why a ring buffer (not a frame-pumped 2-half buffer):
 *
 *   EarthBound renders well below 60 fps and relies on frame-skip to keep game
 *   logic + audio running at 60 Hz. With the old design the pump wrote one
 *   frame into the active DMA half every frame, but the loop only *waited* for
 *   a DMA boundary on rendered frames. On a skip frame the pump ran immediately
 *   with the DMA state unchanged, overwriting the half it had just filled — so
 *   the previous frame's audio was discarded before it played. With ~45% of
 *   frames skipped, a large fraction of generated audio never reached the
 *   speaker → discontinuous, crunchy output. On top of that, a 2-half (~33 ms)
 *   buffer has no slack to ride out a ~26 ms render during which no audio is
 *   produced.
 *
 *   The ring fixes both. The producer (eb_audio_pump, once per game frame)
 *   pushes exactly one frame per call and never overwrites unconsumed data. The
 *   consumer (the SAI Tx ISR, via audio_set_dma_refill_callback) pops one frame
 *   per DMA half into the just-freed half — so the buffer stays fed even during
 *   a long render, and every produced frame plays exactly once in order. The
 *   ring depth provides the jitter slack the 2-half buffer lacked.
 *
 * Pacing: real-time, driven by ring fill level (replaces common_emu_sound_sync,
 * and removes the skip-frame coupling entirely — see gw_timer.c). When the ring
 * is full the loop is keeping pace; eb_audio_pump blocks for one freed slot and
 * produces one frame, so the APU advances at 60 Hz. When the ring is NOT full
 * the render-bound loop has fallen below 60 Hz (heavy scene at
 * MAX_CONSEC_SKIP=1, or recovering from a long synchronous op); the pump then
 * refills every free slot without blocking, so the APU steps at the SAI's
 * real-time rate. The game's visuals/logic slow under load while music keeps
 * correct tempo, rather than the ring draining to an underrun. If the ring does
 * empty anyway (a single op blocking the loop past the ring's depth, e.g. a
 * music-pack load), the ISR replays the last frame — see eb_audio_dma_refill.
 *
 * Concurrency: single producer (main loop, writes head), single consumer (SAI
 * ISR, writes tail). 32-bit aligned accesses are atomic on the M7; a DMB orders
 * the ring writes before the head publish. No lock needed.
 */

#include <string.h>
#include <stdio.h>

#include "platform/platform.h"
#include "game/audio.h"

#include "common.h"
#include "gw_audio.h"
#include "odroid_audio.h"
#include "porting/common.h"

extern void wdog_refresh(void);
/* gw_video.c — issues a deferred LCD present once the previous one latched. */
extern void eb_video_service(void);

#define EB_AUDIO_SAMPLES_PER_FRAME 534  /* 32000 / 60 ≈ 533.33 */

/* Ring depth in frames. MUST be a power of two (index masking). Steady-state
 * audio latency ≈ EB_RING_FRAMES × 16.69 ms because back-pressure keeps the
 * ring near full. 8 frames (~133 ms) gives generous slack to ride consecutive
 * long renders; drop to 4 (~67 ms) if SFX feel laggy and crunch stays gone. */
#define EB_RING_FRAMES 8
#define EB_RING_MASK   (EB_RING_FRAMES - 1)

/* Post-mix loudness boost, Q8 fixed point (256 = unity, 512 = 2.0x, +6 dB).
 *
 * The lakesnes DSP reproduces EarthBound's native mix faithfully, and EB's own
 * sound driver leaves a lot of headroom below full-scale int16 — so unmodified
 * output is noticeably quieter than the other G&W cores (whose source material
 * is hotter). This applies a fixed make-up gain after the volume scaling, with
 * saturating clamp so loud SFX clip cleanly to int16 instead of wrapping.
 * 384 (1.5x) is conservative; 512 (2x) is punchier with occasional soft
 * clipping on the loudest passages. */
#define EB_AUDIO_GAIN_Q8 512

/* Stereo scratch — audio_generate_samples writes L,R,L,R interleaved. */
static int16_t eb_stereo_scratch[EB_AUDIO_SAMPLES_PER_FRAME * 2];

/* Ring of fully-mixed (mono, volume-applied) frames. */
static int16_t eb_ring[EB_RING_FRAMES][EB_AUDIO_SAMPLES_PER_FRAME];
static volatile uint16_t eb_ring_head;  /* producer (main loop) writes */
static volatile uint16_t eb_ring_tail;  /* consumer (SAI ISR) writes */

/* Diagnostic: DMA halves the ISR had to fill from a starved ring (see report
 * in eb_audio_pump). Written by the ISR, read by the main loop. */
static volatile uint32_t eb_underrun_frames;

/* Diagnostic: DWT cycles spent generating+mixing audio (excludes the WFI
 * pacing wait in eb_audio_pump). Read via GDB: cycles_acc / frames_acc =
 * average APU cost per produced frame. Non-static so the symbol survives. */
volatile uint32_t eb_audio_prod_cycles;
volatile uint32_t eb_audio_prod_frames;

static inline uint16_t eb_ring_count(void)
{
    return (uint16_t)(eb_ring_head - eb_ring_tail);
}

/* SAI Tx ISR refill: pop one frame into the just-freed DMA half. Kept tiny —
 * a single memcpy. On underrun (ring empty) replay the most recent frame
 * instead of silence: a brief tempo hold is far less harsh than a click to
 * zero. The producer won't overwrite the (tail-1) slot until head laps all the
 * way around the ring, so it stays valid to replay. At startup that slot is
 * still zero-filled BSS, i.e. silence. */
static void eb_audio_dma_refill(int16_t *dst, uint16_t samples)
{
    uint16_t n = (samples < EB_AUDIO_SAMPLES_PER_FRAME)
                 ? samples : EB_AUDIO_SAMPLES_PER_FRAME;
    const int16_t *src;

    /* Honor the global mute, set by odroid_audio_mute() when the retro-go
     * overlay/menu opens (and for volume/brightness chords). Standard ports go
     * silent here for free: the menu zeroes the DMA buffer and, lacking a
     * refill callback, it stays zeroed. EB *does* refill, so without this check
     * the ISR keeps draining the ring and then drones the last frame on
     * underrun while the menu is up. The producer (eb_audio_pump) is frozen in
     * the modal menu, so the ring is left intact and playback resumes exactly
     * where it paused once unmuted. */
    if (audio_mute) {
        memset(dst, 0, samples * sizeof(int16_t));
        return;
    }

    if (eb_ring_head == eb_ring_tail) {
        eb_underrun_frames++;
        src = eb_ring[(eb_ring_tail - 1) & EB_RING_MASK];  /* repeat last */
    } else {
        src = eb_ring[eb_ring_tail & EB_RING_MASK];
        eb_ring_tail++;
    }

    memcpy(dst, src, n * sizeof(int16_t));
    if (samples > n)
        memset(dst + n, 0, (samples - n) * sizeof(int16_t));
}

bool platform_audio_init(void)
{
    eb_ring_head = 0;
    eb_ring_tail = 0;
    audio_init();
    audio_set_dma_refill_callback(eb_audio_dma_refill);
    audio_start_playing(EB_AUDIO_SAMPLES_PER_FRAME);
    return true;
}

void platform_audio_shutdown(void)
{
    audio_set_dma_refill_callback(NULL);
    audio_stop_playing();
    audio_shutdown();
}

/* Re-arm SAI DMA playback after a STANDBY-hibernation resume WITHOUT touching
 * the lakesnes APU/DSP. The full platform_audio_init() calls audio_init(),
 * which re-inits and resets the SPC700/DSP engine — that would discard the
 * restored music state and drop to silence until the next area change. On
 * resume the APU struct (RAM_EMU) is already restored, so we only need to:
 *   - reset the decoupling ring to empty (the AHB DMA buffer is fresh after a
 *     cold boot; eb_audio_pump refills from the preserved APU on the next
 *     frame),
 *   - re-attach the ISR refill callback and start the SAI DMA,
 *   - re-enable the speaker.
 * odroid_audio_init() (SAI hardware setup) was already re-run this boot by
 * odroid_system_init() in app_main_earthbound before the restore branch. */
void platform_audio_rearm(void)
{
    eb_ring_head = 0;
    eb_ring_tail = 0;
    audio_set_dma_refill_callback(eb_audio_dma_refill);
    audio_start_playing(EB_AUDIO_SAMPLES_PER_FRAME);
    HAL_GPIO_WritePin(GPIO_Speaker_enable_GPIO_Port, GPIO_Speaker_enable_Pin,
                      GPIO_PIN_SET);
}

void platform_audio_lock(void) {}
void platform_audio_unlock(void) {}

/* Generate, mix, and push exactly one frame into the ring. Steps the SPC/APU
 * by one frame of audio (audio_generate_samples runs apu_runCycles), so the
 * caller's rate of calling this == the music's playback rate. */
static void eb_produce_frame(void)
{
    uint32_t _t0 = DWT->CYCCNT;
    int16_t *dst = eb_ring[eb_ring_head & EB_RING_MASK];

    if (common_emu_sound_loop_is_muted()) {
        memset(dst, 0, EB_AUDIO_SAMPLES_PER_FRAME * sizeof(int16_t));
    } else {
        audio_generate_samples(eb_stereo_scratch, EB_AUDIO_SAMPLES_PER_FRAME);

        int16_t factor = common_emu_sound_get_volume();
        for (uint16_t i = 0; i < EB_AUDIO_SAMPLES_PER_FRAME; i++) {
            int32_t mono = ((int32_t)eb_stereo_scratch[i * 2] +
                            (int32_t)eb_stereo_scratch[i * 2 + 1]) >> 1;
            /* Volume scale, then make-up gain. Split shifts keep the product in
             * int32: mono*factor ≤ 32767*255 ≈ 8.4M, then >>8 before the gain
             * multiply so (<=32767)*512 stays well within range. */
            int32_t v = ((mono * factor) >> 8) * EB_AUDIO_GAIN_Q8 >> 8;
            if (v > 32767) v = 32767;
            else if (v < -32768) v = -32768;
            dst[i] = (int16_t)v;
        }
    }

    /* Publish: ensure the ring writes land before the head advance the ISR
     * observes. */
    __DMB();
    eb_ring_head++;

    eb_audio_prod_cycles += DWT->CYCCNT - _t0;
    eb_audio_prod_frames++;
}

void eb_audio_pump(void)
{
    /* Real-time-paced production. Two regimes:
     *
     *   Keeping up (ring full): block for one freed slot, then produce exactly
     *   one frame. The SAI ISR frees a slot every ~16.69 ms, so this is the
     *   frame loop's pacing clock and the APU advances at 60 Hz — correct
     *   tempo, full double-buffer slack.
     *
     *   Behind (ring not full): the render-bound loop is running below 60 Hz
     *   (heavy scene at MAX_CONSEC_SKIP=1, or recovering from a long
     *   synchronous op like a music change). Don't block — refill every free
     *   slot so the APU steps at the SAI's real-time rate regardless of how
     *   slow the loop is. Net effect: the game's visuals/logic slow under load
     *   while music keeps correct tempo, instead of the ring draining to an
     *   underrun. The refill is bounded by ring depth, so a single pump runs
     *   the APU for at most EB_RING_FRAMES frames (~16 ms of work) — safe under
     *   the WWDG. Watchdog refreshed each spin in case the preceding render ran
     *   long. */
    while (eb_ring_count() >= EB_RING_FRAMES) {
        wdog_refresh();
        /* Issue any deferred LCD present (gw_video.c) — the vblank latch we
         * would otherwise block on in end_frame passes during this wait, so
         * the video and audio waits overlap instead of summing. */
        eb_video_service();
        cpumon_sleep();  /* __WFI; wakes on the SAI ISR that frees a slot */
    }
    eb_video_service();

    uint16_t want = (uint16_t)(EB_RING_FRAMES - eb_ring_count());
    for (uint16_t i = 0; i < want; i++)
        eb_produce_frame();

#ifdef EB_PPU_PROFILE
    /* Throttled starvation report (~every 4 s) so heavy-scene / long-op
     * underruns show up on `make monitor` without spamming. */
    {
        static uint32_t pumps, last_underruns;
        if (++pumps >= 240) {
            uint32_t u = eb_underrun_frames;
            if (u != last_underruns)
                printf("AUDIO: %lu underrun frames (+%lu in last ~4s)\n",
                       (unsigned long)u, (unsigned long)(u - last_underruns));
            last_underruns = u;
            pumps = 0;
        }
    }
#endif
}
