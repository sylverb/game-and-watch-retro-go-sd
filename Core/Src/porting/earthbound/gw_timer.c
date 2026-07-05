/*
 * G&W timer platform — monotonic clock via DWT->CYCCNT (CPU-cycle resolution,
 * ~280 MHz on H7B0). HAL_GetTick (1 ms) is retained only for the FPS IIR
 * filter, which is intentionally ms-scaled.
 *
 * Frame presentation lives in gw_video.c::platform_video_end_frame(); this
 * file only does timing. host_process_frame() only calls frame_end() on the
 * fast-forward path, so a swap here would never fire in the normal path.
 *
 * Audio is pumped from platform_timer_frame_start (called at the bottom of
 * host_process_frame, every frame, including frame-skipped ones) so the
 * SAI DMA never underruns when the renderer falls behind.
 *
 * Frame pacing splits the two concerns: platform_timer_should_render() picks
 * the render-skip state at the top of host_process_frame (DWT-clocked, hard
 * skip cap — see that function for why not common_emu_frame_loop), and the loop
 * is throttled to wall-clock by the audio ring's back-pressure rather than by
 * sleep_until. eb_audio_pump() (gw_audio.c, run from platform_timer_frame_start)
 * blocks until the SAI ISR frees a ring slot — once per DMA half (~16.69 ms) —
 * so generation is phase-locked to playback and the DMA is the master clock.
 * platform_timer_sleep_until() therefore no longer calls common_emu_sound_sync()
 * (that would double-pace and starve the buffer). Requires
 * EB_HOST_PACED_FRAMESKIP for the EarthBound build.
 */

#include "platform/platform.h"
#include "stm32h7xx_hal.h"
#include "gw_lcd.h"
#include "porting/common.h"

extern void wdog_refresh(void);
extern void eb_audio_pump(void);

/* IIR-filtered frame period in ms, scaled by (1 << FPS_IIR_SHIFT). 60 FPS →
 * 16.67 ms → steady-state period_acc ≈ 267. Shift=4 (factor 1/16) matches
 * the smoothing upstream uses for its logic/render accumulators. */
#define FPS_IIR_SHIFT 4
static uint32_t period_acc;
static uint32_t last_fps_tick;

/* 32→64-bit extension of DWT->CYCCNT. CYCCNT wraps every ~15 s at 280 MHz;
 * platform_timer_ticks() is called every frame, so any single call observes
 * at most one wrap since the previous call. Single-threaded (game fiber +
 * host loop run sequentially), so no synchronization needed. */
static uint32_t last_cyc;
static uint64_t cyc_high;

#ifdef EB_HOST_PACED_FRAMESKIP
/* Frame-skip controller state (platform_timer_should_render), file-scope so
 * platform_timer_init() can reset it. This is CRITICAL after a STANDBY
 * hibernation resume: these statics live in RAM_EMU and are restored from the
 * snapshot with a stale (pre-sleep) deadline, while the hardware DWT->CYCCNT —
 * and platform_timer_ticks()'s cyc_high — restart near zero on the cold boot.
 * If they aren't reset, (now - skip_deadline) stays hugely negative and the
 * skip branch never fires, so the renderer draws every frame and game
 * logic/audio drop to ~half speed (the exact symptom frame-skip prevents). */
static uint64_t skip_deadline;
static int      skip_consecutive;
static bool     skip_inited;
#endif

bool platform_timer_init(void)
{
    period_acc = 0;
    last_fps_tick = 0;
    common_emu_enable_dwt_cycles();
    last_cyc = DWT->CYCCNT;
    cyc_high = 0;
#ifdef EB_HOST_PACED_FRAMESKIP
    /* Re-baseline the skip controller against the freshly-reset clock above. */
    skip_deadline = 0;
    skip_consecutive = 0;
    skip_inited = false;
#endif
    return true;
}

void platform_timer_shutdown(void) {}

void platform_timer_frame_start(void)
{
    /* Called once per frame from host_process_frame() regardless of frame
     * skip / fast-forward / sleep state. wdog_refresh() lives here (not just
     * inside platform_timer_sleep_until's while-loop) because once frame work
     * exceeds frame_period the sleep call returns instantly with no refresh,
     * and we'd WWDG-reset within a second. */
    wdog_refresh();

    /* Refill the inactive half of the SAI DMA buffer with one frame of
     * APU samples. Runs every frame so skipped renders still keep audio
     * flowing — the DMA cadence is locked to wall-clock, not draw-rate. */
    eb_audio_pump();
}

void platform_timer_frame_end(void)
{
    /* Fast-forward path only — see file header. Frame present is in
     * platform_video_end_frame(); here we just keep the watchdog happy. */
    wdog_refresh();
}

void platform_timer_update_fps(void)
{
    uint32_t now = HAL_GetTick();
    if (last_fps_tick != 0) {
        uint32_t period = now - last_fps_tick;
        period_acc = period_acc - (period_acc >> FPS_IIR_SHIFT) + period;
    }
    last_fps_tick = now;
}

void platform_timer_sleep_until(uint64_t deadline)
{
    /* Frame pacing is owned by the audio ring's back-pressure: eb_audio_pump()
     * (run from platform_timer_frame_start, immediately after this returns)
     * blocks until the SAI ISR frees a ring slot, which happens once per DMA
     * half (~16.69 ms). That phase-locks the loop to audio consumption, so the
     * DMA — not the CPU clock — is the frame's master clock. We therefore do
     * NOT call common_emu_sound_sync() here: doing so would add a second,
     * redundant DMA-half wait per rendered frame, dropping production below
     * consumption and starving the buffer. The upstream frame_deadline is
     * likewise ignored (see file header). Just keep the watchdog happy. */
    (void)deadline;
    wdog_refresh();
}

#ifdef EB_HOST_PACED_FRAMESKIP
bool platform_timer_should_render(void)
{
    /* Frame-skip + audio-pacing controller for EarthBound.
     *
     * EB renders well below 60 fps (~30 ms/frame on the overworld), so we keep
     * game logic + audio running at the 60 Hz target and skip *rendering* on a
     * frame once we have fallen a whole frame behind — capped at
     * MAX_CONSEC_SKIP consecutive skips so a perpetually over-budget renderer
     * still presents a frame periodically.
     *
     * This hook is what makes EB skip renders at all: the upstream built-in
     * frame-skip in host_process_frame is disabled for this port (game_main.c
     * defines EB_MAX_FRAME_SKIP 0, so its skip branch is never taken). Without
     * EB_HOST_PACED_FRAMESKIP, host_process_frame would render every
     * frame at ~30 ms, dragging game logic AND audio production down to ~33 Hz
     * — half-speed gameplay plus a starved audio ring (the SAI consumes 60
     * frames/s but only ~33 would be produced).
     *
     * Why not common_emu_frame_loop(): its integrator is clocked by
     * HAL_GetTick() (1 ms resolution — ~6% quantisation noise per ~16.7 ms
     * frame) and has no consecutive-skip cap, so for a core that can never
     * reach 60 fps it pins above its skip threshold and skips rendering
     * forever (constant choppiness). Here the deadline is tracked with the
     * cycle-accurate DWT clock (platform_timer_ticks), and the cap guarantees a
     * ~30 fps render floor (at most one skip per rendered frame).
     *
     * This decides render-vs-skip ONLY. Audio pacing is fully owned by the
     * ring buffer's back-pressure (gw_audio.c) and is independent of the skip
     * decision — so no skip_frames coupling is published here. */
    enum { MAX_CONSEC_SKIP = 1 };
    /* State is file-scope (skip_*) so platform_timer_init() can reset it across
     * a hibernation resume — see the declaration comment above. */
    uint64_t period = platform_timer_ticks_per_sec() / 60;
    uint64_t now = platform_timer_ticks();
    if (!skip_inited) { skip_deadline = now; skip_inited = true; }

    bool do_render;
    if ((int64_t)(now - skip_deadline) > (int64_t)period &&
        skip_consecutive < MAX_CONSEC_SKIP) {
        do_render = false;
        skip_consecutive++;
    } else {
        do_render = true;
        skip_consecutive = 0;
    }

    /* Advance the deadline by one frame; clamp runaway debt after a
     * pause/menu/long stall so we don't burst-skip to catch up. */
    skip_deadline += period;
    if ((int64_t)(now - skip_deadline) > (int64_t)(MAX_CONSEC_SKIP * period))
        skip_deadline = now;

    return do_render;
}
#endif

uint64_t platform_timer_ticks(void)
{
    uint32_t now = DWT->CYCCNT;
    if (now < last_cyc) {
        cyc_high += 0x100000000ULL;
    }
    last_cyc = now;
    return cyc_high + now;
}

uint64_t platform_timer_ticks_per_sec(void)
{
    return SystemCoreClock;
}

uint32_t platform_timer_get_fps_tenths(void)
{
    /* fps = 1000ms / period_ms; tenths = fps × 10 = 10000 / period_ms.
     * period_acc is scaled by (1<<FPS_IIR_SHIFT), so we keep that scaling
     * on the numerator to preserve precision. */
    if (period_acc == 0) return 600;
    return (10000u << FPS_IIR_SHIFT) / period_acc;
}
