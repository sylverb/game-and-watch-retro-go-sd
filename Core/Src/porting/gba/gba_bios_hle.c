/* Host implementations of the common GBA BIOS SWIs.
 *
 * Algorithms follow the open Normmatt/VBA-M BIOS (external/gpsp/bios/), adapted
 * to gpSP's memory accessors so I/O and waitstate side effects still fire.
 * Cycle costs are approximate but proportional to work — large enough that a
 * game waiting on a CpuFastSet still sees timers and HBlank progress, small
 * enough that we do not invent multi-frame stalls. Long transfers park the CPU
 * in CPU_DMA sleep for any cost that does not fit the current slice (same
 * mechanism as a real DMA), so update_gba keeps advancing the machine.
 */
#include "gba_bios_hle.h"

#include <stdint.h>
#include <string.h>

/* gpSP internals — declared by hand; see m4a_gpsp.c for the same pattern. */
extern unsigned char *memory_map_read[8 * 1024];
extern unsigned char  ws_cyc_nseq[16][2];
extern unsigned char  ws_cyc_seq[16][2];
extern unsigned int   gamepak_size;
extern unsigned int   backup_type;
extern unsigned char *load_gamepak_page(unsigned int physical_index);
extern unsigned int   read_memory8(unsigned int address);
extern unsigned int   read_memory16(unsigned int address);
extern unsigned int   read_memory32(unsigned int address);
extern unsigned int   write_memory8(unsigned int address, unsigned char value);
extern unsigned int   write_memory16(unsigned int address, unsigned short value);
extern unsigned int   write_memory32(unsigned int address, unsigned int value);

#define R8(a)   ((uint8_t)read_memory8(a))
#define R16(a)  ((uint16_t)read_memory16(a))
#define R32(a)  ((uint32_t)read_memory32(a))
#define W8(a,v)  write_memory8((a), (uint8_t)(v))
#define W16(a,v) write_memory16((a), (uint16_t)(v))
#define W32(a,v) write_memory32((a), (uint32_t)(v))

/* Source must not live in the BIOS mirror (same guard as the open BIOS). */
static int src_ok(uint32_t source, uint32_t bytes)
{
    if ((source & 0xe000000u) == 0)
        return 0;
    if (((source + bytes) & 0xe000000u) == 0)
        return 0;
    return 1;
}

/* The guard every length-carrying SWI applies, and the mask is the point: the
 * BIOS checks the source against the byte count masked to 21 bits, not against
 * the 24-bit length its header carries. (No-op for BitUnpack, whose length is a
 * halfword, but it is the same check and it reads as one.) */
static int src_ok_len(uint32_t source, uint32_t bytes)
{
    return src_ok(source, bytes & 0x1FFFFFu);
}

static int iabs32(int32_t x) { return x < 0 ? -x : x; }

static int cost_rw32(uint32_t addr, int seq)
{
    unsigned r = (addr >> 24) & 15u;
    return seq ? ws_cyc_seq[r][1] : ws_cyc_nseq[r][1];
}

static int cost_rw16(uint32_t addr, int seq)
{
    unsigned r = (addr >> 24) & 15u;
    return seq ? ws_cyc_seq[r][0] : ws_cyc_nseq[r][0];
}

/* gba_memory.h's BACKUP_EEPROM, spelled out for the same reason as everything
 * else here: including that header pulls in the CMSIS collision. */
#define GBA_BACKUP_EEPROM 2

/* Host pointer for a span that a plain load/store can serve, or NULL.
 *
 * EWRAM, IWRAM and VRAM qualify. There is no dynarec in this build, so a RAM
 * write has no SMC tag to update, and write_vram16/32 are plain stores whose
 * 0x18000 mirror fold is already baked into memory_map_read. Palette RAM keeps
 * a second, colour-converted copy and OAM sets reg[OAM_UPDATED], so those two
 * have to keep going through write_memory*.
 *
 * ROM qualifies for reads, through the same page the core itself would have
 * used — including the page-0 shadow an RTC cart writes its GPIO registers
 * into. Two ROM spans do not: one that runs past the end of the cart reads open
 * bus, which is not memory at all, and 0x0D is the EEPROM's serial port on a
 * cart that has one, where a read clocks the protocol.
 *
 * `nbytes` must fit the 32 KB gpSP page `addr` lands in; callers clamp to it.
 */
static uint8_t *map_direct(uint32_t addr, uint32_t nbytes, int for_write)
{
    unsigned r = addr >> 24;
    unsigned char *page;

    if (r >= 8u && r <= 0xDu) {
        if (for_write)
            return 0;
        if (r == 0xDu && backup_type == GBA_BACKUP_EEPROM)
            return 0;
        if ((addr & 0x1FFFFFFu) + nbytes > gamepak_size)
            return 0;
        page = memory_map_read[addr >> 15];
        if (!page)
            page = load_gamepak_page((addr >> 15) & 0x3FFu);
    } else if (r == 2u || r == 3u || r == 6u) {
        page = memory_map_read[addr >> 15];
    } else {
        return 0;
    }

    if (!page)
        return 0;
    if ((addr & 0x7FFFu) + nbytes > 0x8000u)
        return 0;
    return page + (addr & 0x7FFFu);
}

/* Bulk 32-bit copy/fill straight into host memory. Returns 0 if any part of the
 * transfer still needs write_memory*, and the caller then runs its word-at-a-time
 * loop over the whole thing from the start.
 *
 * Bailing after some chunks are already done is wasted work, never a wrong
 * result: both this and the loop it falls back to move strictly forward and
 * write the same bytes, so a word the loop re-reads out of an already-copied
 * range holds exactly what the BIOS would have found there at that point. */
static int copy32_direct(uint32_t src, uint32_t dst, int words, int fill, uint32_t fillv)
{
    while (words > 0) {
        uint32_t span = 0x8000u - (dst & 0x7FFFu);
        int nwords;
        uint8_t *d;

        /* A fill never reads past the one word it was given, so only a copy
         * has to stop at the source's page edge too. */
        if (!fill) {
            uint32_t span_s = 0x8000u - (src & 0x7FFFu);
            if (span_s < span)
                span = span_s;
        }
        nwords = (int)(span >> 2);
        if (nwords > words)
            nwords = words;
        if (nwords <= 0)
            return 0;

        d = map_direct(dst, (uint32_t)nwords * 4u, 1);
        if (!d)
            return 0;

        if (fill) {
            uint8_t b = (uint8_t)fillv;
            if (fillv == (uint32_t)b * 0x01010101u) {
                memset(d, b, (size_t)nwords * 4u);
            } else {
                uint32_t *p = (uint32_t *)(void *)d;
                int i;
                for (i = 0; i < nwords; i++)
                    p[i] = fillv;
            }
        } else {
            uint8_t *s = map_direct(src, (uint32_t)nwords * 4u, 0);
            size_t n = (size_t)nwords * 4u;
            if (!s)
                return 0;
            /* The BIOS copies forward, one word at a time. If the two ends are
             * the same host bytes — an IWRAM mirror copied onto itself — that
             * writes every word with its own value and changes nothing. If they
             * merely overlap, the forward copy propagates and memcpy does not:
             * that case belongs to the caller's loop. */
            if (s != d) {
                if (d < s + n && s < d + n)
                    return 0;
                memcpy(d, s, n);
            }
            src += (uint32_t)nwords * 4u;
        }
        dst += (uint32_t)nwords * 4u;
        words -= nwords;
    }
    return 1;
}

/* A one-page window onto guest memory, for the decompressors.
 *
 * They touch memory a byte at a time, and every one of those bytes was a call
 * into read_memory8/write_memory8 and a switch on the region. This holds the
 * host pointer for the 32 KB page of the last access instead, so the common case
 * is a compare and a load. It is a POINTER, never a copy: a read still observes
 * whatever is in memory right now, which is what keeps a destination that
 * overlaps its own source behaving the way the BIOS does.
 *
 * The window is page-ALIGNED, not access-aligned, because Huffman alternates
 * between its tree and its bitstream — two addresses in the same page that would
 * otherwise evict each other on every symbol.
 *
 * Anything the pointer cannot serve (palette, OAM, I/O, past the cart) falls
 * through to the accessor that can. */
typedef struct {
    uint32_t base;      /* guest address of the page held, or 0 with len == 0 */
    uint32_t len;       /* bytes valid from base */
    uint8_t *host;
    int      w8_ok;     /* an 8-bit store may go straight in (plain RAM only —
                         * VRAM turns one into a duplicated halfword) */
} gmap_t;

static void gmap_init(gmap_t *m)
{
    m->base = 0; m->len = 0; m->host = 0; m->w8_ok = 0;
}

static uint8_t *gmap_span(gmap_t *m, uint32_t addr, uint32_t n, int for_write)
{
    uint32_t off = addr - m->base;
    uint32_t page, want;
    unsigned r;
    uint8_t *p;

    if (off < m->len && (m->len - off) >= n)
        return m->host + off;

    m->len = 0;
    page = addr & ~0x7FFFu;
    want = 0x8000u;
    r = addr >> 24;
    if (r >= 8u && r <= 0xDu) {
        uint32_t o = page & 0x1FFFFFFu;
        uint32_t left = o < gamepak_size ? gamepak_size - o : 0;
        if (left < want)
            want = left;
    }
    off = addr - page;
    if (want < off + n)
        return 0;
    p = map_direct(page, want, for_write);
    if (!p)
        return 0;
    m->base = page;
    m->len = want;
    m->host = p;
    m->w8_ok = (r == 2u || r == 3u);
    return p + off;
}

/* The 16- and 32-bit forms decline an unaligned address rather than reproduce
 * what gpSP does with one, and memcpy rather than a cast keeps them honest about
 * the alignment of the host pointer. */
static uint32_t gm_r8(gmap_t *m, uint32_t a)
{
    uint8_t *p = gmap_span(m, a, 1, 0);
    return p ? *p : R8(a);
}

static uint32_t gm_r16(gmap_t *m, uint32_t a)
{
    uint8_t *p = (a & 1u) ? 0 : gmap_span(m, a, 2, 0);
    uint16_t v;
    if (!p)
        return R16(a);
    memcpy(&v, p, 2);
    return v;
}

static uint32_t gm_r32(gmap_t *m, uint32_t a)
{
    uint8_t *p = (a & 3u) ? 0 : gmap_span(m, a, 4, 0);
    uint32_t v;
    if (!p)
        return R32(a);
    memcpy(&v, p, 4);
    return v;
}

static void gm_w8(gmap_t *m, uint32_t a, uint8_t v)
{
    uint8_t *p = gmap_span(m, a, 1, 1);
    if (p && m->w8_ok)
        *p = v;
    else
        W8(a, v);
}

static void gm_w16(gmap_t *m, uint32_t a, uint16_t v)
{
    uint8_t *p = (a & 1u) ? 0 : gmap_span(m, a, 2, 1);
    if (p)
        memcpy(p, &v, 2);
    else
        W16(a, v);
}

static void gm_w32(gmap_t *m, uint32_t a, uint32_t v)
{
    uint8_t *p = (a & 3u) ? 0 : gmap_span(m, a, 4, 1);
    if (p)
        memcpy(p, &v, 4);
    else
        W32(a, v);
}

/* ---------------------------------------------------------------- math ---- */

static int hle_div(unsigned *regs, int arm_order, int *cycles)
{
    int32_t num, den;
    if (arm_order) {
        den = (int32_t)regs[0];
        num = (int32_t)regs[1];
    } else {
        num = (int32_t)regs[0];
        den = (int32_t)regs[1];
    }
    if (den == 0)
        return 0; /* decline — real BIOS hangs */
    /* INT32_MIN / -1 has no 32-bit answer: on the host it is undefined, and
     * whatever it produced would not be what the BIOS's software divide does.
     * Decline and let the BIOS answer for itself. */
    if (den == -1 && num == INT32_MIN)
        return 0;

    int32_t quot = num / den;
    int32_t rem  = num % den;
    regs[0] = (unsigned)quot;
    regs[1] = (unsigned)rem;
    regs[3] = (unsigned)(quot < 0 ? -quot : quot);
    *cycles = arm_order ? 103 : 100;
    return 1;
}

static void hle_sqrt(unsigned *regs, int *cycles)
{
    uint32_t n = regs[0], root = 0, try_;
#define ITER(N) do { \
        try_ = root + (1u << (N)); \
        if (n >= try_ << (N)) { n -= try_ << (N); root |= 2u << (N); } \
    } while (0)
    ITER(15); ITER(14); ITER(13); ITER(12);
    ITER(11); ITER(10); ITER(9);  ITER(8);
    ITER(7);  ITER(6);  ITER(5);  ITER(4);
    ITER(3);  ITER(2);  ITER(1);  ITER(0);
#undef ITER
    regs[0] = root >> 1;
    *cycles = 120;
}

static const int16_t sine_table[256] = {
    0x0000,0x0192,0x0323,0x04B5,0x0645,0x07D5,0x0964,0x0AF1,
    0x0C7C,0x0E05,0x0F8C,0x1111,0x1294,0x1413,0x158F,0x1708,
    0x187D,0x19EF,0x1B5D,0x1CC6,0x1E2B,0x1F8B,0x20E7,0x223D,
    0x238E,0x24DA,0x261F,0x275F,0x2899,0x29CD,0x2AFA,0x2C21,
    0x2D41,0x2E5A,0x2F6B,0x3076,0x3179,0x3274,0x3367,0x3453,
    0x3536,0x3612,0x36E5,0x37AF,0x3871,0x392A,0x39DA,0x3A82,
    0x3B20,0x3BB6,0x3C42,0x3CC5,0x3D3E,0x3DAE,0x3E14,0x3E71,
    0x3EC5,0x3F0E,0x3F4E,0x3F84,0x3FB1,0x3FD3,0x3FEC,0x3FFB,
    0x4000,0x3FFB,0x3FEC,0x3FD3,0x3FB1,0x3F84,0x3F4E,0x3F0E,
    0x3EC5,0x3E71,0x3E14,0x3DAE,0x3D3E,0x3CC5,0x3C42,0x3BB6,
    0x3B20,0x3A82,0x39DA,0x392A,0x3871,0x37AF,0x36E5,0x3612,
    0x3536,0x3453,0x3367,0x3274,0x3179,0x3076,0x2F6B,0x2E5A,
    0x2D41,0x2C21,0x2AFA,0x29CD,0x2899,0x275F,0x261F,0x24DA,
    0x238E,0x223D,0x20E7,0x1F8B,0x1E2B,0x1CC6,0x1B5D,0x19EF,
    0x187D,0x1708,0x158F,0x1413,0x1294,0x1111,0x0F8C,0x0E05,
    0x0C7C,0x0AF1,0x0964,0x07D5,0x0645,0x04B5,0x0323,0x0192,
    0x0000,(int16_t)0xFE6E,(int16_t)0xFCDD,(int16_t)0xFB4B,
    (int16_t)0xF9BB,(int16_t)0xF82B,(int16_t)0xF69C,(int16_t)0xF50F,
    (int16_t)0xF384,(int16_t)0xF1FB,(int16_t)0xF074,(int16_t)0xEEEF,
    (int16_t)0xED6C,(int16_t)0xEBED,(int16_t)0xEA71,(int16_t)0xE8F8,
    (int16_t)0xE783,(int16_t)0xE611,(int16_t)0xE4A3,(int16_t)0xE33A,
    (int16_t)0xE1D5,(int16_t)0xE075,(int16_t)0xDF19,(int16_t)0xDDC3,
    (int16_t)0xDC72,(int16_t)0xDB26,(int16_t)0xD9E1,(int16_t)0xD8A1,
    (int16_t)0xD767,(int16_t)0xD633,(int16_t)0xD506,(int16_t)0xD3DF,
    (int16_t)0xD2BF,(int16_t)0xD1A6,(int16_t)0xD095,(int16_t)0xCF8A,
    (int16_t)0xCE87,(int16_t)0xCD8C,(int16_t)0xCC99,(int16_t)0xCBAD,
    (int16_t)0xCACA,(int16_t)0xC9EE,(int16_t)0xC91B,(int16_t)0xC851,
    (int16_t)0xC78F,(int16_t)0xC6D6,(int16_t)0xC626,(int16_t)0xC57E,
    (int16_t)0xC4E0,(int16_t)0xC44A,(int16_t)0xC3BE,(int16_t)0xC33B,
    (int16_t)0xC2C2,(int16_t)0xC252,(int16_t)0xC1EC,(int16_t)0xC18F,
    (int16_t)0xC13B,(int16_t)0xC0F2,(int16_t)0xC0B2,(int16_t)0xC07C,
    (int16_t)0xC04F,(int16_t)0xC02D,(int16_t)0xC014,(int16_t)0xC005,
    (int16_t)0xC000,(int16_t)0xC005,(int16_t)0xC014,(int16_t)0xC02D,
    (int16_t)0xC04F,(int16_t)0xC07C,(int16_t)0xC0B2,(int16_t)0xC0F2,
    (int16_t)0xC13B,(int16_t)0xC18F,(int16_t)0xC1EC,(int16_t)0xC252,
    (int16_t)0xC2C2,(int16_t)0xC33B,(int16_t)0xC3BE,(int16_t)0xC44A,
    (int16_t)0xC4E0,(int16_t)0xC57E,(int16_t)0xC626,(int16_t)0xC6D6,
    (int16_t)0xC78F,(int16_t)0xC851,(int16_t)0xC91B,(int16_t)0xC9EE,
    (int16_t)0xCACA,(int16_t)0xCBAD,(int16_t)0xCC99,(int16_t)0xCD8C,
    (int16_t)0xCE87,(int16_t)0xCF8A,(int16_t)0xD095,(int16_t)0xD1A6,
    (int16_t)0xD2BF,(int16_t)0xD3DF,(int16_t)0xD506,(int16_t)0xD633,
    (int16_t)0xD767,(int16_t)0xD8A1,(int16_t)0xD9E1,(int16_t)0xDB26,
    (int16_t)0xDC72,(int16_t)0xDDC3,(int16_t)0xDF19,(int16_t)0xE075,
    (int16_t)0xE1D5,(int16_t)0xE33A,(int16_t)0xE4A3,(int16_t)0xE611,
    (int16_t)0xE783,(int16_t)0xE8F8,(int16_t)0xEA71,(int16_t)0xEBED,
    (int16_t)0xED6C,(int16_t)0xEEEF,(int16_t)0xF074,(int16_t)0xF1FB,
    (int16_t)0xF384,(int16_t)0xF50F,(int16_t)0xF69C,(int16_t)0xF82B,
    (int16_t)0xF9BB,(int16_t)0xFB4B,(int16_t)0xFCDD,(int16_t)0xFE6E
};

static uint32_t arctan_body(uint32_t input)
{
    int32_t a = -(((int32_t)(input * input)) >> 14);
    int32_t b = ((0xA9 * a) >> 14) + 0x390;
    b = ((b * a) >> 14) + 0x91C;
    b = ((b * a) >> 14) + 0xFB6;
    b = ((b * a) >> 14) + 0x16AA;
    b = ((b * a) >> 14) + 0x2081;
    b = ((b * a) >> 14) + 0x3651;
    b = ((b * a) >> 14) + 0xA2F9;
    return (uint32_t)(((int32_t)input * b) >> 16);
}

static void hle_arctan(unsigned *regs, int *cycles)
{
    regs[0] = arctan_body(regs[0]);
    *cycles = 80;
}

static void hle_arctan2(unsigned *regs, int *cycles)
{
    int32_t x = (int32_t)regs[0];
    int32_t y = (int32_t)regs[1];
    uint32_t res = 0;

    if (y == 0) {
        res = ((uint32_t)x >> 16) & 0x8000u;
    } else if (x == 0) {
        res = (((uint32_t)y >> 16) & 0x8000u) + 0x4000u;
    } else {
        int ax = iabs32(x), ay = iabs32(y);
        if (ax > ay || (ax == ay && !((x < 0) && (y < 0)))) {
            /* Div(y<<14, x) then ArcTan — reuse host division */
            int32_t div = (y << 14) / x;
            uint32_t at = arctan_body((uint32_t)div);
            if (x < 0)
                res = 0x8000u + at;
            else
                res = ((((uint32_t)y >> 16) & 0x8000u) << 1) + at;
        } else {
            int32_t div = (x << 14) / y;
            uint32_t at = arctan_body((uint32_t)div);
            res = (0x4000u + (((uint32_t)y >> 16) & 0x8000u)) - at;
        }
    }
    regs[0] = res;
    *cycles = 160;
}

/* ---------------------------------------------------------- memory copy ---- */

static int hle_cpu_set(unsigned *regs, int *cycles)
{
    uint32_t source = regs[0], dest = regs[1], cnt = regs[2];
    int count = (int)(cnt & 0x1FFFFFu);
    int fill = (cnt >> 24) & 1;
    int word32 = (cnt >> 26) & 1;
    int cost = 24;

    if (!src_ok(source, (uint32_t)(((cnt << 11) >> 9) & 0x1fffffu)))
        return 0;

    if (word32) {
        source &= ~3u;
        dest &= ~3u;
        /* Fast path: block move where both ends are plain memory (see
         * map_direct), word loop where they are not. Same cycle bill either
         * way — the guest is charged for the transfer, not for how we ran it. */
        if (fill) {
            uint32_t value = source > 0x0EFFFFFFu ? 0x1CAD1CADu : R32(source);
            cost += cost_rw32(source, 0) + count * (1 + cost_rw32(dest, 1));
            if (copy32_direct(source, dest, count, 1, value)) {
                *cycles = cost;
                return 1;
            }
            while (count--) {
                W32(dest, value);
                dest += 4;
            }
        } else {
            cost += count * (cost_rw32(source, 1) + cost_rw32(dest, 1));
            if (copy32_direct(source, dest, count, 0, 0)) {
                *cycles = cost;
                return 1;
            }
            while (count--) {
                uint32_t v = source > 0x0EFFFFFFu ? 0x1CAD1CADu : R32(source);
                W32(dest, v);
                source += 4;
                dest += 4;
            }
        }
    } else {
        /* The per-element cost is hoisted out of these loops: the region bits it
         * reads cannot change under a transfer this size, and the bill is
         * approximate by design (see the note at the top of this file). */
        if (fill) {
            uint16_t value = source > 0x0EFFFFFFu ? 0x1CADu : R16(source);
            cost += cost_rw16(source, 0) + count * (1 + cost_rw16(dest, 1));
            while (count--) {
                W16(dest, value);
                dest += 2;
            }
        } else {
            cost += count * (cost_rw16(source, 1) + cost_rw16(dest, 1));
            while (count--) {
                uint16_t v = source > 0x0EFFFFFFu ? 0x1CADu : R16(source);
                W16(dest, v);
                source += 2;
                dest += 2;
            }
        }
    }
    *cycles = cost;
    return 1;
}

static int hle_cpu_fast_set(unsigned *regs, int *cycles)
{
    uint32_t source = regs[0] & ~3u;
    uint32_t dest = regs[1] & ~3u;
    uint32_t cnt = regs[2];
    int count = (int)(cnt & 0x1FFFFFu);
    int fill = (cnt >> 24) & 1;
    int cost = 24;
    int i;

    if (!src_ok(regs[0], (uint32_t)(((cnt << 11) >> 9) & 0x1fffffu)))
        return 0;

    /* CpuFastSet moves 32 bytes at a time and nothing smaller: a count that is
     * not a multiple of 8 words is rounded UP, and the last block is written in
     * full. The word loop below already does that by construction (it steps 8
     * and tests > 0), so rounding here is what keeps the block copy below
     * transferring the same amount as the loop it stands in for. */
    count = (count + 7) & ~7;

    if (fill) {
        uint32_t value = source > 0x0EFFFFFFu ? 0xBAFFFFFBu : R32(source);
        cost += cost_rw32(source, 0) + count * (1 + cost_rw32(dest, 1));
        if (copy32_direct(source, dest, count, 1, value)) {
            *cycles = cost;
            return 1;
        }
        while (count > 0) {
            for (i = 0; i < 8; i++) {
                W32(dest, value);
                dest += 4;
            }
            count -= 8;
        }
    } else {
        cost += count * (cost_rw32(source, 1) + cost_rw32(dest, 1));
        if (copy32_direct(source, dest, count, 0, 0)) {
            *cycles = cost;
            return 1;
        }
        while (count > 0) {
            for (i = 0; i < 8; i++) {
                uint32_t v = source > 0x0EFFFFFFu ? 0xBAFFFFFBu : R32(source);
                W32(dest, v);
                source += 4;
                dest += 4;
            }
            count -= 8;
        }
    }
    *cycles = cost;
    return 1;
}

/* --------------------------------------------------------------- affine ---- */

static void hle_bg_affine_set(unsigned *regs, int *cycles)
{
    uint32_t src = regs[0], dest = regs[1];
    uint32_t num = regs[2];
    int cost = 24;

    while (num--) {
        int32_t cx = (int32_t)R32(src); src += 4;
        int32_t cy = (int32_t)R32(src); src += 4;
        int16_t dispx = (int16_t)R16(src); src += 2;
        int16_t dispy = (int16_t)R16(src); src += 2;
        int16_t rx = (int16_t)R16(src); src += 2;
        int16_t ry = (int16_t)R16(src); src += 2;
        uint16_t theta = R16(src) >> 8; src += 4;
        int32_t a = sine_table[(theta + 0x40) & 255];
        int32_t b = sine_table[theta];
        int16_t dx  = (int16_t)((rx * a) >> 14);
        int16_t dmx = (int16_t)((rx * b) >> 14);
        int16_t dy  = (int16_t)((ry * b) >> 14);
        int16_t dmy = (int16_t)((ry * a) >> 14);
        W16(dest, (uint16_t)dx);  dest += 2;
        W16(dest, (uint16_t)-dmx); dest += 2;
        W16(dest, (uint16_t)dy);  dest += 2;
        W16(dest, (uint16_t)dmy); dest += 2;
        W32(dest, (uint32_t)(cx - dx * dispx + dmx * dispy)); dest += 4;
        W32(dest, (uint32_t)(cy - dy * dispx - dmy * dispy)); dest += 4;
        cost += 100;
    }
    *cycles = cost;
}

static void hle_obj_affine_set(unsigned *regs, int *cycles)
{
    uint32_t src = regs[0], dest = regs[1];
    int num = (int)regs[2];
    int offset = (int)regs[3];
    int cost = 24;

    while (num--) {
        int16_t rx = (int16_t)R16(src); src += 2;
        int16_t ry = (int16_t)R16(src); src += 2;
        uint16_t theta = R16(src) >> 8; src += 4;
        int32_t a = sine_table[(theta + 0x40) & 255];
        int32_t b = sine_table[theta];
        int16_t dx  = (int16_t)((rx * a) >> 14);
        int16_t dmx = (int16_t)((rx * b) >> 14);
        int16_t dy  = (int16_t)((ry * b) >> 14);
        int16_t dmy = (int16_t)((ry * a) >> 14);
        W16(dest, (uint16_t)dx);   dest += offset;
        W16(dest, (uint16_t)-dmx); dest += offset;
        W16(dest, (uint16_t)dy);   dest += offset;
        W16(dest, (uint16_t)dmy);  dest += offset;
        cost += 60;
    }
    *cycles = cost;
}

/* ---------------------------------------------------------- decompress ---- */

static int hle_lz77_wram(unsigned *regs, int *cycles)
{
    uint32_t source = regs[0], dest = regs[1];
    uint32_t header = R32(source);
    int len = (int)(header >> 8);
    int cost = 32;
    gmap_t ms, md, mw;

    source += 4;
    if (!src_ok_len(source, (uint32_t)len))
        return 0;

    gmap_init(&ms); gmap_init(&md); gmap_init(&mw);

    while (len > 0) {
        uint8_t d = (uint8_t)gm_r8(&ms, source++);
        cost += 2;
        for (int i = 0; i < 8; i++) {
            if (d & 0x80) {
                uint16_t data = (uint16_t)(gm_r8(&ms, source) << 8);
                source++;
                data |= gm_r8(&ms, source);
                source++;
                int length = (data >> 12) + 3;
                int offset = data & 0x0FFF;
                uint32_t window = dest - offset - 1;
                for (int j = 0; j < length; j++) {
                    uint8_t v = (uint8_t)gm_r8(&mw, window++);
                    gm_w8(&md, dest++, v);
                    cost += 3;
                    if (--len == 0) { *cycles = cost; return 1; }
                }
            } else {
                uint8_t v = (uint8_t)gm_r8(&ms, source++);
                gm_w8(&md, dest++, v);
                cost += 2;
                if (--len == 0) { *cycles = cost; return 1; }
            }
            d <<= 1;
        }
    }
    *cycles = cost;
    return 1;
}

static int hle_lz77_vram(unsigned *regs, int *cycles)
{
    uint32_t source = regs[0], dest = regs[1];
    uint32_t header = R32(source);
    int len = (int)(header >> 8);
    int byteCount = 0, byteShift = 0;
    uint32_t writeValue = 0;
    int cost = 32;
    gmap_t ms, md, mw;

    source += 4;
    if (!src_ok_len(source, (uint32_t)len))
        return 0;

    gmap_init(&ms); gmap_init(&md); gmap_init(&mw);

    while (len > 0) {
        uint8_t d = (uint8_t)gm_r8(&ms, source++);
        cost += 2;
        for (int i = 0; i < 8; i++) {
            if (d & 0x80) {
                uint16_t data = (uint16_t)(gm_r8(&ms, source) << 8);
                source++;
                data |= gm_r8(&ms, source);
                source++;
                int length = (data >> 12) + 3;
                int offset = data & 0x0FFF;
                uint32_t window = dest + byteCount - offset - 1;
                for (int j = 0; j < length; j++) {
                    /* The window reads MEMORY, so the byte still sitting in
                     * writeValue is not visible to it. That is the BIOS's
                     * behaviour and games depend on it. */
                    writeValue |= (gm_r8(&mw, window++) << byteShift);
                    byteShift += 8;
                    byteCount++;
                    if (byteCount == 2) {
                        gm_w16(&md, dest, (uint16_t)writeValue);
                        dest += 2;
                        byteCount = byteShift = 0;
                        writeValue = 0;
                        cost += 2;
                    }
                    if (--len == 0) { *cycles = cost; return 1; }
                }
            } else {
                writeValue |= (gm_r8(&ms, source++) << byteShift);
                byteShift += 8;
                byteCount++;
                if (byteCount == 2) {
                    gm_w16(&md, dest, (uint16_t)writeValue);
                    dest += 2;
                    byteCount = byteShift = 0;
                    writeValue = 0;
                    cost += 2;
                }
                if (--len == 0) { *cycles = cost; return 1; }
            }
            d <<= 1;
        }
    }
    *cycles = cost;
    return 1;
}

static int hle_rl_wram(unsigned *regs, int *cycles)
{
    uint32_t source = regs[0], dest = regs[1];
    uint32_t header = R32(source);
    int len = (int)(header >> 8);
    int cost = 24;
    gmap_t ms, md;

    source += 4;
    if (!src_ok_len(source, (uint32_t)len))
        return 0;

    gmap_init(&ms); gmap_init(&md);

    while (len > 0) {
        uint8_t d = (uint8_t)gm_r8(&ms, source++);
        int l = d & 0x7F;
        if (d & 0x80) {
            uint8_t data = (uint8_t)gm_r8(&ms, source++);
            l += 3;
            for (int i = 0; i < l; i++) {
                gm_w8(&md, dest++, data);
                cost += 2;
                if (--len == 0) { *cycles = cost; return 1; }
            }
        } else {
            l++;
            for (int i = 0; i < l; i++) {
                uint8_t v = (uint8_t)gm_r8(&ms, source++);
                gm_w8(&md, dest++, v);
                cost += 2;
                if (--len == 0) { *cycles = cost; return 1; }
            }
        }
    }
    *cycles = cost;
    return 1;
}

static int hle_rl_vram(unsigned *regs, int *cycles)
{
    uint32_t source = regs[0], dest = regs[1];
    /* Alone among these, RLUnCompVram aligns the address it reads the HEADER
     * from and then steps four bytes on from the address it was GIVEN. With an
     * unaligned source the two disagree, and the stream starts elsewhere. */
    uint32_t header = R32(source & ~3u);
    int len = (int)(header >> 8);
    int byteCount = 0, byteShift = 0;
    uint32_t writeValue = 0;
    int cost = 24;
    gmap_t ms, md;

    source += 4;
    if (!src_ok_len(source, (uint32_t)len))
        return 0;

    gmap_init(&ms); gmap_init(&md);

    while (len > 0) {
        uint8_t d = (uint8_t)gm_r8(&ms, source++);
        int l = d & 0x7F;
        if (d & 0x80) {
            uint8_t data = (uint8_t)gm_r8(&ms, source++);
            l += 3;
            for (int i = 0; i < l; i++) {
                writeValue |= (data << byteShift);
                byteShift += 8;
                byteCount++;
                if (byteCount == 2) {
                    gm_w16(&md, dest, (uint16_t)writeValue);
                    dest += 2;
                    byteCount = byteShift = 0;
                    writeValue = 0;
                    cost += 2;
                }
                if (--len == 0) { *cycles = cost; return 1; }
            }
        } else {
            l++;
            for (int i = 0; i < l; i++) {
                writeValue |= (gm_r8(&ms, source++) << byteShift);
                byteShift += 8;
                byteCount++;
                if (byteCount == 2) {
                    gm_w16(&md, dest, (uint16_t)writeValue);
                    dest += 2;
                    byteCount = byteShift = 0;
                    writeValue = 0;
                    cost += 2;
                }
                if (--len == 0) { *cycles = cost; return 1; }
            }
        }
    }
    *cycles = cost;
    return 1;
}

static int hle_huff(unsigned *regs, int *cycles)
{
    uint32_t source = regs[0], dest = regs[1];
    uint32_t header = R32(source);
    int len = (int)(header >> 8);
    int cost = 40;
    uint8_t treeSize;
    uint32_t treeStart;
    uint32_t mask, data;
    int pos;
    uint8_t rootNode, currentNode;
    int writeData;
    int byteShift, byteCount;
    uint32_t writeValue;
    /* A tree whose nodes never flag a leaf makes the decoder walk without ever
     * consuming `len`. The BIOS spins forever on that, which a game can survive
     * — the frame loop keeps running around it — but spinning HERE takes the
     * firmware down with it, so give up instead. A legal tree is at most 32 deep
     * and every symbol costs at least one step, so this cannot fire on data the
     * BIOS would have decoded. */
    uint32_t steps = 0, step_cap;
    gmap_t ms, md;

    source += 4;
    if (!src_ok_len(source, (uint32_t)len))
        return 0;

    gmap_init(&ms); gmap_init(&md);

    step_cap = (uint32_t)(len & 0xFFFFFF) * 64u + 4096u;
    treeSize = (uint8_t)gm_r8(&ms, source++);
    treeStart = source;
    source += ((treeSize + 1u) << 1) - 1u;

    mask = 0x80000000u;
    data = gm_r32(&ms, source);
    source += 4;
    pos = 0;
    rootNode = (uint8_t)gm_r8(&ms, treeStart);
    currentNode = rootNode;
    writeData = 0;
    byteShift = byteCount = 0;
    writeValue = 0;

    if ((header & 0x0F) == 8) {
        while (len > 0) {
            if (++steps > step_cap) break;
            if (pos == 0) pos++;
            else pos += (((currentNode & 0x3F) + 1) << 1);

            if (data & mask) {
                if (currentNode & 0x40) writeData = 1;
                currentNode = (uint8_t)gm_r8(&ms, treeStart + pos + 1);
            } else {
                if (currentNode & 0x80) writeData = 1;
                currentNode = (uint8_t)gm_r8(&ms, treeStart + pos);
            }
            if (writeData) {
                writeValue |= ((uint32_t)currentNode << byteShift);
                byteCount++;
                byteShift += 8;
                pos = 0;
                currentNode = rootNode;
                writeData = 0;
                if (byteCount == 4) {
                    gm_w32(&md, dest, writeValue);
                    dest += 4;
                    len -= 4;
                    writeValue = 0;
                    byteCount = byteShift = 0;
                    cost += 4;
                }
            }
            mask >>= 1;
            if (mask == 0) {
                mask = 0x80000000u;
                data = gm_r32(&ms, source);
                source += 4;
                cost += 2;
            }
        }
    } else {
        /* 4-bit data: a leaf is a NIBBLE, not a byte. Two of them make a byte,
         * four bytes make the word that goes out — the output is still written
         * 32 bits at a time and `len` still counts down in fours. */
        int halfLen = 0, value = 0;
        while (len > 0) {
            if (++steps > step_cap) break;
            if (pos == 0) pos++;
            else pos += (((currentNode & 0x3F) + 1) << 1);

            if (data & mask) {
                if (currentNode & 0x40) writeData = 1;
                currentNode = (uint8_t)gm_r8(&ms, treeStart + pos + 1);
            } else {
                if (currentNode & 0x80) writeData = 1;
                currentNode = (uint8_t)gm_r8(&ms, treeStart + pos);
            }
            if (writeData) {
                if (halfLen == 0)
                    value |= currentNode;
                else
                    value |= (currentNode << 4);
                halfLen += 4;
                if (halfLen == 8) {
                    writeValue |= ((uint32_t)value << byteShift);
                    byteCount++;
                    byteShift += 8;
                    halfLen = 0;
                    value = 0;
                    if (byteCount == 4) {
                        byteCount = byteShift = 0;
                        gm_w32(&md, dest, writeValue);
                        dest += 4;
                        writeValue = 0;
                        len -= 4;
                        cost += 4;
                    }
                }
                pos = 0;
                currentNode = rootNode;
                writeData = 0;
            }
            mask >>= 1;
            if (mask == 0) {
                mask = 0x80000000u;
                data = gm_r32(&ms, source);
                source += 4;
                cost += 2;
            }
        }
    }
    *cycles = cost;
    return 1;
}

static int hle_diff8_wram(unsigned *regs, int *cycles)
{
    uint32_t source = regs[0], dest = regs[1];
    uint32_t header = R32(source);
    int len = (int)(header >> 8);
    uint8_t data;
    int cost = 24;
    gmap_t ms, md;

    source += 4;
    if (!src_ok_len(source, (uint32_t)len))
        return 0;

    gmap_init(&ms); gmap_init(&md);

    data = (uint8_t)gm_r8(&ms, source++);
    gm_w8(&md, dest++, data);
    len--;
    cost += 2;
    while (len > 0) {
        data += (uint8_t)gm_r8(&ms, source++);
        gm_w8(&md, dest++, data);
        len--;
        cost += 2;
    }
    *cycles = cost;
    return 1;
}

static int hle_diff8_vram(unsigned *regs, int *cycles)
{
    uint32_t source = regs[0], dest = regs[1];
    uint32_t header = R32(source);
    int len = (int)(header >> 8);
    uint8_t data;
    uint16_t writeData;
    int shift = 8, bytes = 1;
    int cost = 24;
    gmap_t ms, md;

    source += 4;
    if (!src_ok_len(source, (uint32_t)len))
        return 0;

    gmap_init(&ms); gmap_init(&md);

    data = (uint8_t)gm_r8(&ms, source++);
    writeData = data;
    while (len >= 2) {
        data += (uint8_t)gm_r8(&ms, source++);
        writeData |= (uint16_t)(data << shift);
        bytes++;
        shift += 8;
        if (bytes == 2) {
            gm_w16(&md, dest, writeData);
            dest += 2;
            len -= 2;
            bytes = 0;
            writeData = 0;
            shift = 0;
            cost += 3;
        }
    }
    *cycles = cost;
    return 1;
}

static int hle_diff16(unsigned *regs, int *cycles)
{
    uint32_t source = regs[0], dest = regs[1];
    uint32_t header = R32(source);
    int len = (int)(header >> 8);
    uint16_t data;
    int cost = 24;
    gmap_t ms, md;

    source += 4;
    if (!src_ok_len(source, (uint32_t)len))
        return 0;

    gmap_init(&ms); gmap_init(&md);

    data = (uint16_t)gm_r16(&ms, source);
    source += 2;
    gm_w16(&md, dest, data);
    dest += 2;
    len -= 2;
    cost += 3;
    while (len >= 2) {
        data += (uint16_t)gm_r16(&ms, source);
        source += 2;
        gm_w16(&md, dest, data);
        dest += 2;
        len -= 2;
        cost += 3;
    }
    *cycles = cost;
    return 1;
}

static int hle_bit_unpack(unsigned *regs, int *cycles)
{
    uint32_t source = regs[0], dest = regs[1], header = regs[2];
    int len = (int)R16(header);
    int bits, revbits, dataSize;
    uint32_t base;
    int addBase;
    int data = 0, bitwritecount = 0;
    int cost = 32;
    gmap_t ms, md;

    if (!src_ok_len(source, (uint32_t)len))
        return 0;

    gmap_init(&ms); gmap_init(&md);

    bits = R8(header + 2);
    /* A source width outside 1..8 has no meaning: 0 never advances the inner
     * loop's bit counter and 9+ shifts `mask` by a negative amount. The BIOS
     * would spin on the first and read garbage on the second, and it can do so
     * where the frame loop is still able to interrupt it. */
    if (bits == 0 || bits > 8)
        return 0;
    revbits = 8 - bits;
    base = R32(header + 4);
    addBase = (base & 0x80000000u) != 0;
    base &= 0x7fffffffu;
    dataSize = R8(header + 3);

    while (1) {
        if (--len < 0)
            break;
        {
            int mask = 0xff >> revbits;
            uint8_t b = (uint8_t)gm_r8(&ms, source++);
            int bitcount = 0;
            cost += 2;
            while (bitcount < 8) {
                uint32_t d = b & (uint32_t)mask;
                uint32_t temp = d >> bitcount;
                if (d || addBase)
                    temp += base;
                data |= (int)(temp << bitwritecount);
                bitwritecount += dataSize;
                if (bitwritecount >= 32) {
                    gm_w32(&md, dest, (uint32_t)data);
                    dest += 4;
                    data = 0;
                    bitwritecount = 0;
                    cost += 2;
                }
                mask <<= bits;
                bitcount += bits;
            }
        }
    }
    *cycles = cost;
    return 1;
}

int gba_bios_hle(unsigned number, unsigned *regs, int *cycles)
{
    *cycles = 0;

    switch (number & 0xFFu) {
    case 0x06: /* Div */
        return hle_div(regs, 0, cycles);
    case 0x07: /* DivArm */
        return hle_div(regs, 1, cycles);
    case 0x08:
        hle_sqrt(regs, cycles);
        return 1;
    case 0x09:
        hle_arctan(regs, cycles);
        return 1;
    case 0x0A:
        hle_arctan2(regs, cycles);
        return 1;
    case 0x0B:
        return hle_cpu_set(regs, cycles);
    case 0x0C:
        return hle_cpu_fast_set(regs, cycles);
    case 0x0D: /* GetBiosChecksum — official GBA value */
        regs[0] = 0xBAAE187Fu;
        *cycles = 0x4000; /* roughly: sum 16 KB of BIOS */
        return 1;
    case 0x0E:
        hle_bg_affine_set(regs, cycles);
        return 1;
    case 0x0F:
        hle_obj_affine_set(regs, cycles);
        return 1;
    case 0x10:
        return hle_bit_unpack(regs, cycles);
    case 0x11:
        return hle_lz77_wram(regs, cycles);
    case 0x12:
        return hle_lz77_vram(regs, cycles);
    case 0x13:
        return hle_huff(regs, cycles);
    case 0x14:
        return hle_rl_wram(regs, cycles);
    case 0x15:
        return hle_rl_vram(regs, cycles);
    case 0x16:
        return hle_diff8_wram(regs, cycles);
    case 0x17:
        return hle_diff8_vram(regs, cycles);
    case 0x18:
        return hle_diff16(regs, cycles);
    default:
        return 0;
    }
}
