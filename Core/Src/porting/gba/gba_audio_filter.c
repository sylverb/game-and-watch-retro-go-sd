/* See gba_audio_filter.h for why this exists. Kept free of every firmware
 * header on purpose: tests/test_gba_audio_filter.c compiles THIS file on the
 * host and proves the passband, the stopband and the bypass against tones —
 * a harness that reimplements the filter would prove nothing (docs/HARNESSES.md).
 */
#include "gba_audio_filter.h"

/* Cutoff = CUTOFF_NUM/CUTOFF_DEN of the source rate — just inside the source's
 * own Nyquist, so the music keeps its top octave while the images above it go.
 * Bypass when the images would start above what the speaker can say anyway. */
#define GBA_LPF_OUT_RATE     48000.0f
#define GBA_LPF_CUTOFF_NUM   42u
#define GBA_LPF_CUTOFF_DEN   100u
#define GBA_LPF_BYPASS_HZ    38000u   /* source rate, not cutoff */

#define GBA_LPF_PI 3.14159265358979f

/* tan(x) as the [5/4] Pade approximant — because tanf() is a call into libm,
 * and libm lives in the RESIDENT flash, which linking this file's first build
 * overflowed by 1,412 bytes. The bilinear prewarp only ever asks for
 * x = pi*fc/48000 in [0.19, 1.05] rad (cutoff 3-16 kHz), where this rational
 * is within 0.04% of tan — a cutoff error of a few hertz, against a filter
 * whose own placement (0.42x) is a taste decision. */
static float tan_approx(float x)
{
    const float x2 = x * x;
    return x * (945.0f - x2 * (105.0f - x2))
             / (945.0f - x2 * (420.0f - 15.0f * x2));
}

typedef struct {
    float b0, b1, b2, a1, a2;   /* normalized: a0 == 1 */
    float z1, z2;               /* transposed direct form II state */
} biquad;

static biquad   s_bq[2];
static uint32_t s_rate_hz;      /* what configure() last saw */
static uint32_t s_cutoff_hz;    /* 0 = bypass */

/* One Butterworth second-order section via the bilinear transform. A 4th-order
 * Butterworth is two of these with the classic pole-pair Qs. */
static biquad butter_section(float fc, float q)
{
    const float w  = tan_approx(GBA_LPF_PI * fc / GBA_LPF_OUT_RATE);
    const float ww = w * w;
    const float n  = 1.0f / (ww + w / q + 1.0f);
    biquad s;
    s.b0 = ww * n;
    s.b1 = 2.0f * s.b0;
    s.b2 = s.b0;
    s.a1 = 2.0f * (ww - 1.0f) * n;
    s.a2 = (ww - w / q + 1.0f) * n;
    s.z1 = 0.0f;
    s.z2 = 0.0f;
    return s;
}

void gba_lpf_configure(uint32_t rate_hz)
{
    if (rate_hz == s_rate_hz)
        return;

    /* rate 0 means "no FIFO is being clocked at this instant" — a gap between
     * a cry and the music, a song handoff — NOT "this cart mixes too high to
     * need the filter". Keep the last cutoff through the gap: toggling to
     * bypass and back stitches an unfiltered seam into the stream, and the
     * seam is a click at exactly the moment a note ends. (Reported as
     * "Pokemon crackles at note tails"; Pokemon is both a filtered-rate cart
     * and the one that plays a cry at every turn.) */
    if (rate_hz == 0)
        return;
    s_rate_hz = rate_hz;

    if (rate_hz >= GBA_LPF_BYPASS_HZ) {
        s_cutoff_hz = 0;
        return;
    }

    s_cutoff_hz = rate_hz * GBA_LPF_CUTOFF_NUM / GBA_LPF_CUTOFF_DEN;
    /* 4th-order Butterworth pole-pair Qs: 1/(2cos(pi/8)), 1/(2cos(3pi/8)). */
    s_bq[0] = butter_section((float)s_cutoff_hz, 0.54119610f);
    s_bq[1] = butter_section((float)s_cutoff_hz, 1.30656296f);
}

void gba_lpf_reset(void)
{
    s_bq[0].z1 = s_bq[0].z2 = 0.0f;
    s_bq[1].z1 = s_bq[1].z2 = 0.0f;
}

uint32_t gba_lpf_cutoff_hz(void)
{
    return s_cutoff_hz;
}

void gba_lpf_apply(int16_t *samples, uint32_t n)
{
    if (s_cutoff_hz == 0)
        return;

    biquad *f = &s_bq[0];
    biquad *g = &s_bq[1];
    for (uint32_t i = 0; i < n; i++) {
        float x = (float)samples[i];

        float y = f->b0 * x + f->z1;
        f->z1 = f->b1 * x - f->a1 * y + f->z2;
        f->z2 = f->b2 * x - f->a2 * y;

        x = y;
        y = g->b0 * x + g->z1;
        g->z1 = g->b1 * x - g->a1 * y + g->z2;
        g->z2 = g->b2 * x - g->a2 * y;

        /* Butterworth peaks at unity, so y stays within s16 range for s16
         * input; the clamp is for the transient after a state load. */
        if (y > 32767.0f)  y = 32767.0f;
        if (y < -32768.0f) y = -32768.0f;
        samples[i] = (int16_t)y;
    }
}
