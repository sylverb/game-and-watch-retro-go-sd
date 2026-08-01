/* PC Engine CD ADPCM (OKI MSM5205 4-bit voice/SFX). Ported from Mednafen
 * pce_fast (pcecd.cpp ADPCM_Run / ADPCM_PB_Run). Registers $1808-$180E are
 * routed here from pce_scsi; CD data is DMA'd into 64KB ADPCM RAM then
 * playback is triggered via $180D.
 *
 * Game logic (Read/Write pending, LengthCount, End/Half flags) is clocked from
 * emulated CPU cycles via pce_adpcm_sync. Audio decode runs in pce_adpcm_fill
 * at the programmed sample rate resampled to OUT_RATE — same as the pre-Mednafen
 * port and avoids FIFO underrun stutter when CPU sync runs in bursts. */
#include "pce_adpcm.h"
#include "pce.h"
#include <string.h>

#define ADPCM_RAM_SIZE  0x10000
#define OUT_RATE        44100   /* = PCE_SAMPLE_RATE */
#define ADPCM_READ_DELAY  (19 * 3)   /* $180A read pending, CPU cycles */
#define ADPCM_WRITE_DELAY (3 * 11)   /* $180A write pending */

static const int StepSizes[49] = {
    16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,
    157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,
    963,1060,1166,1282,1411,1552
};
static const int StepIdxDelta[16] = { -1,-1,-1,-1,2,4,6,8, -1,-1,-1,-1,2,4,6,8 };

static uint8_t  s_ram[ADPCM_RAM_SIZE];
static uint16_t s_addr, s_read_addr, s_write_addr, s_length;
static uint8_t  s_read_buffer;
static uint8_t  s_play_buffer;
static uint8_t  s_write_pending_val;
static uint8_t  s_last_cmd, s_freq;
static bool     s_playing, s_end, s_half, s_play_nibble;
static int32_t  s_cur;
static int      s_ssi;
static int32_t  s_read_pending, s_write_pending;
static int32_t  s_adpcm_last_sync;

static uint32_t s_phase;
static int16_t  s_held;

static int adpcm_decode(uint8_t nib)
{
    int d = StepSizes[s_ssi] * (2 * (nib & 7) + 1) / 8;
    if (nib & 8) d = -d;
    s_ssi += StepIdxDelta[nib];
    if (s_ssi < 0) s_ssi = 0; else if (s_ssi > 48) s_ssi = 48;
    s_cur = (s_cur + d) & 0xFFF;
    return s_cur;
}

/* One MSM5205 nibble: fetch on high nibble, decode, update length/end. */
static void adpcm_pb_step(void)
{
    if (s_playing && !s_play_nibble) {
        s_half = (s_length < 32768);
        if (!s_length && !(s_last_cmd & 0x10)) {
            if (s_end)
                s_half = false;
            s_end = true;
            if (s_last_cmd & 0x40)
                s_playing = false;
        }
        s_play_buffer = s_ram[s_read_addr];
        s_read_addr = (uint16_t)((s_read_addr + 1) & 0xFFFF);
        if (s_length && !(s_last_cmd & 0x10))
            s_length--;
    }

    if (s_playing) {
        uint8_t nib = (uint8_t)((s_play_buffer >> (s_play_nibble ? 0 : 4)) & 0x0F);
        s_held = (int16_t)((adpcm_decode(nib) - 0x800) << 4);
        s_play_nibble = !s_play_nibble;
    }
}

static void adpcm_pending_run(int32_t clocks)
{
    if (clocks <= 0)
        return;

    if (s_write_pending > 0) {
        s_write_pending -= clocks;
        if (s_write_pending <= 0) {
            s_half = (s_length < 32768);
            if (!(s_last_cmd & 0x10) && s_length < 0xFFFFu)
                s_length++;
            s_ram[s_write_addr++] = s_write_pending_val;
            s_write_pending = 0;
        }
    }

    if (s_read_pending > 0) {
        s_read_pending -= clocks;
        if (s_read_pending <= 0) {
            s_read_buffer = s_ram[s_read_addr];
            s_read_addr = (uint16_t)((s_read_addr + 1) & 0xFFFF);
            s_read_pending = 0;

            s_half = (s_length < 32768);
            if (!(s_last_cmd & 0x10)) {
                if (s_length)
                    s_length--;
                else {
                    s_end = true;
                    s_half = false;
                    if (s_last_cmd & 0x40)
                        s_playing = false;
                }
            }
        }
    }
}

void pce_adpcm_run(int32_t clocks)
{
    adpcm_pending_run(clocks);
}

void pce_adpcm_sync(void)
{
    int32_t now = Cycles;
    int32_t delta = now - s_adpcm_last_sync;
    if (delta < 0)
        delta = now;
    if (delta > 0) {
        adpcm_pending_run(delta);
        s_adpcm_last_sync = now;
    }
}

void pce_adpcm_frame_end(void)
{
    pce_adpcm_sync();
    s_adpcm_last_sync = 0;
}

void pce_adpcm_reset(void)
{
    s_addr = s_read_addr = s_write_addr = s_length = 0;
    s_read_buffer = s_play_buffer = s_write_pending_val = 0;
    s_last_cmd = s_freq = 0;
    s_playing = s_end = s_half = s_play_nibble = false;
    s_cur = 0x800; s_ssi = 0;
    s_read_pending = s_write_pending = 0;
    s_adpcm_last_sync = Cycles;
    s_phase = 0; s_held = 0;
}

void pce_adpcm_write(uint8_t reg, uint8_t val)
{
    switch (reg & 0x0F) {
    case 0x8:
        if (s_last_cmd & 0x80) break;
        s_addr = (uint16_t)((s_addr & 0xFF00) | val);
        if (s_last_cmd & 0x10) s_length = s_addr;
        break;
    case 0x9:
        if (s_last_cmd & 0x80) break;
        s_addr = (uint16_t)((s_addr & 0x00FF) | ((uint16_t)val << 8));
        if (s_last_cmd & 0x10) s_length = s_addr;
        break;
    case 0xA:
        s_write_pending_val = val;
        if (s_write_pending <= 0)
            s_write_pending = ADPCM_WRITE_DELAY;
        break;
    case 0xD:
        if (val & 0x80) {
            s_addr = s_read_addr = s_write_addr = s_length = 0;
            s_last_cmd = 0;
            s_playing = s_end = s_half = s_play_nibble = false;
            s_cur = 0x800; s_ssi = 0;
            s_read_pending = s_write_pending = 0;
            return;
        }
        if (s_playing && !(val & 0x20)) {
            s_playing = false;
        }
        if (!s_playing && (val & 0x20)) {
            s_playing = true;
            s_end = false;
            s_half = false;
            s_play_nibble = false;
            s_cur = 0x800; s_ssi = 0;
            s_phase = 0;
        }
        if (val & 0x10) { s_length = s_addr; s_end = false; }
        if (!(s_last_cmd & 0x08) && (val & 0x08))
            s_read_addr = (val & 0x04) ? s_addr : (uint16_t)(s_addr - 1);
        if (!(s_last_cmd & 0x02) && (val & 0x02))
            s_write_addr = (val & 0x01) ? s_addr : (uint16_t)(s_addr - 1);
        s_last_cmd = val;
        break;
    case 0xE:
        s_freq = val & 0x0F;
        break;
    default:
        break;
    }
}

uint8_t pce_adpcm_read(uint8_t reg)
{
    switch (reg & 0x0F) {
    case 0xA:
        if (s_read_pending <= 0)
            s_read_pending = ADPCM_READ_DELAY;
        return s_read_buffer;
    case 0xC: {
        uint8_t ret = (uint8_t)((s_end ? 0x01 : 0) | (s_playing ? 0x08 : 0));
        ret |= (s_write_pending > 0) ? 0x04 : 0;
        ret |= (s_read_pending > 0) ? 0x80 : 0;
        return ret;
    }
    case 0xD:
        return s_last_cmd;
    default:
        return 0;
    }
}

void pce_adpcm_dma_byte(uint8_t val)
{
    s_ram[s_write_addr++] = val;
    if (!(s_last_cmd & 0x10)) {
        s_half = (s_length < 32768);
        if (s_length < 0xFFFFu)
            s_length++;
    }
}

bool     pce_adpcm_playing(void)    { return s_playing; }
bool     pce_adpcm_end_flag(void)   { return s_end; }
bool     pce_adpcm_half_flag(void)  { return s_half; }

void pce_adpcm_reconcile_load(void)
{
    if (s_playing) {
        s_end = false;
        if (s_length == 0 && !(s_last_cmd & 0x10)) {
            s_playing = false;
            s_end = true;
            s_half = false;
        } else {
            s_half = (s_length < 32768);
        }
    } else if (!s_end) {
        s_end = true;
        s_half = false;
    } else {
        s_half = (s_length < 32768);
    }
    s_adpcm_last_sync = Cycles;
}

void pce_adpcm_get(uint32_t out[PCE_ADPCM_STATE_WORDS])
{
    out[0] = (uint32_t)s_addr | ((uint32_t)s_read_addr << 16);
    out[1] = (uint32_t)s_write_addr | ((uint32_t)s_length << 16);
    out[2] = (uint32_t)s_last_cmd | ((uint32_t)s_freq << 8) |
             ((uint32_t)(s_playing ? 1 : 0) << 16) | ((uint32_t)(s_end ? 1 : 0) << 17) |
             ((uint32_t)(s_half ? 1 : 0) << 18)    | ((uint32_t)(s_play_nibble ? 1 : 0) << 19);
    out[3] = (uint32_t)s_cur;
    out[4] = (uint32_t)s_ssi;
    out[5] = s_phase;
    out[6] = (uint32_t)(uint16_t)s_held;
    out[7] = (uint32_t)s_read_buffer | ((uint32_t)s_play_buffer << 8) |
             ((uint32_t)(uint16_t)s_write_pending_val << 16);
}

void pce_adpcm_set(const uint32_t in[PCE_ADPCM_STATE_WORDS])
{
    s_addr        = (uint16_t)(in[0] & 0xFFFF);
    s_read_addr   = (uint16_t)(in[0] >> 16);
    s_write_addr  = (uint16_t)(in[1] & 0xFFFF);
    s_length      = (uint16_t)(in[1] >> 16);
    s_last_cmd    = (uint8_t)(in[2] & 0xFF);
    s_freq        = (uint8_t)((in[2] >> 8) & 0x0F);
    s_playing     = (in[2] >> 16) & 1;
    s_end         = (in[2] >> 17) & 1;
    s_half        = (in[2] >> 18) & 1;
    s_play_nibble = (in[2] >> 19) & 1;
    s_cur         = (int32_t)(in[3] & 0xFFF);
    s_ssi         = (int)in[4]; if (s_ssi < 0 || s_ssi > 48) s_ssi = 0;
    s_phase       = in[5];
    s_held        = (int16_t)(uint16_t)in[6];
    s_read_buffer = (uint8_t)(in[7] & 0xFF);
    s_play_buffer = (uint8_t)((in[7] >> 8) & 0xFF);
    s_write_pending_val = (uint8_t)((in[7] >> 16) & 0xFF);
    s_read_pending = s_write_pending = 0;
    s_adpcm_last_sync = Cycles;
}

uint8_t *pce_adpcm_ram(void) { return s_ram; }

int pce_adpcm_fill(int16_t *out, int frames)
{
    if (!s_playing)
        return 0;

    /* fs (Hz) * 256: 32087.5*256 ≈ 8214400 */
    uint32_t fs = (uint32_t)(8214400u / (16 - s_freq));
    for (int i = 0; i < frames; i++) {
        s_phase += fs;
        while (s_phase >= (OUT_RATE << 8)) {
            adpcm_pb_step();
            s_phase -= (OUT_RATE << 8);
        }
        out[i * 2] = out[i * 2 + 1] = s_held;
        if (!s_playing) {
            for (i++; i < frames; i++)
                out[i * 2] = out[i * 2 + 1] = 0;
            return frames;
        }
    }
    return frames;
}
