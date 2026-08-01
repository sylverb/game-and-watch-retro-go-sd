/* M4A software-mixer HLE — the native block.
 *
 * This is a HAND TRANSLITERATION of M4A's `SoundMainRAM` mixing block: one C
 * statement per ARM instruction, in the original's control flow, with the
 * original's labels kept as C labels named after their ARM addresses, and the
 * original instruction quoted above each line. It is deliberately not "a better
 * mixer" — it is THE SAME mixer, so that its output, its memory writes and its
 * guest cycle count are bit-identical to interpreting it. Read it next to the
 * disassembly; the shape is the proof.
 *
 * Two properties make bit-exactness reachable, and both are worth saying plainly:
 *
 *   - It is all integer arithmetic. There is nothing that could round differently.
 *   - Guest CYCLES are charged, not saved. The block costs the guest exactly what
 *     it always cost, so the game's timeline does not move by a single cycle. We
 *     spend less HOST time to produce the same guest history. (That is the
 *     opposite of the idle-loop skip, which deletes guest cycles on purpose.)
 *
 * The ARM flags are carried explicitly because the block leans on them ACROSS
 * labels — most sharply at L37A8, which is reached both by falling out of a
 * `subs`/`addeq` pair and by a `bgt` from the sample-loop handler, and whose
 * `ldrsbNE` therefore means different things depending on which way you came in.
 * Get that wrong and the sample pointer walks off by one, quietly, in music only.
 */
#include "m4a_hle.h"

#include <string.h>

/* ---------------------------------------------------------------- helpers */

/* A lazily-resolved window into guest memory. gpSP maps in 32 KB pages, so a
 * host pointer is only good to the end of its page; walking off it re-maps.
 * Sample data crosses pages routinely, so this is the common path, not a corner. */
typedef struct {
    const m4a_bus *bus;
    uint8_t *host;      /* host pointer corresponding to guest `lo` */
    uint32_t lo, hi;    /* the guest range [lo, hi) that `host` covers */
    int      c8n, c32n, c32s;
    int      failed;    /* an address would not map: the whole attempt is off */
} m4a_win;

static void win_init(m4a_win *w, const m4a_bus *bus)
{
    memset(w, 0, sizeof *w);
    w->bus = bus;
    w->lo = w->hi = 1;   /* an empty range that cannot match address 0 */
}

static int win_hold(m4a_win *w, uint32_t addr, uint32_t nbytes)
{
    uint32_t span = 0;
    uint8_t *p;

    if (addr >= w->lo && addr + nbytes <= w->hi)
        return 1;
    p = w->bus->map(w->bus->ctx, addr, &span);
    if (!p || span < nbytes) {
        w->failed = 1;
        return 0;
    }
    w->host = p;
    w->lo   = addr;
    w->hi   = addr + span;
    w->bus->cost(w->bus->ctx, addr, &w->c8n, &w->c32n, &w->c32s);
    return 1;
}

static int32_t rd_s8(m4a_win *w, uint32_t addr, int32_t *cyc)
{
    if (!win_hold(w, addr, 1))
        return 0;
    *cyc -= w->c8n;
    return (int32_t)(int8_t)w->host[addr - w->lo];
}

static uint32_t rd_u8(m4a_win *w, uint32_t addr, int32_t *cyc)
{
    if (!win_hold(w, addr, 1))
        return 0;
    *cyc -= w->c8n;
    return w->host[addr - w->lo];
}

static void wr_u8(m4a_win *w, uint32_t addr, uint8_t val, int32_t *cyc)
{
    if (!win_hold(w, addr, 1))
        return;
    *cyc -= w->c8n;
    w->host[addr - w->lo] = val;
}

/* Every 32-bit access the block makes is word-aligned by construction: the mix
 * pointer keeps a 2-bit counter in its TOP bits and only touches memory when
 * that counter is zero (see L3690). A misaligned one would mean our reading of
 * the block is wrong, so refuse rather than invent the rotate the interpreter
 * would have done. Pages are 32 KB-aligned, so an aligned word never straddles
 * one. */
static uint32_t rd_u32(m4a_win *w, uint32_t addr, int32_t *cyc, int seq)
{
    const uint8_t *p;
    if (addr & 3u) { w->failed = 1; return 0; }
    if (!win_hold(w, addr, 4))
        return 0;
    *cyc -= seq ? w->c32s : w->c32n;
    p = w->host + (addr - w->lo);
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr_u32(m4a_win *w, uint32_t addr, uint32_t val, int32_t *cyc, int seq)
{
    uint8_t *p;
    if (addr & 3u) { w->failed = 1; return; }
    if (!win_hold(w, addr, 4))
        return;
    *cyc -= seq ? w->c32s : w->c32n;
    p = w->host + (addr - w->lo);
    p[0] = (uint8_t)(val);
    p[1] = (uint8_t)(val >> 8);
    p[2] = (uint8_t)(val >> 16);
    p[3] = (uint8_t)(val >> 24);
}

static uint32_t ror32(uint32_t x, uint32_t k)
{
    k &= 31u;
    return k ? ((x >> k) | (x << (32u - k))) : x;
}

/* ARM flag setters, spelled once so that every `subs`/`adds` below sets them the
 * same way. `sub` is a + ~b + 1, so C is "no borrow", i.e. a >= b unsigned. */
#define SET_LOGIC(res)      do { n_f = (res) >> 31; z_f = ((res) == 0); } while (0)
#define SET_SUB(a_, b_, d_) do { n_f = (d_) >> 31; z_f = ((d_) == 0);                 \
                                 c_f = ((a_) >= (b_));                                \
                                 v_f = ((((a_) ^ (b_)) & ((a_) ^ (d_))) >> 31); } while (0)
#define SET_ADD(a_, b_, d_) do { n_f = (d_) >> 31; z_f = ((d_) == 0);                 \
                                 c_f = ((d_) < (a_));                                 \
                                 v_f = ((~((a_) ^ (b_)) & ((a_) ^ (d_))) >> 31); } while (0)
/* `cmp rX, #0` is a subtract of zero: C is always set (no borrow is possible)
 * and V always clear. Spelled separately so the generic macro is not asked to
 * compare an unsigned value against zero, which is a tautology the compiler is
 * right to complain about. */
#define SET_CMP0(a_)        do { n_f = (a_) >> 31; z_f = ((a_) == 0);                 \
                                 c_f = 1u; v_f = 0u; } while (0)
#define COND_GT()  (!z_f && (n_f == v_f))
#define COND_LE()  ( z_f || (n_f != v_f))

/* Where every guest register lives while the block runs, and how it gets back
 * into the caller's state — spelled once, because it happens at every checkpoint
 * as well as at the end, and a register dropped in one place and not the other is
 * exactly the bug nobody finds. */
#define SAVE_REGS()  do {                                                       \
    s->r[0]  = r0;  s->r[1]  = r1;  s->r[2]  = r2;  s->r[3]  = r3;              \
    s->r[4]  = r4;  s->r[5]  = r5;  s->r[6]  = r6;  s->r[8]  = r8;              \
    s->r[9]  = r9;  s->r[10] = sl;  s->r[12] = ip;  s->r[13] = sp;              \
    s->r[14] = lr;                                                              \
    s->n = n_f; s->z = z_f; s->c = c_f; s->v = v_f;                             \
    s->cycles = cyc;                                                            \
} while (0)

/* The checkpoint, after every single guest instruction.
 *
 * The interpreter tests its cycle budget after each instruction and, when it runs
 * out, drops out of its loop to let update_gba() move the video, the timers and
 * the DMA along — then comes back and carries on from the very next instruction.
 * This block is thousands of cycles long and a slice is a scanline at most, so
 * that happens eight or ten times inside ONE call of the mixer.
 *
 * So we do the same, on the same instruction, on the same cycle: hand the state
 * back (including the PC, so that an interrupt raised in there stacks the right
 * return address), let the hardware catch up, and carry on. Running the block
 * atomically instead was tried, and the screen and the audio came out identical
 * while the CLOCK did not — which is a difference no one would see and any game
 * that reads a timer would feel. */
#define CHK(nxt)  do {                                                          \
    if (cyc <= 0) {                                                             \
        int _rc;                                                                \
        if (FAILED()) return M4A_DECLINED;                                      \
        SAVE_REGS();                                                            \
        s->pc = base + (nxt);                                                   \
        _rc = bus->refill(bus->ctx, s);                                         \
        if (_rc != M4A_OK_CONTINUE)                                             \
            return _rc;                                                         \
        cyc = s->cycles;                                                        \
    }                                                                           \
} while (0)

/* ------------------------------------------------------------- the block */

/* The bytes, exactly as they sit in IWRAM. A game whose mixer differs by one
 * instruction is a different program, and gets interpreted. */
static const uint8_t m4a_code_v1_mono[] = {
    0x00,0x80,0x8d,0xe5, 0x0a,0xa0,0xd4,0xe5, 0x0a,0xa8,0xa0,0xe1, 0x01,0x00,0xd4,0xe5,   /* +000 */
    0x08,0x00,0x10,0xe3, 0x3b,0x00,0x00,0x0a, 0x04,0x00,0x52,0xe3, 0x14,0x00,0x00,0xda,   /* +010 */
    0x08,0x20,0x52,0xe0, 0x00,0xe0,0xa0,0xc3, 0x05,0x00,0x00,0xca, 0x08,0xe0,0xa0,0xe1,   /* +020 */
    0x08,0x20,0x82,0xe0, 0x04,0x80,0x42,0xe2, 0x08,0xe0,0x4e,0xe0, 0x03,0x20,0x12,0xe2,   /* +030 */
    0x04,0x20,0xa0,0x03, 0x00,0x60,0x95,0xe5, 0xd1,0x00,0xd3,0xe0, 0x9a,0x00,0x01,0xe0,   /* +040 */
    0xff,0x18,0xc1,0xe3, 0x66,0x64,0x81,0xe0, 0x01,0x51,0x95,0xe2, 0xf9,0xff,0xff,0x3a,   /* +050 */
    0x04,0x60,0x85,0xe4, 0x04,0x80,0x58,0xe2, 0xf5,0xff,0xff,0xca, 0x0e,0x80,0x98,0xe0,   /* +060 */
    0x44,0x00,0x00,0x0a, 0x00,0x60,0x95,0xe5, 0xd1,0x00,0xd3,0xe0, 0x9a,0x00,0x01,0xe0,   /* +070 */
    0xff,0x18,0xc1,0xe3, 0x66,0x64,0x81,0xe0, 0x01,0x20,0x52,0xe2, 0x11,0x00,0x00,0x0a,   /* +080 */
    0x01,0x51,0x95,0xe2, 0xf7,0xff,0xff,0x3a, 0x04,0x60,0x85,0xe4, 0x04,0x80,0x58,0xe2,   /* +090 */
    0xdc,0xff,0xff,0xca, 0x37,0x00,0x00,0xea, 0x18,0x00,0x9d,0xe5, 0x00,0x00,0x50,0xe3,   /* +0a0 */
    0x05,0x00,0x00,0x0a, 0x14,0x30,0x9d,0xe5, 0x00,0x90,0x62,0xe2, 0x02,0x20,0x90,0xe0,   /* +0b0 */
    0x25,0x00,0x00,0xca, 0x00,0x90,0x49,0xe0, 0xfb,0xff,0xff,0xea, 0x10,0x10,0xbd,0xe8,   /* +0c0 */
    0x00,0x20,0xa0,0xe3, 0x03,0x00,0x00,0xea, 0x10,0x20,0x9d,0xe5, 0x00,0x00,0x52,0xe3,   /* +0d0 */
    0x0c,0x30,0x9d,0x15, 0xe9,0xff,0xff,0x1a, 0x00,0x20,0xc4,0xe5, 0x25,0x0f,0xa0,0xe1,   /* +0e0 */
    0x03,0x51,0xc5,0xe3, 0x03,0x00,0x60,0xe2, 0x80,0x01,0xa0,0xe1, 0x76,0x60,0xa0,0xe1,   /* +0f0 */
    0x04,0x60,0x85,0xe4, 0x21,0x00,0x00,0xea, 0x10,0x10,0x2d,0xe9, 0x1c,0xe0,0x94,0xe5,   /* +100 */
    0x20,0x10,0x94,0xe5, 0x9c,0x01,0x04,0xe0, 0xd0,0x00,0xd3,0xe1, 0xd1,0x10,0xf3,0xe1,   /* +110 */
    0x00,0x10,0x41,0xe0, 0x00,0x60,0x95,0xe5, 0x9e,0x01,0x09,0xe0, 0xc9,0x9b,0x80,0xe0,   /* +120 */
    0x9a,0x09,0x0c,0xe0, 0xff,0xc8,0xcc,0xe3, 0x66,0x64,0x8c,0xe0, 0x04,0xe0,0x8e,0xe0,   /* +130 */
    0xae,0x9b,0xb0,0xe1, 0x07,0x00,0x00,0x0a, 0xfe,0xe5,0xce,0xe3, 0x09,0x20,0x52,0xe0,   /* +140 */
    0xd4,0xff,0xff,0xda, 0x01,0x90,0x59,0xe2, 0x01,0x00,0x80,0x00, 0xd9,0x00,0xb3,0x11,   /* +150 */
    0xd1,0x10,0xf3,0xe1, 0x00,0x10,0x41,0xe0, 0x01,0x51,0x95,0xe2, 0xed,0xff,0xff,0x3a,   /* +160 */
    0x04,0x60,0x85,0xe4, 0x04,0x80,0x58,0xe2, 0xe9,0xff,0xff,0xca, 0x01,0x30,0x43,0xe2,   /* +170 */
    0x10,0x10,0xbd,0xe8, 0x1c,0xe0,0x84,0xe5, 0x18,0x20,0x84,0xe5, 0x28,0x30,0x84,0xe5,   /* +180 */
    0x00,0x80,0x9d,0xe5, 0x01,0x00,0x8f,0xe2, 0x10,0xff,0x2f,0xe1,   /* +190 */
};

#define V1_EXIT_OFF  0x194u   /* the `add r0, pc, #1` that bx's back into Thumb */

/* Regions we are willing to WRITE. EWRAM and IWRAM are plain memory. Anywhere
 * else — I/O, palette, VRAM, OAM, cart backup — a store can MEAN something, and
 * the interpreter's write path, not ours, is the one that knows what. */
static int writable_region(uint32_t addr)
{
    uint32_t r = addr >> 24;
    return r == 2u || r == 3u;
}

static int m4a_run_v1_mono(m4a_state *s, const m4a_bus *bus)
{
    /* Guest registers under their ARM names: sl = r10, ip = r12, lr = r14. */
    const uint32_t base = s->pc;   /* the block's entry address, in the guest */
    uint32_t r0, r1, r2, r3, r4, r5, r6, r8, r9, sl, ip, lr, sp;
    uint32_t n_f, z_f, c_f, v_f;
    int32_t  cyc = s->cycles;
    m4a_win  wch, wmix, wsmp, wstk;   /* channel struct, mix buffer, samples, stack */

    r0 = s->r[0];  r1 = s->r[1];  r2 = s->r[2];  r3 = s->r[3];
    r4 = s->r[4];  r5 = s->r[5];  r6 = s->r[6];  r8 = s->r[8];
    r9 = s->r[9];  sl = s->r[10]; ip = s->r[12]; sp = s->r[13];
    lr = s->r[14];
    n_f = s->n; z_f = s->z; c_f = s->c; v_f = s->v;

    /* The mix pointer carries a 2-bit counter in its top bits, and every load and
     * store below assumes that counter is zero — the block only touches memory on
     * a wrap. Were a caller ever to hand us a mid-word pointer, the "addresses"
     * the block forms would not be addresses at all, and what the interpreter
     * makes of them is its business, not ours. Decline. */
    if ((r5 >> 30) != 0u)
        return M4A_DECLINED;
    if (!writable_region(r5) || !writable_region(r4) || !writable_region(sp))
        return M4A_DECLINED;

    win_init(&wch,  bus);
    win_init(&wmix, bus);
    win_init(&wsmp, bus);
    win_init(&wstk, bus);

    /* Probe every window before writing to any of them: the block's very first
     * instruction is a store, so there is no "check as you go" that could still
     * decline cleanly. After this, a failure can only come from an address the
     * probe already accepted — but we still check, and still decline, because a
     * wrong answer is worse than a slow one. */
    if (!win_hold(&wstk, sp - 8u, 40u) || !win_hold(&wch, r4, 44u) ||
        !win_hold(&wmix, r5, 4u)       || !win_hold(&wsmp, r3, 1u))
        return M4A_DECLINED;

#define FAILED()  (wch.failed || wmix.failed || wsmp.failed || wstk.failed)

    /* 300364c: str   r8, [sp]            */ wr_u32(&wstk, sp, r8, &cyc, 0);            cyc -= 1; CHK(0x004u);
    /* 3003650: ldrb  sl, [r4, #10]       */ sl = rd_u8(&wch, r4 + 10u, &cyc);          cyc -= 1; CHK(0x008u);
    /* 3003654: lsl   sl, sl, #16         */ sl <<= 16;                                 cyc -= 1; CHK(0x00cu);
    /* 3003658: ldrb  r0, [r4, #1]        */ r0 = rd_u8(&wch, r4 + 1u, &cyc);           cyc -= 1; CHK(0x010u);
    /* 300365c: tst   r0, #8              */ SET_LOGIC(r0 & 8u);                        cyc -= 1; CHK(0x014u);
    /* 3003660: beq   L3754               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x108u); goto L3754; } CHK(0x018u);

L3664:
    /* 3003664: cmp   r2, #4              */ { uint32_t d = r2 - 4u; SET_SUB(r2, 4u, d); } cyc -= 1; CHK(0x01cu);
    /* 3003668: ble   L36C0               */ cyc -= 1; if (COND_LE()) { cyc -= 1; CHK(0x074u); goto L36C0; } CHK(0x020u);
    /* 300366c: subs  r2, r2, r8          */ { uint32_t a = r2, d = a - r8; SET_SUB(a, r8, d); r2 = d; } cyc -= 1; CHK(0x024u);
    /* 3003670: movgt lr, #0              */ cyc -= 1; if (COND_GT()) lr = 0u; CHK(0x028u);
    /* 3003674: bgt   L3690               */ cyc -= 1; if (COND_GT()) { cyc -= 1; CHK(0x044u); goto L3690; } CHK(0x02cu);
    /* 3003678: mov   lr, r8              */ lr = r8;                                   cyc -= 1; CHK(0x030u);
    /* 300367c: add   r2, r2, r8          */ r2 += r8;                                  cyc -= 1; CHK(0x034u);
    /* 3003680: sub   r8, r2, #4          */ r8 = r2 - 4u;                              cyc -= 1; CHK(0x038u);
    /* 3003684: sub   lr, lr, r8          */ lr -= r8;                                  cyc -= 1; CHK(0x03cu);
    /* 3003688: ands  r2, r2, #3          */ r2 &= 3u; SET_LOGIC(r2);                   cyc -= 1; CHK(0x040u);
    /* 300368c: moveq r2, #4              */ cyc -= 1; if (z_f) r2 = 4u; CHK(0x044u);

L3690:
    /* 3003690: ldr   r6, [r5]            */ r6 = rd_u32(&wmix, r5, &cyc, 0);           cyc -= 1; CHK(0x048u);
L3694:
    /* 3003694: ldrsb r0, [r3], #1        */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc); r3 += 1u; cyc -= 1; CHK(0x04cu);
    /* 3003698: mul   r1, sl, r0          */ r1 = sl * r0;                              cyc -= 1; CHK(0x050u);
    /* 300369c: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x054u);
#ifdef M4A_SABOTAGE
    /* See the note at the other M4A_SABOTAGE below. */
    r1 ^= 0x01000000u;
#endif
    /* 30036a0: add   r6, r1, r6, ror #8  */ r6 = r1 + ror32(r6, 8);                    cyc -= 1; CHK(0x058u);
    /* 30036a4: adds  r5, r5, #0x40000000 */ { uint32_t a = r5, d = a + 0x40000000u; SET_ADD(a, 0x40000000u, d); r5 = d; } cyc -= 1; CHK(0x05cu);
    /* 30036a8: bcc   L3694               */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x048u); goto L3694; } CHK(0x060u);
    /* 30036ac: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x064u);
    /* 30036b0: subs  r8, r8, #4          */ { uint32_t a = r8, d = a - 4u; SET_SUB(a, 4u, d); r8 = d; } cyc -= 1; CHK(0x068u);
    /* 30036b4: bgt   L3690               */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x044u); goto L3690; } CHK(0x06cu);
    /* 30036b8: adds  r8, r8, lr          */ { uint32_t a = r8, d = a + lr; SET_ADD(a, lr, d); r8 = d; } cyc -= 1; CHK(0x070u);
    /* 30036bc: beq   L37D4               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x188u); goto L37D4; } CHK(0x074u);

L36C0:
    /* 30036c0: ldr   r6, [r5]            */ r6 = rd_u32(&wmix, r5, &cyc, 0);           cyc -= 1; CHK(0x078u);
L36C4:
    /* 30036c4: ldrsb r0, [r3], #1        */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc); r3 += 1u; cyc -= 1; CHK(0x07cu);
    /* 30036c8: mul   r1, sl, r0          */ r1 = sl * r0;                              cyc -= 1; CHK(0x080u);
    /* 30036cc: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x084u);
    /* 30036d0: add   r6, r1, r6, ror #8  */ r6 = r1 + ror32(r6, 8);                    cyc -= 1; CHK(0x088u);
    /* 30036d4: subs  r2, r2, #1          */ { uint32_t a = r2, d = a - 1u; SET_SUB(a, 1u, d); r2 = d; } cyc -= 1; CHK(0x08cu);
    /* 30036d8: beq   L3724               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x0d8u); goto L3724; } CHK(0x090u);
L36DC:
    /* 30036dc: adds  r5, r5, #0x40000000 */ { uint32_t a = r5, d = a + 0x40000000u; SET_ADD(a, 0x40000000u, d); r5 = d; } cyc -= 1; CHK(0x094u);
    /* 30036e0: bcc   L36C4               */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x078u); goto L36C4; } CHK(0x098u);
    /* 30036e4: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x09cu);
    /* 30036e8: subs  r8, r8, #4          */ { uint32_t a = r8, d = a - 4u; SET_SUB(a, 4u, d); r8 = d; } cyc -= 1; CHK(0x0a0u);
    /* 30036ec: bgt   L3664               */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x018u); goto L3664; } CHK(0x0a4u);
    /* 30036f0: b     L37D4               */ cyc -= 2; CHK(0x188u); goto L37D4;

L36F4:
    /* The sample ran out. Note that these are read AFTER the push at L3754 moved
     * sp down by 8, so [sp,#24] and [sp,#20] are the caller's +16 and +12 — the
     * very two words the FAST path reads at L3724 as [sp,#16] and [sp,#12], where
     * no push has happened. One contract, reached two ways.
     *
     * `sp` is a live register here and really moves, which it did not in the first
     * version of this file: the offsets were hand-corrected against a fixed sp
     * instead. That reads the same words and is wrong anyway — because when the
     * block gives way mid-push to let the hardware catch up, an interrupt taken in
     * that window stacks itself at whatever sp SAYS. Say the wrong one and the
     * handler writes over the r4 and ip we just pushed, and the mixer comes back
     * to a channel pointer that is now a return address. */
    /* 30036f4: ldr   r0, [sp, #24]       */ r0 = rd_u32(&wstk, sp + 24u, &cyc, 0);     cyc -= 1; CHK(0x0acu);
    /* 30036f8: cmp   r0, #0              */ SET_CMP0(r0); cyc -= 1; CHK(0x0b0u);
    /* 30036fc: beq   L3718               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x0ccu); goto L3718; } CHK(0x0b4u);
    /* 3003700: ldr   r3, [sp, #20]       */ r3 = rd_u32(&wstk, sp + 20u, &cyc, 0);     cyc -= 1; CHK(0x0b8u);
    /* 3003704: rsb   r9, r2, #0          */ r9 = 0u - r2;                              cyc -= 1; CHK(0x0bcu);
L3708:
    /* 3003708: adds  r2, r0, r2          */ { uint32_t a = r0, d = a + r2; SET_ADD(a, r2, d); r2 = d; } cyc -= 1; CHK(0x0c0u);
    /* 300370c: bgt   L37A8               */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x15cu); goto L37A8; } CHK(0x0c4u);
    /* 3003710: sub   r9, r9, r0          */ r9 -= r0;                                  cyc -= 1; CHK(0x0c8u);
    /* 3003714: b     L3708               */ cyc -= 2; CHK(0x0bcu); goto L3708;

L3718:
    /* 3003718: pop   {r4, ip}            */ r4 = rd_u32(&wstk, sp, &cyc, 1);
                                             ip = rd_u32(&wstk, sp + 4u, &cyc, 1);
                                             sp += 8u;                                  cyc -= 1; CHK(0x0d0u);
    /* 300371c: mov   r2, #0              */ r2 = 0u;                                   cyc -= 1; CHK(0x0d4u);
    /* 3003720: b     L3734               */ cyc -= 2; CHK(0x0e8u); goto L3734;

L3724:
    /* 3003724: ldr   r2, [sp, #16]       */ r2 = rd_u32(&wstk, sp + 16u, &cyc, 0);     cyc -= 1; CHK(0x0dcu);
    /* 3003728: cmp   r2, #0              */ SET_CMP0(r2); cyc -= 1; CHK(0x0e0u);
    /* 300372c: ldrne r3, [sp, #12]       */ cyc -= 1; if (!z_f) r3 = rd_u32(&wstk, sp + 12u, &cyc, 0); CHK(0x0e4u);
    /* 3003730: bne   L36DC               */ cyc -= 1; if (!z_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x090u); goto L36DC; } CHK(0x0e8u);

L3734:
    /* 3003734: strb  r2, [r4]            */ wr_u8(&wch, r4, (uint8_t)r2, &cyc);        cyc -= 1; CHK(0x0ecu);
    /* 3003738: lsr   r0, r5, #30         */ r0 = r5 >> 30;                             cyc -= 1; CHK(0x0f0u);
    /* 300373c: bic   r5, r5, #0xc0000000 */ r5 &= ~0xc0000000u;                        cyc -= 1; CHK(0x0f4u);
    /* 3003740: rsb   r0, r0, #3          */ r0 = 3u - r0;                              cyc -= 1; CHK(0x0f8u);
    /* 3003744: lsl   r0, r0, #3          */ r0 <<= 3;                                  cyc -= 1; CHK(0x0fcu);
    /* 3003748: ror   r6, r6, r0          */ r6 = ror32(r6, r0 & 0xffu);                cyc -= 1; CHK(0x100u);
    /* 300374c: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x104u);
    /* 3003750: b     L37DC               */ cyc -= 2; CHK(0x190u); goto L37DC;

L3754:
    /* 3003754: push  {r4, ip}            */ sp -= 8u;
                                             wr_u32(&wstk, sp, r4, &cyc, 1);
                                             wr_u32(&wstk, sp + 4u, ip, &cyc, 1);       cyc -= 1; CHK(0x10cu);
    /* 3003758: ldr   lr, [r4, #28]       */ lr = rd_u32(&wch, r4 + 28u, &cyc, 0);      cyc -= 1; CHK(0x110u);
    /* 300375c: ldr   r1, [r4, #32]       */ r1 = rd_u32(&wch, r4 + 32u, &cyc, 0);      cyc -= 1; CHK(0x114u);
    /* 3003760: mul   r4, ip, r1          */ r4 = ip * r1;                              cyc -= 1; CHK(0x118u);
    /* 3003764: ldrsb r0, [r3]            */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc);     cyc -= 1; CHK(0x11cu);
    /* 3003768: ldrsb r1, [r3, #1]!       */ r3 += 1u; r1 = (uint32_t)rd_s8(&wsmp, r3, &cyc); cyc -= 1; CHK(0x120u);
    /* 300376c: sub   r1, r1, r0          */ r1 -= r0;                                  cyc -= 1; CHK(0x124u);
    if (FAILED()) return M4A_DECLINED;

L3770:
    /* 3003770: ldr   r6, [r5]            */ r6 = rd_u32(&wmix, r5, &cyc, 0);           cyc -= 1; CHK(0x128u);
L3774:
    /* 3003774: mul   r9, lr, r1          */ r9 = lr * r1;                              cyc -= 1; CHK(0x12cu);
    /* 3003778: add   r9, r0, r9, asr #23 */ r9 = r0 + (uint32_t)(((int32_t)r9) >> 23); cyc -= 1; CHK(0x130u);
    /* 300377c: mul   ip, sl, r9          */ ip = sl * r9;                              cyc -= 1; CHK(0x134u);
    /* 3003780: bic   ip, ip, #0xff0000   */ ip &= ~0x00ff0000u;                        cyc -= 1; CHK(0x138u);
#ifdef M4A_SABOTAGE
    /* prove.sh builds this on purpose, to check the verifier can tell. It is the
     * smallest lie the block could tell: one sample, one step quieter. Nothing
     * crashes, no screenshot changes, and no one would hear it. If the verifier's
     * green light does not go red here, the green light means nothing.
     *
     * Two things about it were wrong before they were right, and both are the same
     * mistake the verifier exists to catch:
     *
     *  - It was only in the fast mixing loop. FFTA's channels all resample, so it
     *    never ran, and the RED test "passed". A saboteur that is never executed
     *    proves as little as a test that never fails. It is now in both loops.
     *  - It flipped bit 0. But `sl` is the volume shifted left by 16, so the low
     *    sixteen bits of this product are always zero and clearing one of them
     *    changes nothing at all. The mixer's signal lives in the TOP byte — which
     *    is what bit 24 is. */
    ip ^= 0x01000000u;
#endif
    /* 3003784: add   r6, ip, r6, ror #8  */ r6 = ip + ror32(r6, 8);                    cyc -= 1; CHK(0x13cu);
    /* 3003788: add   lr, lr, r4          */ lr += r4;                                  cyc -= 1; CHK(0x140u);
    /* 300378c: lsrs  r9, lr, #23         */ r9 = lr >> 23; SET_LOGIC(r9); c_f = (lr >> 22) & 1u; cyc -= 1; CHK(0x144u);
    /* 3003790: beq   L37B4               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x168u); goto L37B4; } CHK(0x148u);
    /* 3003794: bic   lr, lr, #0x3f800000 */ lr &= ~0x3f800000u;                        cyc -= 1; CHK(0x14cu);
    /* 3003798: subs  r2, r2, r9          */ { uint32_t a = r2, d = a - r9; SET_SUB(a, r9, d); r2 = d; } cyc -= 1; CHK(0x150u);
    /* 300379c: ble   L36F4               */ cyc -= 1; if (COND_LE()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0a8u); goto L36F4; } CHK(0x154u);
    /* 30037a0: subs  r9, r9, #1          */ { uint32_t a = r9, d = a - 1u; SET_SUB(a, 1u, d); r9 = d; } cyc -= 1; CHK(0x158u);
    /* 30037a4: addeq r0, r0, r1          */ cyc -= 1; if (z_f) r0 += r1; CHK(0x15cu);
L37A8:
    /* 30037a8: ldrsbne r0, [r3, r9]!     */ if (!z_f) { r3 += r9;
                                                 r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc); }
                                             cyc -= 1; CHK(0x160u);
    /* 30037ac: ldrsb r1, [r3, #1]!       */ r3 += 1u; r1 = (uint32_t)rd_s8(&wsmp, r3, &cyc); cyc -= 1; CHK(0x164u);
    /* 30037b0: sub   r1, r1, r0          */ r1 -= r0;                                  cyc -= 1; CHK(0x168u);
L37B4:
    /* 30037b4: adds  r5, r5, #0x40000000 */ { uint32_t a = r5, d = a + 0x40000000u; SET_ADD(a, 0x40000000u, d); r5 = d; } cyc -= 1; CHK(0x16cu);
    /* 30037b8: bcc   L3774               */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x128u); goto L3774; } CHK(0x170u);
    /* 30037bc: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x174u);
    /* 30037c0: subs  r8, r8, #4          */ { uint32_t a = r8, d = a - 4u; SET_SUB(a, 4u, d); r8 = d; } cyc -= 1; CHK(0x178u);
    /* 30037c4: bgt   L3770               */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x124u); goto L3770; } CHK(0x17cu);
    /* 30037c8: sub   r3, r3, #1          */ r3 -= 1u;                                  cyc -= 1; CHK(0x180u);
    /* 30037cc: pop   {r4, ip}            */ r4 = rd_u32(&wstk, sp, &cyc, 1);
                                             ip = rd_u32(&wstk, sp + 4u, &cyc, 1);
                                             sp += 8u;                                  cyc -= 1; CHK(0x184u);
    /* 30037d0: str   lr, [r4, #28]       */ wr_u32(&wch, r4 + 28u, lr, &cyc, 0);       cyc -= 1; CHK(0x188u);

L37D4:
    /* 30037d4: str   r2, [r4, #24]       */ wr_u32(&wch, r4 + 24u, r2, &cyc, 0);       cyc -= 1; CHK(0x18cu);
    /* 30037d8: str   r3, [r4, #40]       */ wr_u32(&wch, r4 + 40u, r3, &cyc, 0);       cyc -= 1; CHK(0x190u);
L37DC:
    /* 30037dc: ldr   r8, [sp]            */ r8 = rd_u32(&wstk, sp, &cyc, 0);           cyc -= 1; CHK(0x194u);
    /* That last `cyc -= 1` is the end-of-instruction fetch charge, and it belongs
     * here because the block is accounted for ENTIRELY in this function: gpSP
     * resumes at `m4a_resume`, which sits just past its own
     * `cycles_remaining -= ws_cyc_seq[...]`, and the catch-up path does not go
     * through that line at all. Leave it out and the guest gets one cycle free per
     * block — which is not a crash, it is a clock that runs slightly fast for ever.
     * The verifier caught exactly this, and said "delta -1". */

    /* The interpreter resumes at V1_EXIT_OFF and runs the last two instructions
     * itself (`add r0, pc, #1` / `bx r0`), so the ARM-to-Thumb switch stays in the
     * one place that already knows how to do it. r0 is about to be overwritten by
     * that `add`; we still hand back the value the block left, because a
     * transliteration that is only right where someone is looking is not one. */
    if (FAILED())
        return M4A_DECLINED;

    SAVE_REGS();
    return M4A_DONE;

#undef FAILED
}


/* ---------------------------------------------------- variant 2: stereo */

/* The same routine, from a build of M4A that mixes in stereo — Pokemon Emerald's,
 * and most of the library's. It is the same shape with a second accumulator: `r6`
 * is the left channel, `r7` the right, and the right-hand mix buffer sits a fixed
 * 0x630 bytes past the left one. The phase accumulator moved from `lr` to `r9`,
 * which is the sort of difference that makes this a different program and not a
 * parameter.
 *
 * One path is NOT transliterated: when the channel's status has bit 4 or 5 set
 * (`tst r0, #0x30`), the routine calls out to a subroutine that sets a sample up
 * — computes its loop points, marks it started. That happens once when a note
 * begins, not once per sample, so it is nothing to the frame; and it is a whole
 * second routine to get exactly right. We look at the status byte BEFORE touching
 * anything and hand those blocks straight to the interpreter, which is what
 * declining is for.
 */
/* CHK() saves the guest registers through SAVE_REGS, and the stereo block lives in
 * two more of them than the mono one does — r7 is the right channel's accumulator
 * and fp its volume. Redefine it here so that a checkpoint taken mid-block hands
 * back the whole machine and not most of it: the compiler cannot tell you that a
 * macro named after "save the registers" saved eleven of thirteen. */
#undef SAVE_REGS
#define SAVE_REGS()  do {                                                       \
    s->r[0]  = r0;  s->r[1]  = r1;  s->r[2]  = r2;  s->r[3]  = r3;              \
    s->r[4]  = r4;  s->r[5]  = r5;  s->r[6]  = r6;  s->r[7]  = r7;              \
    s->r[8]  = r8;  s->r[9]  = r9;  s->r[10] = sl;  s->r[11] = fp;              \
    s->r[12] = ip;  s->r[13] = sp;  s->r[14] = lr;                              \
    s->n = n_f; s->z = z_f; s->c = c_f; s->v = v_f;                             \
    s->cycles = cyc;                                                            \
} while (0)

static const uint8_t m4a_code_v2_stereo[] = {
    0x00,0x80,0x8d,0xe5, 0x1c,0x90,0x94,0xe5, 0x0a,0xa0,0xd4,0xe5, 0x0b,0xb0,0xd4,0xe5,   /* +000 */
    0x01,0x00,0xd4,0xe5, 0x30,0x00,0x10,0xe3, 0x01,0x00,0x00,0x0a, 0x7f,0x00,0x00,0xeb,   /* +010 */
    0x6f,0x00,0x00,0xea, 0x0a,0xa8,0xa0,0xe1, 0x0b,0xb8,0xa0,0xe1, 0x01,0x00,0xd4,0xe5,   /* +020 */
    0x08,0x00,0x10,0xe3, 0x47,0x00,0x00,0x0a, 0x04,0x00,0x52,0xe3, 0x19,0x00,0x00,0xda,   /* +030 */
    0x08,0x20,0x52,0xe0, 0x00,0x90,0xa0,0xc3, 0x05,0x00,0x00,0xca, 0x08,0x90,0xa0,0xe1,   /* +040 */
    0x08,0x20,0x82,0xe0, 0x04,0x80,0x42,0xe2, 0x08,0x90,0x49,0xe0, 0x03,0x20,0x12,0xe2,   /* +050 */
    0x04,0x20,0xa0,0x03, 0x00,0x60,0x95,0xe5, 0x30,0x76,0x95,0xe5, 0xd1,0x00,0xd3,0xe0,   /* +060 */
    0x9a,0x00,0x01,0xe0, 0xff,0x18,0xc1,0xe3, 0x66,0x64,0x81,0xe0, 0x9b,0x00,0x01,0xe0,   /* +070 */
    0xff,0x18,0xc1,0xe3, 0x67,0x74,0x81,0xe0, 0x01,0x51,0x95,0xe2, 0xf6,0xff,0xff,0x3a,   /* +080 */
    0x30,0x76,0x85,0xe5, 0x04,0x60,0x85,0xe4, 0x04,0x80,0x58,0xe2, 0xf0,0xff,0xff,0xca,   /* +090 */
    0x09,0x80,0x98,0xe0, 0x4f,0x00,0x00,0x0a, 0x00,0x60,0x95,0xe5, 0x30,0x76,0x95,0xe5,   /* +0a0 */
    0xd1,0x00,0xd3,0xe0, 0x9a,0x00,0x01,0xe0, 0xff,0x18,0xc1,0xe3, 0x66,0x64,0x81,0xe0,   /* +0b0 */
    0x9b,0x00,0x01,0xe0, 0xff,0x18,0xc1,0xe3, 0x67,0x74,0x81,0xe0, 0x01,0x20,0x52,0xe2,   /* +0c0 */
    0x12,0x00,0x00,0x0a, 0x01,0x51,0x95,0xe2, 0xf4,0xff,0xff,0x3a, 0x30,0x76,0x85,0xe5,   /* +0d0 */
    0x04,0x60,0x85,0xe4, 0x04,0x80,0x58,0xe2, 0xd2,0xff,0xff,0xca, 0x3d,0x00,0x00,0xea,   /* +0e0 */
    0x18,0x00,0x9d,0xe5, 0x00,0x00,0x50,0xe3, 0x05,0x00,0x00,0x0a, 0x14,0x30,0x9d,0xe5,   /* +0f0 */
    0x00,0xe0,0x62,0xe2, 0x02,0x20,0x90,0xe0, 0x2a,0x00,0x00,0xca, 0x00,0xe0,0x4e,0xe0,   /* +100 */
    0xfb,0xff,0xff,0xea, 0x10,0x10,0xbd,0xe8, 0x00,0x20,0xa0,0xe3, 0x03,0x00,0x00,0xea,   /* +110 */
    0x10,0x20,0x9d,0xe5, 0x00,0x00,0x52,0xe3, 0x0c,0x30,0x9d,0x15, 0xe8,0xff,0xff,0x1a,   /* +120 */
    0x00,0x20,0xc4,0xe5, 0x25,0x0f,0xa0,0xe1, 0x03,0x51,0xc5,0xe3, 0x03,0x00,0x60,0xe2,   /* +130 */
    0x80,0x01,0xa0,0xe1, 0x76,0x60,0xa0,0xe1, 0x77,0x70,0xa0,0xe1, 0x30,0x76,0x85,0xe5,   /* +140 */
    0x04,0x60,0x85,0xe4, 0x25,0x00,0x00,0xea, 0x10,0x10,0x2d,0xe9, 0x20,0x10,0x94,0xe5,   /* +150 */
    0x9c,0x01,0x04,0xe0, 0xd0,0x00,0xd3,0xe1, 0xd1,0x10,0xf3,0xe1, 0x00,0x10,0x41,0xe0,   /* +160 */
    0x00,0x60,0x95,0xe5, 0x30,0x76,0x95,0xe5, 0x99,0x01,0x0e,0xe0, 0xce,0xeb,0x80,0xe0,   /* +170 */
    0x9a,0x0e,0x0c,0xe0, 0xff,0xc8,0xcc,0xe3, 0x66,0x64,0x8c,0xe0, 0x9b,0x0e,0x0c,0xe0,   /* +180 */
    0xff,0xc8,0xcc,0xe3, 0x67,0x74,0x8c,0xe0, 0x04,0x90,0x89,0xe0, 0xa9,0xeb,0xb0,0xe1,   /* +190 */
    0x07,0x00,0x00,0x0a, 0xfe,0x95,0xc9,0xe3, 0x0e,0x20,0x52,0xe0, 0xcf,0xff,0xff,0xda,   /* +1a0 */
    0x01,0xe0,0x5e,0xe2, 0x01,0x00,0x80,0x00, 0xde,0x00,0xb3,0x11, 0xd1,0x10,0xf3,0xe1,   /* +1b0 */
    0x00,0x10,0x41,0xe0, 0x01,0x51,0x95,0xe2, 0xea,0xff,0xff,0x3a, 0x30,0x76,0x85,0xe5,   /* +1c0 */
    0x04,0x60,0x85,0xe4, 0x04,0x80,0x58,0xe2, 0xe4,0xff,0xff,0xca, 0x01,0x30,0x43,0xe2,   /* +1d0 */
    0x10,0x10,0xbd,0xe8, 0x1c,0x90,0x84,0xe5, 0x18,0x20,0x84,0xe5, 0x28,0x30,0x84,0xe5,   /* +1e0 */
    0x00,0x80,0x9d,0xe5, 0x01,0x00,0x8f,0xe2, 0x10,0xff,0x2f,0xe1,   /* +1f0 */
};

#define V2_EXIT_OFF  0x1f4u

static int m4a_run_v2_stereo(m4a_state *s, const m4a_bus *bus)
{
    const uint32_t base = s->pc;
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, ip, lr, sp;
    uint32_t n_f, z_f, c_f, v_f;
    int32_t  cyc = s->cycles;
    m4a_win  wch, wmix, wsmp, wstk;

    r0 = s->r[0];  r1 = s->r[1];  r2 = s->r[2];  r3 = s->r[3];
    r4 = s->r[4];  r5 = s->r[5];  r6 = s->r[6];  r7 = s->r[7];
    r8 = s->r[8];  r9 = s->r[9];  sl = s->r[10]; fp = s->r[11];
    ip = s->r[12]; sp = s->r[13]; lr = s->r[14];
    n_f = s->n; z_f = s->z; c_f = s->c; v_f = s->v;

    if ((r5 >> 30) != 0u)
        return M4A_DECLINED;
    if (!writable_region(r5) || !writable_region(r4) || !writable_region(sp))
        return M4A_DECLINED;

    win_init(&wch,  bus);
    win_init(&wmix, bus);
    win_init(&wsmp, bus);
    win_init(&wstk, bus);

    /* The mix window has to reach BOTH channels: the left one walks forward from
     * r5 for r8 bytes, and the right one is the same walk 0x630 further on. */
    if (!win_hold(&wstk, sp - 8u, 40u) || !win_hold(&wch, r4, 44u) ||
        !win_hold(&wmix, r5, 0x630u + r8 + 8u) || !win_hold(&wsmp, r3, 1u))
        return M4A_DECLINED;

    /* The sample-setup path calls a subroutine we have not transliterated. Look
     * before leaping: nothing has been written yet, so declining here is free. */
    {
        int32_t peek_cyc = 0;
        uint32_t status = rd_u8(&wch, r4 + 1u, &peek_cyc);
        if (wch.failed || (status & 0x30u))
            return M4A_DECLINED;
    }

#define FAILED()  (wch.failed || wmix.failed || wsmp.failed || wstk.failed)

    /* 3001c44: str   r8, [sp]            */ wr_u32(&wstk, sp, r8, &cyc, 0);            cyc -= 1; CHK(0x004u);
    /* 3001c48: ldr   r9, [r4, #28]       */ r9 = rd_u32(&wch, r4 + 28u, &cyc, 0);      cyc -= 1; CHK(0x008u);
    /* 3001c4c: ldrb  sl, [r4, #10]       */ sl = rd_u8(&wch, r4 + 10u, &cyc);          cyc -= 1; CHK(0x00cu);
    /* 3001c50: ldrb  fp, [r4, #11]       */ fp = rd_u8(&wch, r4 + 11u, &cyc);          cyc -= 1; CHK(0x010u);
    /* 3001c54: ldrb  r0, [r4, #1]        */ r0 = rd_u8(&wch, r4 + 1u, &cyc);           cyc -= 1; CHK(0x014u);
    /* 3001c58: tst   r0, #48             */ SET_LOGIC(r0 & 0x30u);                     cyc -= 1; CHK(0x018u);
    /* 3001c5c: beq   L1C68               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x024u); goto L1C68; } CHK(0x01cu);
    /* 3001c60: bl    <sample setup>      */
    /* 3001c64: b     L1E28               */
    /* Unreachable: the status byte was checked above and those blocks declined.
     * If we are here, the check and the code disagree, and the code wins. */
    return M4A_DECLINED;

L1C68:
    /* 3001c68: lsl   sl, sl, #16         */ sl <<= 16;                                 cyc -= 1; CHK(0x028u);
    /* 3001c6c: lsl   fp, fp, #16         */ fp <<= 16;                                 cyc -= 1; CHK(0x02cu);
    /* 3001c70: ldrb  r0, [r4, #1]        */ r0 = rd_u8(&wch, r4 + 1u, &cyc);           cyc -= 1; CHK(0x030u);
    /* 3001c74: tst   r0, #8              */ SET_LOGIC(r0 & 8u);                        cyc -= 1; CHK(0x034u);
    /* 3001c78: beq   L1D9C               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x158u); goto L1D9C; } CHK(0x038u);

L1C7C:
    /* 3001c7c: cmp   r2, #4              */ { uint32_t d = r2 - 4u; SET_SUB(r2, 4u, d); } cyc -= 1; CHK(0x03cu);
    /* 3001c80: ble   L1CEC               */ cyc -= 1; if (COND_LE()) { cyc -= 1; CHK(0x0a8u); goto L1CEC; } CHK(0x040u);
    /* 3001c84: subs  r2, r2, r8          */ { uint32_t a = r2, d = a - r8; SET_SUB(a, r8, d); r2 = d; } cyc -= 1; CHK(0x044u);
    /* 3001c88: movgt r9, #0              */ cyc -= 1; if (COND_GT()) r9 = 0u;          CHK(0x048u);
    /* 3001c8c: bgt   L1CA8               */ cyc -= 1; if (COND_GT()) { cyc -= 1; CHK(0x064u); goto L1CA8; } CHK(0x04cu);
    /* 3001c90: mov   r9, r8              */ r9 = r8;                                   cyc -= 1; CHK(0x050u);
    /* 3001c94: add   r2, r2, r8          */ r2 += r8;                                  cyc -= 1; CHK(0x054u);
    /* 3001c98: sub   r8, r2, #4          */ r8 = r2 - 4u;                              cyc -= 1; CHK(0x058u);
    /* 3001c9c: sub   r9, r9, r8          */ r9 -= r8;                                  cyc -= 1; CHK(0x05cu);
    /* 3001ca0: ands  r2, r2, #3          */ r2 &= 3u; SET_LOGIC(r2);                   cyc -= 1; CHK(0x060u);
    /* 3001ca4: moveq r2, #4              */ cyc -= 1; if (z_f) r2 = 4u;                CHK(0x064u);

L1CA8:
    /* 3001ca8: ldr   r6, [r5]            */ r6 = rd_u32(&wmix, r5, &cyc, 0);           cyc -= 1; CHK(0x068u);
    /* 3001cac: ldr   r7, [r5, #1584]     */ r7 = rd_u32(&wmix, r5 + 0x630u, &cyc, 0);  cyc -= 1; CHK(0x06cu);
L1CB0:
    /* 3001cb0: ldrsb r0, [r3], #1        */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc); r3 += 1u; cyc -= 1; CHK(0x070u);
    /* 3001cb4: mul   r1, sl, r0          */ r1 = sl * r0;                              cyc -= 1; CHK(0x074u);
    /* 3001cb8: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x078u);
    /* 3001cbc: add   r6, r1, r6, ror #8  */ r6 = r1 + ror32(r6, 8);                    cyc -= 1; CHK(0x07cu);
    /* 3001cc0: mul   r1, fp, r0          */ r1 = fp * r0;                              cyc -= 1; CHK(0x080u);
    /* 3001cc4: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x084u);
    /* 3001cc8: add   r7, r1, r7, ror #8  */ r7 = r1 + ror32(r7, 8);                    cyc -= 1; CHK(0x088u);
    /* 3001ccc: adds  r5, r5, #0x40000000 */ { uint32_t a = r5, d = a + 0x40000000u; SET_ADD(a, 0x40000000u, d); r5 = d; } cyc -= 1; CHK(0x08cu);
    /* 3001cd0: bcc   L1CB0               */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x06cu); goto L1CB0; } CHK(0x090u);
    /* 3001cd4: str   r7, [r5, #1584]     */ wr_u32(&wmix, r5 + 0x630u, r7, &cyc, 0);   cyc -= 1; CHK(0x094u);
    /* 3001cd8: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x098u);
    /* 3001cdc: subs  r8, r8, #4          */ { uint32_t a = r8, d = a - 4u; SET_SUB(a, 4u, d); r8 = d; } cyc -= 1; CHK(0x09cu);
    /* 3001ce0: bgt   L1CA8               */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x064u); goto L1CA8; } CHK(0x0a0u);
    /* 3001ce4: adds  r8, r8, r9          */ { uint32_t a = r8, d = a + r9; SET_ADD(a, r9, d); r8 = d; } cyc -= 1; CHK(0x0a4u);
    /* 3001ce8: beq   L1E2C               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x1e8u); goto L1E2C; } CHK(0x0a8u);

L1CEC:
    /* 3001cec: ldr   r6, [r5]            */ r6 = rd_u32(&wmix, r5, &cyc, 0);           cyc -= 1; CHK(0x0acu);
    /* 3001cf0: ldr   r7, [r5, #1584]     */ r7 = rd_u32(&wmix, r5 + 0x630u, &cyc, 0);  cyc -= 1; CHK(0x0b0u);
L1CF4:
    /* 3001cf4: ldrsb r0, [r3], #1        */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc); r3 += 1u; cyc -= 1; CHK(0x0b4u);
    /* 3001cf8: mul   r1, sl, r0          */ r1 = sl * r0;                              cyc -= 1; CHK(0x0b8u);
    /* 3001cfc: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x0bcu);
    /* 3001d00: add   r6, r1, r6, ror #8  */ r6 = r1 + ror32(r6, 8);                    cyc -= 1; CHK(0x0c0u);
    /* 3001d04: mul   r1, fp, r0          */ r1 = fp * r0;                              cyc -= 1; CHK(0x0c4u);
    /* 3001d08: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x0c8u);
    /* 3001d0c: add   r7, r1, r7, ror #8  */ r7 = r1 + ror32(r7, 8);                    cyc -= 1; CHK(0x0ccu);
    /* 3001d10: subs  r2, r2, #1          */ { uint32_t a = r2, d = a - 1u; SET_SUB(a, 1u, d); r2 = d; } cyc -= 1; CHK(0x0d0u);
    /* 3001d14: beq   L1D64               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x120u); goto L1D64; } CHK(0x0d4u);
L1D18:
    /* 3001d18: adds  r5, r5, #0x40000000 */ { uint32_t a = r5, d = a + 0x40000000u; SET_ADD(a, 0x40000000u, d); r5 = d; } cyc -= 1; CHK(0x0d8u);
    /* 3001d1c: bcc   L1CF4               */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0b0u); goto L1CF4; } CHK(0x0dcu);
    /* 3001d20: str   r7, [r5, #1584]     */ wr_u32(&wmix, r5 + 0x630u, r7, &cyc, 0);   cyc -= 1; CHK(0x0e0u);
    /* 3001d24: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x0e4u);
    /* 3001d28: subs  r8, r8, #4          */ { uint32_t a = r8, d = a - 4u; SET_SUB(a, 4u, d); r8 = d; } cyc -= 1; CHK(0x0e8u);
    /* 3001d2c: bgt   L1C7C               */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x038u); goto L1C7C; } CHK(0x0ecu);
    /* 3001d30: b     L1E2C               */ cyc -= 2; CHK(0x1e8u); goto L1E2C;

L1D34:
    /* 3001d34: ldr   r0, [sp, #24]       */ r0 = rd_u32(&wstk, sp + 24u, &cyc, 0);     cyc -= 1; CHK(0x0f4u);
    /* 3001d38: cmp   r0, #0              */ SET_CMP0(r0);                              cyc -= 1; CHK(0x0f8u);
    /* 3001d3c: beq   L1D58               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x114u); goto L1D58; } CHK(0x0fcu);
    /* 3001d40: ldr   r3, [sp, #20]       */ r3 = rd_u32(&wstk, sp + 20u, &cyc, 0);     cyc -= 1; CHK(0x100u);
    /* 3001d44: rsb   lr, r2, #0          */ lr = 0u - r2;                              cyc -= 1; CHK(0x104u);
L1D48:
    /* 3001d48: adds  r2, r0, r2          */ { uint32_t a = r0, d = a + r2; SET_ADD(a, r2, d); r2 = d; } cyc -= 1; CHK(0x108u);
    /* 3001d4c: bgt   L1DFC               */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x1b8u); goto L1DFC; } CHK(0x10cu);
    /* 3001d50: sub   lr, lr, r0          */ lr -= r0;                                  cyc -= 1; CHK(0x110u);
    /* 3001d54: b     L1D48               */ cyc -= 2; CHK(0x104u); goto L1D48;

L1D58:
    /* 3001d58: pop   {r4, ip}            */ r4 = rd_u32(&wstk, sp, &cyc, 1);
                                             ip = rd_u32(&wstk, sp + 4u, &cyc, 1);
                                             sp += 8u;                                  cyc -= 1; CHK(0x118u);
    /* 3001d5c: mov   r2, #0              */ r2 = 0u;                                   cyc -= 1; CHK(0x11cu);
    /* 3001d60: b     L1D74               */ cyc -= 2; CHK(0x130u); goto L1D74;

L1D64:
    /* 3001d64: ldr   r2, [sp, #16]       */ r2 = rd_u32(&wstk, sp + 16u, &cyc, 0);     cyc -= 1; CHK(0x124u);
    /* 3001d68: cmp   r2, #0              */ SET_CMP0(r2);                              cyc -= 1; CHK(0x128u);
    /* 3001d6c: ldrne r3, [sp, #12]       */ cyc -= 1; if (!z_f) r3 = rd_u32(&wstk, sp + 12u, &cyc, 0); CHK(0x12cu);
    /* 3001d70: bne   L1D18               */ cyc -= 1; if (!z_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0d4u); goto L1D18; } CHK(0x130u);

L1D74:
    /* 3001d74: strb  r2, [r4]            */ wr_u8(&wch, r4, (uint8_t)r2, &cyc);        cyc -= 1; CHK(0x134u);
    /* 3001d78: lsr   r0, r5, #30         */ r0 = r5 >> 30;                             cyc -= 1; CHK(0x138u);
    /* 3001d7c: bic   r5, r5, #0xc0000000 */ r5 &= ~0xc0000000u;                        cyc -= 1; CHK(0x13cu);
    /* 3001d80: rsb   r0, r0, #3          */ r0 = 3u - r0;                              cyc -= 1; CHK(0x140u);
    /* 3001d84: lsl   r0, r0, #3          */ r0 <<= 3;                                  cyc -= 1; CHK(0x144u);
    /* 3001d88: ror   r6, r6, r0          */ r6 = ror32(r6, r0 & 0xffu);                cyc -= 1; CHK(0x148u);
    /* 3001d8c: ror   r7, r7, r0          */ r7 = ror32(r7, r0 & 0xffu);                cyc -= 1; CHK(0x14cu);
    /* 3001d90: str   r7, [r5, #1584]     */ wr_u32(&wmix, r5 + 0x630u, r7, &cyc, 0);   cyc -= 1; CHK(0x150u);
    /* 3001d94: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x154u);
    /* 3001d98: b     L1E34               */ cyc -= 2; CHK(0x1f0u); goto L1E34;

L1D9C:
    /* 3001d9c: push  {r4, ip}            */ sp -= 8u;
                                             wr_u32(&wstk, sp, r4, &cyc, 1);
                                             wr_u32(&wstk, sp + 4u, ip, &cyc, 1);       cyc -= 1; CHK(0x15cu);
    /* 3001da0: ldr   r1, [r4, #32]       */ r1 = rd_u32(&wch, r4 + 32u, &cyc, 0);      cyc -= 1; CHK(0x160u);
    /* 3001da4: mul   r4, ip, r1          */ r4 = ip * r1;                              cyc -= 1; CHK(0x164u);
    /* 3001da8: ldrsb r0, [r3]            */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc);     cyc -= 1; CHK(0x168u);
    /* 3001dac: ldrsb r1, [r3, #1]!       */ r3 += 1u; r1 = (uint32_t)rd_s8(&wsmp, r3, &cyc); cyc -= 1; CHK(0x16cu);
    /* 3001db0: sub   r1, r1, r0          */ r1 -= r0;                                  cyc -= 1; CHK(0x170u);

L1DB4:
    /* 3001db4: ldr   r6, [r5]            */ r6 = rd_u32(&wmix, r5, &cyc, 0);           cyc -= 1; CHK(0x174u);
    /* 3001db8: ldr   r7, [r5, #1584]     */ r7 = rd_u32(&wmix, r5 + 0x630u, &cyc, 0);  cyc -= 1; CHK(0x178u);
L1DBC:
    /* 3001dbc: mul   lr, r9, r1          */ lr = r9 * r1;                              cyc -= 1; CHK(0x17cu);
    /* 3001dc0: add   lr, r0, lr, asr #23 */ lr = r0 + (uint32_t)(((int32_t)lr) >> 23); cyc -= 1; CHK(0x180u);
    /* 3001dc4: mul   ip, sl, lr          */ ip = sl * lr;                              cyc -= 1; CHK(0x184u);
    /* 3001dc8: bic   ip, ip, #0xff0000   */ ip &= ~0x00ff0000u;                        cyc -= 1; CHK(0x188u);
#ifdef M4A_SABOTAGE
    /* The RED, in the loop this variant actually runs. See the note in the mono
     * block: a saboteur on a path the game never takes proves nothing. */
    ip ^= 0x01000000u;
#endif
    /* 3001dcc: add   r6, ip, r6, ror #8  */ r6 = ip + ror32(r6, 8);                    cyc -= 1; CHK(0x18cu);
    /* 3001dd0: mul   ip, fp, lr          */ ip = fp * lr;                              cyc -= 1; CHK(0x190u);
    /* 3001dd4: bic   ip, ip, #0xff0000   */ ip &= ~0x00ff0000u;                        cyc -= 1; CHK(0x194u);
    /* 3001dd8: add   r7, ip, r7, ror #8  */ r7 = ip + ror32(r7, 8);                    cyc -= 1; CHK(0x198u);
    /* 3001ddc: add   r9, r9, r4          */ r9 += r4;                                  cyc -= 1; CHK(0x19cu);
    /* 3001de0: lsrs  lr, r9, #23         */ lr = r9 >> 23; SET_LOGIC(lr); c_f = (r9 >> 22) & 1u; cyc -= 1; CHK(0x1a0u);
    /* 3001de4: beq   L1E08               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x1c4u); goto L1E08; } CHK(0x1a4u);
    /* 3001de8: bic   r9, r9, #0x3f800000 */ r9 &= ~0x3f800000u;                        cyc -= 1; CHK(0x1a8u);
    /* 3001dec: subs  r2, r2, lr          */ { uint32_t a = r2, d = a - lr; SET_SUB(a, lr, d); r2 = d; } cyc -= 1; CHK(0x1acu);
    /* 3001df0: ble   L1D34               */ cyc -= 1; if (COND_LE()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0f0u); goto L1D34; } CHK(0x1b0u);
    /* 3001df4: subs  lr, lr, #1          */ { uint32_t a = lr, d = a - 1u; SET_SUB(a, 1u, d); lr = d; } cyc -= 1; CHK(0x1b4u);
    /* 3001df8: addeq r0, r0, r1          */ cyc -= 1; if (z_f) r0 += r1;               CHK(0x1b8u);
L1DFC:
    /* 3001dfc: ldrsbne r0, [r3, lr]!     */ if (!z_f) { r3 += lr;
                                                 r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc); }
                                             cyc -= 1; CHK(0x1bcu);
    /* 3001e00: ldrsb r1, [r3, #1]!       */ r3 += 1u; r1 = (uint32_t)rd_s8(&wsmp, r3, &cyc); cyc -= 1; CHK(0x1c0u);
    /* 3001e04: sub   r1, r1, r0          */ r1 -= r0;                                  cyc -= 1; CHK(0x1c4u);
L1E08:
    /* 3001e08: adds  r5, r5, #0x40000000 */ { uint32_t a = r5, d = a + 0x40000000u; SET_ADD(a, 0x40000000u, d); r5 = d; } cyc -= 1; CHK(0x1c8u);
    /* 3001e0c: bcc   L1DBC               */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x178u); goto L1DBC; } CHK(0x1ccu);
    /* 3001e10: str   r7, [r5, #1584]     */ wr_u32(&wmix, r5 + 0x630u, r7, &cyc, 0);   cyc -= 1; CHK(0x1d0u);
    /* 3001e14: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x1d4u);
    /* 3001e18: subs  r8, r8, #4          */ { uint32_t a = r8, d = a - 4u; SET_SUB(a, 4u, d); r8 = d; } cyc -= 1; CHK(0x1d8u);
    /* 3001e1c: bgt   L1DB4               */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x170u); goto L1DB4; } CHK(0x1dcu);
    /* 3001e20: sub   r3, r3, #1          */ r3 -= 1u;                                  cyc -= 1; CHK(0x1e0u);
    /* 3001e24: pop   {r4, ip}            */ r4 = rd_u32(&wstk, sp, &cyc, 1);
                                             ip = rd_u32(&wstk, sp + 4u, &cyc, 1);
                                             sp += 8u;                                  cyc -= 1; CHK(0x1e4u);
    /* 3001e28: str   r9, [r4, #28]       */ wr_u32(&wch, r4 + 28u, r9, &cyc, 0);       cyc -= 1; CHK(0x1e8u);
L1E2C:
    /* 3001e2c: str   r2, [r4, #24]       */ wr_u32(&wch, r4 + 24u, r2, &cyc, 0);       cyc -= 1; CHK(0x1ecu);
    /* 3001e30: str   r3, [r4, #40]       */ wr_u32(&wch, r4 + 40u, r3, &cyc, 0);       cyc -= 1; CHK(0x1f0u);
L1E34:
    /* 3001e34: ldr   r8, [sp]            */ r8 = rd_u32(&wstk, sp, &cyc, 0);           cyc -= 1;

    if (FAILED())
        return M4A_DECLINED;

    s->r[0]  = r0;  s->r[1]  = r1;  s->r[2]  = r2;  s->r[3]  = r3;
    s->r[4]  = r4;  s->r[5]  = r5;  s->r[6]  = r6;  s->r[7]  = r7;
    s->r[8]  = r8;  s->r[9]  = r9;  s->r[10] = sl;  s->r[11] = fp;
    s->r[12] = ip;  s->r[13] = sp; s->r[14] = lr;
    s->n = n_f; s->z = z_f; s->c = c_f; s->v = v_f;
    s->cycles = cyc;
    return M4A_DONE;

#undef FAILED
}


/* ------------------------------------------- variant 3: stereo, no setup call */

/* The most common build of the three, by a distance: 210 of 633 carts in the
 * corpus, against 99 for the mono one and 8 for the other stereo. Zelda: A Link
 * to the Past / Four Swords, Mario & Luigi, Mario Tennis, Mario Golf — this is
 * the one Nintendo shipped.
 *
 * It is exactly the two we already have, welded together: the phase accumulator
 * is in `lr` and the interpolation temp in `r9`, as in the mono block, while the
 * mixing is stereo, with `fp` the right volume, `r7` the right accumulator, and
 * the right mix buffer a fixed 0x630 past the left — as in the other. And it has
 * no `tst r0, #0x30` at the top and no call out to a sample-setup subroutine, so
 * unlike the stereo variant there is no path here we have to hand back.
 */
static const uint8_t m4a_code_v3_stereo2[] = {
    0x00,0x80,0x8d,0xe5, 0x0a,0xa0,0xd4,0xe5, 0x0b,0xb0,0xd4,0xe5, 0x0a,0xa8,0xa0,0xe1,   /* +000 */
    0x0b,0xb8,0xa0,0xe1, 0x01,0x00,0xd4,0xe5, 0x08,0x00,0x10,0xe3, 0x47,0x00,0x00,0x0a,   /* +010 */
    0x04,0x00,0x52,0xe3, 0x19,0x00,0x00,0xda, 0x08,0x20,0x52,0xe0, 0x00,0xe0,0xa0,0xc3,   /* +020 */
    0x05,0x00,0x00,0xca, 0x08,0xe0,0xa0,0xe1, 0x08,0x20,0x82,0xe0, 0x04,0x80,0x42,0xe2,   /* +030 */
    0x08,0xe0,0x4e,0xe0, 0x03,0x20,0x12,0xe2, 0x04,0x20,0xa0,0x03, 0x00,0x60,0x95,0xe5,   /* +040 */
    0x30,0x76,0x95,0xe5, 0xd1,0x00,0xd3,0xe0, 0x9a,0x00,0x01,0xe0, 0xff,0x18,0xc1,0xe3,   /* +050 */
    0x66,0x64,0x81,0xe0, 0x9b,0x00,0x01,0xe0, 0xff,0x18,0xc1,0xe3, 0x67,0x74,0x81,0xe0,   /* +060 */
    0x01,0x51,0x95,0xe2, 0xf6,0xff,0xff,0x3a, 0x30,0x76,0x85,0xe5, 0x04,0x60,0x85,0xe4,   /* +070 */
    0x04,0x80,0x58,0xe2, 0xf0,0xff,0xff,0xca, 0x0e,0x80,0x98,0xe0, 0x50,0x00,0x00,0x0a,   /* +080 */
    0x00,0x60,0x95,0xe5, 0x30,0x76,0x95,0xe5, 0xd1,0x00,0xd3,0xe0, 0x9a,0x00,0x01,0xe0,   /* +090 */
    0xff,0x18,0xc1,0xe3, 0x66,0x64,0x81,0xe0, 0x9b,0x00,0x01,0xe0, 0xff,0x18,0xc1,0xe3,   /* +0a0 */
    0x67,0x74,0x81,0xe0, 0x01,0x20,0x52,0xe2, 0x12,0x00,0x00,0x0a, 0x01,0x51,0x95,0xe2,   /* +0b0 */
    0xf4,0xff,0xff,0x3a, 0x30,0x76,0x85,0xe5, 0x04,0x60,0x85,0xe4, 0x04,0x80,0x58,0xe2,   /* +0c0 */
    0xd2,0xff,0xff,0xca, 0x3e,0x00,0x00,0xea, 0x18,0x00,0x9d,0xe5, 0x00,0x00,0x50,0xe3,   /* +0d0 */
    0x05,0x00,0x00,0x0a, 0x14,0x30,0x9d,0xe5, 0x00,0x90,0x62,0xe2, 0x02,0x20,0x90,0xe0,   /* +0e0 */
    0x2b,0x00,0x00,0xca, 0x00,0x90,0x49,0xe0, 0xfb,0xff,0xff,0xea, 0x10,0x10,0xbd,0xe8,   /* +0f0 */
    0x00,0x20,0xa0,0xe3, 0x03,0x00,0x00,0xea, 0x10,0x20,0x9d,0xe5, 0x00,0x00,0x52,0xe3,   /* +100 */
    0x0c,0x30,0x9d,0x15, 0xe8,0xff,0xff,0x1a, 0x00,0x20,0xc4,0xe5, 0x25,0x0f,0xa0,0xe1,   /* +110 */
    0x03,0x51,0xc5,0xe3, 0x03,0x00,0x60,0xe2, 0x80,0x01,0xa0,0xe1, 0x76,0x60,0xa0,0xe1,   /* +120 */
    0x77,0x70,0xa0,0xe1, 0x30,0x76,0x85,0xe5, 0x04,0x60,0x85,0xe4, 0x26,0x00,0x00,0xea,   /* +130 */
    0x10,0x10,0x2d,0xe9, 0x1c,0xe0,0x94,0xe5, 0x20,0x10,0x94,0xe5, 0x9c,0x01,0x04,0xe0,   /* +140 */
    0xd0,0x00,0xd3,0xe1, 0xd1,0x10,0xf3,0xe1, 0x00,0x10,0x41,0xe0, 0x00,0x60,0x95,0xe5,   /* +150 */
    0x30,0x76,0x95,0xe5, 0x9e,0x01,0x09,0xe0, 0xc9,0x9b,0x80,0xe0, 0x9a,0x09,0x0c,0xe0,   /* +160 */
    0xff,0xc8,0xcc,0xe3, 0x66,0x64,0x8c,0xe0, 0x9b,0x09,0x0c,0xe0, 0xff,0xc8,0xcc,0xe3,   /* +170 */
    0x67,0x74,0x8c,0xe0, 0x04,0xe0,0x8e,0xe0, 0xae,0x9b,0xb0,0xe1, 0x07,0x00,0x00,0x0a,   /* +180 */
    0xfe,0xe5,0xce,0xe3, 0x09,0x20,0x52,0xe0, 0xce,0xff,0xff,0xda, 0x01,0x90,0x59,0xe2,   /* +190 */
    0x01,0x00,0x80,0x00, 0xd9,0x00,0xb3,0x11, 0xd1,0x10,0xf3,0xe1, 0x00,0x10,0x41,0xe0,   /* +1a0 */
    0x01,0x51,0x95,0xe2, 0xea,0xff,0xff,0x3a, 0x30,0x76,0x85,0xe5, 0x04,0x60,0x85,0xe4,   /* +1b0 */
    0x04,0x80,0x58,0xe2, 0xe4,0xff,0xff,0xca, 0x01,0x30,0x43,0xe2, 0x10,0x10,0xbd,0xe8,   /* +1c0 */
    0x1c,0xe0,0x84,0xe5, 0x18,0x20,0x84,0xe5, 0x28,0x30,0x84,0xe5, 0x00,0x80,0x9d,0xe5,   /* +1d0 */
    0x01,0x00,0x8f,0xe2, 0x10,0xff,0x2f,0xe1,   /* +1e0 */
};

#define V3_EXIT_OFF  0x1e0u

static int m4a_run_v3_stereo2(m4a_state *s, const m4a_bus *bus)
{
    const uint32_t base = s->pc;
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, ip, lr, sp;
    uint32_t n_f, z_f, c_f, v_f;
    int32_t  cyc = s->cycles;
    m4a_win  wch, wmix, wsmp, wstk;

    r0 = s->r[0];  r1 = s->r[1];  r2 = s->r[2];  r3 = s->r[3];
    r4 = s->r[4];  r5 = s->r[5];  r6 = s->r[6];  r7 = s->r[7];
    r8 = s->r[8];  r9 = s->r[9];  sl = s->r[10]; fp = s->r[11];
    ip = s->r[12]; sp = s->r[13]; lr = s->r[14];
    n_f = s->n; z_f = s->z; c_f = s->c; v_f = s->v;

    if ((r5 >> 30) != 0u)
        return M4A_DECLINED;
    if (!writable_region(r5) || !writable_region(r4) || !writable_region(sp))
        return M4A_DECLINED;

    win_init(&wch,  bus);
    win_init(&wmix, bus);
    win_init(&wsmp, bus);
    win_init(&wstk, bus);

    if (!win_hold(&wstk, sp - 8u, 40u) || !win_hold(&wch, r4, 44u) ||
        !win_hold(&wmix, r5, 0x630u + r8 + 8u) || !win_hold(&wsmp, r3, 1u))
        return M4A_DECLINED;

#define FAILED()  (wch.failed || wmix.failed || wsmp.failed || wstk.failed)

    /* 3000000: str   r8, [sp]            */ wr_u32(&wstk, sp, r8, &cyc, 0);            cyc -= 1; CHK(0x004u);
    /* 3000004: ldrb  sl, [r4, #10]       */ sl = rd_u8(&wch, r4 + 10u, &cyc);          cyc -= 1; CHK(0x008u);
    /* 3000008: ldrb  fp, [r4, #11]       */ fp = rd_u8(&wch, r4 + 11u, &cyc);          cyc -= 1; CHK(0x00cu);
    /* 300000c: lsl   sl, sl, #16         */ sl <<= 16;                                 cyc -= 1; CHK(0x010u);
    /* 3000010: lsl   fp, fp, #16         */ fp <<= 16;                                 cyc -= 1; CHK(0x014u);
    /* 3000014: ldrb  r0, [r4, #1]        */ r0 = rd_u8(&wch, r4 + 1u, &cyc);           cyc -= 1; CHK(0x018u);
    /* 3000018: tst   r0, #8              */ SET_LOGIC(r0 & 8u);                        cyc -= 1; CHK(0x01cu);
    /* 300001c: beq   L140               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x140u); goto L140; } CHK(0x020u);

L020:
    /* 3000020: cmp   r2, #4              */ { uint32_t d = r2 - 4u; SET_SUB(r2, 4u, d); } cyc -= 1; CHK(0x024u);
    /* 3000024: ble   L090                */ cyc -= 1; if (COND_LE()) { cyc -= 1; CHK(0x090u); goto L090; } CHK(0x028u);
    /* 3000028: subs  r2, r2, r8          */ { uint32_t a = r2, d = a - r8; SET_SUB(a, r8, d); r2 = d; } cyc -= 1; CHK(0x02cu);
    /* 300002c: movgt lr, #0              */ cyc -= 1; if (COND_GT()) lr = 0u;          CHK(0x030u);
    /* 3000030: bgt   L04C                */ cyc -= 1; if (COND_GT()) { cyc -= 1; CHK(0x04cu); goto L04C; } CHK(0x034u);
    /* 3000034: mov   lr, r8              */ lr = r8;                                   cyc -= 1; CHK(0x038u);
    /* 3000038: add   r2, r2, r8          */ r2 += r8;                                  cyc -= 1; CHK(0x03cu);
    /* 300003c: sub   r8, r2, #4          */ r8 = r2 - 4u;                              cyc -= 1; CHK(0x040u);
    /* 3000040: sub   lr, lr, r8          */ lr -= r8;                                  cyc -= 1; CHK(0x044u);
    /* 3000044: ands  r2, r2, #3          */ r2 &= 3u; SET_LOGIC(r2);                   cyc -= 1; CHK(0x048u);
    /* 3000048: moveq r2, #4              */ cyc -= 1; if (z_f) r2 = 4u;                CHK(0x04cu);

L04C:
    /* 300004c: ldr   r6, [r5]            */ r6 = rd_u32(&wmix, r5, &cyc, 0);           cyc -= 1; CHK(0x050u);
    /* 3000050: ldr   r7, [r5, #1584]     */ r7 = rd_u32(&wmix, r5 + 0x630u, &cyc, 0);  cyc -= 1; CHK(0x054u);
L054:
    /* 3000054: ldrsb r0, [r3], #1        */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc); r3 += 1u; cyc -= 1; CHK(0x058u);
    /* 3000058: mul   r1, sl, r0          */ r1 = sl * r0;                              cyc -= 1; CHK(0x05cu);
    /* 300005c: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x060u);
    /* 3000060: add   r6, r1, r6, ror #8  */ r6 = r1 + ror32(r6, 8);                    cyc -= 1; CHK(0x064u);
    /* 3000064: mul   r1, fp, r0          */ r1 = fp * r0;                              cyc -= 1; CHK(0x068u);
    /* 3000068: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x06cu);
    /* 300006c: add   r7, r1, r7, ror #8  */ r7 = r1 + ror32(r7, 8);                    cyc -= 1; CHK(0x070u);
    /* 3000070: adds  r5, r5, #0x40000000 */ { uint32_t a = r5, d = a + 0x40000000u; SET_ADD(a, 0x40000000u, d); r5 = d; } cyc -= 1; CHK(0x074u);
    /* 3000074: bcc   L054                */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x054u); goto L054; } CHK(0x078u);
    /* 3000078: str   r7, [r5, #1584]     */ wr_u32(&wmix, r5 + 0x630u, r7, &cyc, 0);   cyc -= 1; CHK(0x07cu);
    /* 300007c: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x080u);
    /* 3000080: subs  r8, r8, #4          */ { uint32_t a = r8, d = a - 4u; SET_SUB(a, 4u, d); r8 = d; } cyc -= 1; CHK(0x084u);
    /* 3000084: bgt   L04C                */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x04cu); goto L04C; } CHK(0x088u);
    /* 3000088: adds  r8, r8, lr          */ { uint32_t a = r8, d = a + lr; SET_ADD(a, lr, d); r8 = d; } cyc -= 1; CHK(0x08cu);
    /* 300008c: beq   L1D4                */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x1d4u); goto L1D4; } CHK(0x090u);

L090:
    /* 3000090: ldr   r6, [r5]            */ r6 = rd_u32(&wmix, r5, &cyc, 0);           cyc -= 1; CHK(0x094u);
    /* 3000094: ldr   r7, [r5, #1584]     */ r7 = rd_u32(&wmix, r5 + 0x630u, &cyc, 0);  cyc -= 1; CHK(0x098u);
L098:
    /* 3000098: ldrsb r0, [r3], #1        */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc); r3 += 1u; cyc -= 1; CHK(0x09cu);
    /* 300009c: mul   r1, sl, r0          */ r1 = sl * r0;                              cyc -= 1; CHK(0x0a0u);
    /* 30000a0: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x0a4u);
    /* 30000a4: add   r6, r1, r6, ror #8  */ r6 = r1 + ror32(r6, 8);                    cyc -= 1; CHK(0x0a8u);
    /* 30000a8: mul   r1, fp, r0          */ r1 = fp * r0;                              cyc -= 1; CHK(0x0acu);
    /* 30000ac: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x0b0u);
    /* 30000b0: add   r7, r1, r7, ror #8  */ r7 = r1 + ror32(r7, 8);                    cyc -= 1; CHK(0x0b4u);
    /* 30000b4: subs  r2, r2, #1          */ { uint32_t a = r2, d = a - 1u; SET_SUB(a, 1u, d); r2 = d; } cyc -= 1; CHK(0x0b8u);
    /* 30000b8: beq   L108                */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x108u); goto L108; } CHK(0x0bcu);
L0BC:
    /* 30000bc: adds  r5, r5, #0x40000000 */ { uint32_t a = r5, d = a + 0x40000000u; SET_ADD(a, 0x40000000u, d); r5 = d; } cyc -= 1; CHK(0x0c0u);
    /* 30000c0: bcc   L098                */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x098u); goto L098; } CHK(0x0c4u);
    /* 30000c4: str   r7, [r5, #1584]     */ wr_u32(&wmix, r5 + 0x630u, r7, &cyc, 0);   cyc -= 1; CHK(0x0c8u);
    /* 30000c8: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x0ccu);
    /* 30000cc: subs  r8, r8, #4          */ { uint32_t a = r8, d = a - 4u; SET_SUB(a, 4u, d); r8 = d; } cyc -= 1; CHK(0x0d0u);
    /* 30000d0: bgt   L020                */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x020u); goto L020; } CHK(0x0d4u);
    /* 30000d4: b     L1D4                */ cyc -= 2; CHK(0x1d4u); goto L1D4;

L0D8:
    /* 30000d8: ldr   r0, [sp, #24]       */ r0 = rd_u32(&wstk, sp + 24u, &cyc, 0);     cyc -= 1; CHK(0x0dcu);
    /* 30000dc: cmp   r0, #0              */ SET_CMP0(r0);                              cyc -= 1; CHK(0x0e0u);
    /* 30000e0: beq   L0FC                */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x0fcu); goto L0FC; } CHK(0x0e4u);
    /* 30000e4: ldr   r3, [sp, #20]       */ r3 = rd_u32(&wstk, sp + 20u, &cyc, 0);     cyc -= 1; CHK(0x0e8u);
    /* 30000e8: rsb   r9, r2, #0          */ r9 = 0u - r2;                              cyc -= 1; CHK(0x0ecu);
L0EC:
    /* 30000ec: adds  r2, r0, r2          */ { uint32_t a = r0, d = a + r2; SET_ADD(a, r2, d); r2 = d; } cyc -= 1; CHK(0x0f0u);
    /* 30000f0: bgt   L1A4                */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x1a4u); goto L1A4; } CHK(0x0f4u);
    /* 30000f4: sub   r9, r9, r0          */ r9 -= r0;                                  cyc -= 1; CHK(0x0f8u);
    /* 30000f8: b     L0EC                */ cyc -= 2; CHK(0x0ecu); goto L0EC;

L0FC:
    /* 30000fc: pop   {r4, ip}            */ r4 = rd_u32(&wstk, sp, &cyc, 1);
                                             ip = rd_u32(&wstk, sp + 4u, &cyc, 1);
                                             sp += 8u;                                  cyc -= 1; CHK(0x100u);
    /* 3000100: mov   r2, #0              */ r2 = 0u;                                   cyc -= 1; CHK(0x104u);
    /* 3000104: b     L118                */ cyc -= 2; CHK(0x118u); goto L118;

L108:
    /* 3000108: ldr   r2, [sp, #16]       */ r2 = rd_u32(&wstk, sp + 16u, &cyc, 0);     cyc -= 1; CHK(0x10cu);
    /* 300010c: cmp   r2, #0              */ SET_CMP0(r2);                              cyc -= 1; CHK(0x110u);
    /* 3000110: ldrne r3, [sp, #12]       */ cyc -= 1; if (!z_f) r3 = rd_u32(&wstk, sp + 12u, &cyc, 0); CHK(0x114u);
    /* 3000114: bne   L0BC                */ cyc -= 1; if (!z_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0bcu); goto L0BC; } CHK(0x118u);

L118:
    /* 3000118: strb  r2, [r4]            */ wr_u8(&wch, r4, (uint8_t)r2, &cyc);        cyc -= 1; CHK(0x11cu);
    /* 300011c: lsr   r0, r5, #30         */ r0 = r5 >> 30;                             cyc -= 1; CHK(0x120u);
    /* 3000120: bic   r5, r5, #0xc0000000 */ r5 &= ~0xc0000000u;                        cyc -= 1; CHK(0x124u);
    /* 3000124: rsb   r0, r0, #3          */ r0 = 3u - r0;                              cyc -= 1; CHK(0x128u);
    /* 3000128: lsl   r0, r0, #3          */ r0 <<= 3;                                  cyc -= 1; CHK(0x12cu);
    /* 300012c: ror   r6, r6, r0          */ r6 = ror32(r6, r0 & 0xffu);                cyc -= 1; CHK(0x130u);
    /* 3000130: ror   r7, r7, r0          */ r7 = ror32(r7, r0 & 0xffu);                cyc -= 1; CHK(0x134u);
    /* 3000134: str   r7, [r5, #1584]     */ wr_u32(&wmix, r5 + 0x630u, r7, &cyc, 0);   cyc -= 1; CHK(0x138u);
    /* 3000138: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x13cu);
    /* 300013c: b     L1DC                */ cyc -= 2; CHK(0x1dcu); goto L1DC;

L140:
    /* 3000140: push  {r4, ip}            */ sp -= 8u;
                                             wr_u32(&wstk, sp, r4, &cyc, 1);
                                             wr_u32(&wstk, sp + 4u, ip, &cyc, 1);       cyc -= 1; CHK(0x144u);
    /* 3000144: ldr   lr, [r4, #28]       */ lr = rd_u32(&wch, r4 + 28u, &cyc, 0);      cyc -= 1; CHK(0x148u);
    /* 3000148: ldr   r1, [r4, #32]       */ r1 = rd_u32(&wch, r4 + 32u, &cyc, 0);      cyc -= 1; CHK(0x14cu);
    /* 300014c: mul   r4, ip, r1          */ r4 = ip * r1;                              cyc -= 1; CHK(0x150u);
    /* 3000150: ldrsb r0, [r3]            */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc);     cyc -= 1; CHK(0x154u);
    /* 3000154: ldrsb r1, [r3, #1]!       */ r3 += 1u; r1 = (uint32_t)rd_s8(&wsmp, r3, &cyc); cyc -= 1; CHK(0x158u);
    /* 3000158: sub   r1, r1, r0          */ r1 -= r0;                                  cyc -= 1; CHK(0x15cu);

L15C:
    /* 300015c: ldr   r6, [r5]            */ r6 = rd_u32(&wmix, r5, &cyc, 0);           cyc -= 1; CHK(0x160u);
    /* 3000160: ldr   r7, [r5, #1584]     */ r7 = rd_u32(&wmix, r5 + 0x630u, &cyc, 0);  cyc -= 1; CHK(0x164u);
L164:
    /* 3000164: mul   r9, lr, r1          */ r9 = lr * r1;                              cyc -= 1; CHK(0x168u);
    /* 3000168: add   r9, r0, r9, asr #23 */ r9 = r0 + (uint32_t)(((int32_t)r9) >> 23); cyc -= 1; CHK(0x16cu);
    /* 300016c: mul   ip, sl, r9          */ ip = sl * r9;                              cyc -= 1; CHK(0x170u);
    /* 3000170: bic   ip, ip, #0xff0000   */ ip &= ~0x00ff0000u;
#ifdef M4A_SABOTAGE
    ip ^= 0x01000000u;   /* the RED, on the path this variant actually runs */
#endif
                                                                                        cyc -= 1; CHK(0x174u);
    /* 3000174: add   r6, ip, r6, ror #8  */ r6 = ip + ror32(r6, 8);                    cyc -= 1; CHK(0x178u);
    /* 3000178: mul   ip, fp, r9          */ ip = fp * r9;                              cyc -= 1; CHK(0x17cu);
    /* 300017c: bic   ip, ip, #0xff0000   */ ip &= ~0x00ff0000u;                        cyc -= 1; CHK(0x180u);
    /* 3000180: add   r7, ip, r7, ror #8  */ r7 = ip + ror32(r7, 8);                    cyc -= 1; CHK(0x184u);
    /* 3000184: add   lr, lr, r4          */ lr += r4;                                  cyc -= 1; CHK(0x188u);
    /* 3000188: lsrs  r9, lr, #23         */ r9 = lr >> 23; SET_LOGIC(r9); c_f = (lr >> 22) & 1u; cyc -= 1; CHK(0x18cu);
    /* 300018c: beq   L1B0                */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x1b0u); goto L1B0; } CHK(0x190u);
    /* 3000190: bic   lr, lr, #0x3f800000 */ lr &= ~0x3f800000u;                        cyc -= 1; CHK(0x194u);
    /* 3000194: subs  r2, r2, r9          */ { uint32_t a = r2, d = a - r9; SET_SUB(a, r9, d); r2 = d; } cyc -= 1; CHK(0x198u);
    /* 3000198: ble   L0D8                */ cyc -= 1; if (COND_LE()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0d8u); goto L0D8; } CHK(0x19cu);
    /* 300019c: subs  r9, r9, #1          */ { uint32_t a = r9, d = a - 1u; SET_SUB(a, 1u, d); r9 = d; } cyc -= 1; CHK(0x1a0u);
    /* 30001a0: addeq r0, r0, r1          */ cyc -= 1; if (z_f) r0 += r1;               CHK(0x1a4u);
L1A4:
    /* 30001a4: ldrsbne r0, [r3, r9]!     */ if (!z_f) { r3 += r9;
                                                 r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc); }
                                             cyc -= 1; CHK(0x1a8u);
    /* 30001a8: ldrsb r1, [r3, #1]!       */ r3 += 1u; r1 = (uint32_t)rd_s8(&wsmp, r3, &cyc); cyc -= 1; CHK(0x1acu);
    /* 30001ac: sub   r1, r1, r0          */ r1 -= r0;                                  cyc -= 1; CHK(0x1b0u);
L1B0:
    /* 30001b0: adds  r5, r5, #0x40000000 */ { uint32_t a = r5, d = a + 0x40000000u; SET_ADD(a, 0x40000000u, d); r5 = d; } cyc -= 1; CHK(0x1b4u);
    /* 30001b4: bcc   L164                */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x164u); goto L164; } CHK(0x1b8u);
    /* 30001b8: str   r7, [r5, #1584]     */ wr_u32(&wmix, r5 + 0x630u, r7, &cyc, 0);   cyc -= 1; CHK(0x1bcu);
    /* 30001bc: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x1c0u);
    /* 30001c0: subs  r8, r8, #4          */ { uint32_t a = r8, d = a - 4u; SET_SUB(a, 4u, d); r8 = d; } cyc -= 1; CHK(0x1c4u);
    /* 30001c4: bgt   L15C                */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x15cu); goto L15C; } CHK(0x1c8u);
    /* 30001c8: sub   r3, r3, #1          */ r3 -= 1u;                                  cyc -= 1; CHK(0x1ccu);
    /* 30001cc: pop   {r4, ip}            */ r4 = rd_u32(&wstk, sp, &cyc, 1);
                                             ip = rd_u32(&wstk, sp + 4u, &cyc, 1);
                                             sp += 8u;                                  cyc -= 1; CHK(0x1d0u);
    /* 30001d0: str   lr, [r4, #28]       */ wr_u32(&wch, r4 + 28u, lr, &cyc, 0);       cyc -= 1; CHK(0x1d4u);
L1D4:
    /* 30001d4: str   r2, [r4, #24]       */ wr_u32(&wch, r4 + 24u, r2, &cyc, 0);       cyc -= 1; CHK(0x1d8u);
    /* 30001d8: str   r3, [r4, #40]       */ wr_u32(&wch, r4 + 40u, r3, &cyc, 0);       cyc -= 1; CHK(0x1dcu);
L1DC:
    /* 30001dc: ldr   r8, [sp]            */ r8 = rd_u32(&wstk, sp, &cyc, 0);           cyc -= 1;

    if (FAILED())
        return M4A_DECLINED;

    SAVE_REGS();
    return M4A_DONE;

#undef FAILED
}

static const m4a_variant m4a_v3_stereo2 = {
    "m4a-soundmainram-stereo2",
    m4a_code_v3_stereo2,
    (uint32_t)sizeof m4a_code_v3_stereo2,
    V3_EXIT_OFF,
    m4a_run_v3_stereo2,
};

/* ------------------------------------------- variant 4: the same, two instructions apart */

/* The variant above, assembled by a compiler that happened to schedule two
 * independent instructions the other way round:
 *
 *      variant 3            variant 4
 *      ldrb fp, [r4,#11]    lsl  sl, sl, #16
 *      lsl  sl, sl, #16     ldrb fp, [r4,#11]
 *
 * The two do not touch each other, the block computes the same thing, and it is
 * still a different program — because the block gives way to the hardware BETWEEN
 * instructions, and at the offset between those two the machine is in one state
 * or the other. Share one transliteration between them and a checkpoint landing
 * in that gap resumes the interpreter into a register that has not been shifted
 * yet, or been shifted twice.
 *
 * 15 carts. It costs a copy and two swapped lines, and the alternative is a bug
 * that shows up as one wrong note, hours in, on fifteen games. */
static const uint8_t m4a_code_v4_stereo3[] = {
    0x00,0x80,0x8d,0xe5, 0x0a,0xa0,0xd4,0xe5, 0x0a,0xa8,0xa0,0xe1, 0x0b,0xb0,0xd4,0xe5,   /* +000 */
    0x0b,0xb8,0xa0,0xe1, 0x01,0x00,0xd4,0xe5, 0x08,0x00,0x10,0xe3, 0x47,0x00,0x00,0x0a,   /* +010 */
    0x04,0x00,0x52,0xe3, 0x19,0x00,0x00,0xda, 0x08,0x20,0x52,0xe0, 0x00,0xe0,0xa0,0xc3,   /* +020 */
    0x05,0x00,0x00,0xca, 0x08,0xe0,0xa0,0xe1, 0x08,0x20,0x82,0xe0, 0x04,0x80,0x42,0xe2,   /* +030 */
    0x08,0xe0,0x4e,0xe0, 0x03,0x20,0x12,0xe2, 0x04,0x20,0xa0,0x03, 0x00,0x60,0x95,0xe5,   /* +040 */
    0x30,0x76,0x95,0xe5, 0xd1,0x00,0xd3,0xe0, 0x9a,0x00,0x01,0xe0, 0xff,0x18,0xc1,0xe3,   /* +050 */
    0x66,0x64,0x81,0xe0, 0x9b,0x00,0x01,0xe0, 0xff,0x18,0xc1,0xe3, 0x67,0x74,0x81,0xe0,   /* +060 */
    0x01,0x51,0x95,0xe2, 0xf6,0xff,0xff,0x3a, 0x30,0x76,0x85,0xe5, 0x04,0x60,0x85,0xe4,   /* +070 */
    0x04,0x80,0x58,0xe2, 0xf0,0xff,0xff,0xca, 0x0e,0x80,0x98,0xe0, 0x50,0x00,0x00,0x0a,   /* +080 */
    0x00,0x60,0x95,0xe5, 0x30,0x76,0x95,0xe5, 0xd1,0x00,0xd3,0xe0, 0x9a,0x00,0x01,0xe0,   /* +090 */
    0xff,0x18,0xc1,0xe3, 0x66,0x64,0x81,0xe0, 0x9b,0x00,0x01,0xe0, 0xff,0x18,0xc1,0xe3,   /* +0a0 */
    0x67,0x74,0x81,0xe0, 0x01,0x20,0x52,0xe2, 0x12,0x00,0x00,0x0a, 0x01,0x51,0x95,0xe2,   /* +0b0 */
    0xf4,0xff,0xff,0x3a, 0x30,0x76,0x85,0xe5, 0x04,0x60,0x85,0xe4, 0x04,0x80,0x58,0xe2,   /* +0c0 */
    0xd2,0xff,0xff,0xca, 0x3e,0x00,0x00,0xea, 0x18,0x00,0x9d,0xe5, 0x00,0x00,0x50,0xe3,   /* +0d0 */
    0x05,0x00,0x00,0x0a, 0x14,0x30,0x9d,0xe5, 0x00,0x90,0x62,0xe2, 0x02,0x20,0x90,0xe0,   /* +0e0 */
    0x2b,0x00,0x00,0xca, 0x00,0x90,0x49,0xe0, 0xfb,0xff,0xff,0xea, 0x10,0x10,0xbd,0xe8,   /* +0f0 */
    0x00,0x20,0xa0,0xe3, 0x03,0x00,0x00,0xea, 0x10,0x20,0x9d,0xe5, 0x00,0x00,0x52,0xe3,   /* +100 */
    0x0c,0x30,0x9d,0x15, 0xe8,0xff,0xff,0x1a, 0x00,0x20,0xc4,0xe5, 0x25,0x0f,0xa0,0xe1,   /* +110 */
    0x03,0x51,0xc5,0xe3, 0x03,0x00,0x60,0xe2, 0x80,0x01,0xa0,0xe1, 0x76,0x60,0xa0,0xe1,   /* +120 */
    0x77,0x70,0xa0,0xe1, 0x30,0x76,0x85,0xe5, 0x04,0x60,0x85,0xe4, 0x26,0x00,0x00,0xea,   /* +130 */
    0x10,0x10,0x2d,0xe9, 0x1c,0xe0,0x94,0xe5, 0x20,0x10,0x94,0xe5, 0x9c,0x01,0x04,0xe0,   /* +140 */
    0xd0,0x00,0xd3,0xe1, 0xd1,0x10,0xf3,0xe1, 0x00,0x10,0x41,0xe0, 0x00,0x60,0x95,0xe5,   /* +150 */
    0x30,0x76,0x95,0xe5, 0x9e,0x01,0x09,0xe0, 0xc9,0x9b,0x80,0xe0, 0x9a,0x09,0x0c,0xe0,   /* +160 */
    0xff,0xc8,0xcc,0xe3, 0x66,0x64,0x8c,0xe0, 0x9b,0x09,0x0c,0xe0, 0xff,0xc8,0xcc,0xe3,   /* +170 */
    0x67,0x74,0x8c,0xe0, 0x04,0xe0,0x8e,0xe0, 0xae,0x9b,0xb0,0xe1, 0x07,0x00,0x00,0x0a,   /* +180 */
    0xfe,0xe5,0xce,0xe3, 0x09,0x20,0x52,0xe0, 0xce,0xff,0xff,0xda, 0x01,0x90,0x59,0xe2,   /* +190 */
    0x01,0x00,0x80,0x00, 0xd9,0x00,0xb3,0x11, 0xd1,0x10,0xf3,0xe1, 0x00,0x10,0x41,0xe0,   /* +1a0 */
    0x01,0x51,0x95,0xe2, 0xea,0xff,0xff,0x3a, 0x30,0x76,0x85,0xe5, 0x04,0x60,0x85,0xe4,   /* +1b0 */
    0x04,0x80,0x58,0xe2, 0xe4,0xff,0xff,0xca, 0x01,0x30,0x43,0xe2, 0x10,0x10,0xbd,0xe8,   /* +1c0 */
    0x1c,0xe0,0x84,0xe5, 0x18,0x20,0x84,0xe5, 0x28,0x30,0x84,0xe5, 0x00,0x80,0x9d,0xe5,   /* +1d0 */
    0x01,0x00,0x8f,0xe2, 0x10,0xff,0x2f,0xe1,   /* +1e0 */
};

#define V4_EXIT_OFF  0x1e0u

static int m4a_run_v4_stereo3(m4a_state *s, const m4a_bus *bus)
{
    const uint32_t base = s->pc;
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, ip, lr, sp;
    uint32_t n_f, z_f, c_f, v_f;
    int32_t  cyc = s->cycles;
    m4a_win  wch, wmix, wsmp, wstk;

    r0 = s->r[0];  r1 = s->r[1];  r2 = s->r[2];  r3 = s->r[3];
    r4 = s->r[4];  r5 = s->r[5];  r6 = s->r[6];  r7 = s->r[7];
    r8 = s->r[8];  r9 = s->r[9];  sl = s->r[10]; fp = s->r[11];
    ip = s->r[12]; sp = s->r[13]; lr = s->r[14];
    n_f = s->n; z_f = s->z; c_f = s->c; v_f = s->v;

    if ((r5 >> 30) != 0u)
        return M4A_DECLINED;
    if (!writable_region(r5) || !writable_region(r4) || !writable_region(sp))
        return M4A_DECLINED;

    win_init(&wch,  bus);
    win_init(&wmix, bus);
    win_init(&wsmp, bus);
    win_init(&wstk, bus);

    if (!win_hold(&wstk, sp - 8u, 40u) || !win_hold(&wch, r4, 44u) ||
        !win_hold(&wmix, r5, 0x630u + r8 + 8u) || !win_hold(&wsmp, r3, 1u))
        return M4A_DECLINED;

#define FAILED()  (wch.failed || wmix.failed || wsmp.failed || wstk.failed)

    /* 3000000: str   r8, [sp]            */ wr_u32(&wstk, sp, r8, &cyc, 0);            cyc -= 1; CHK(0x004u);
    /* 3000004: ldrb  sl, [r4, #10]       */ sl = rd_u8(&wch, r4 + 10u, &cyc);          cyc -= 1; CHK(0x008u);
    /* 3000008: lsl   sl, sl, #16         */ sl <<= 16;                                 cyc -= 1; CHK(0x00cu);
    /* 300000c: ldrb  fp, [r4, #11]       */ fp = rd_u8(&wch, r4 + 11u, &cyc);          cyc -= 1; CHK(0x010u);
    /* 3000010: lsl   fp, fp, #16         */ fp <<= 16;                                 cyc -= 1; CHK(0x014u);
    /* 3000014: ldrb  r0, [r4, #1]        */ r0 = rd_u8(&wch, r4 + 1u, &cyc);           cyc -= 1; CHK(0x018u);
    /* 3000018: tst   r0, #8              */ SET_LOGIC(r0 & 8u);                        cyc -= 1; CHK(0x01cu);
    /* 300001c: beq   L140               */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x140u); goto L140; } CHK(0x020u);

L020:
    /* 3000020: cmp   r2, #4              */ { uint32_t d = r2 - 4u; SET_SUB(r2, 4u, d); } cyc -= 1; CHK(0x024u);
    /* 3000024: ble   L090                */ cyc -= 1; if (COND_LE()) { cyc -= 1; CHK(0x090u); goto L090; } CHK(0x028u);
    /* 3000028: subs  r2, r2, r8          */ { uint32_t a = r2, d = a - r8; SET_SUB(a, r8, d); r2 = d; } cyc -= 1; CHK(0x02cu);
    /* 300002c: movgt lr, #0              */ cyc -= 1; if (COND_GT()) lr = 0u;          CHK(0x030u);
    /* 3000030: bgt   L04C                */ cyc -= 1; if (COND_GT()) { cyc -= 1; CHK(0x04cu); goto L04C; } CHK(0x034u);
    /* 3000034: mov   lr, r8              */ lr = r8;                                   cyc -= 1; CHK(0x038u);
    /* 3000038: add   r2, r2, r8          */ r2 += r8;                                  cyc -= 1; CHK(0x03cu);
    /* 300003c: sub   r8, r2, #4          */ r8 = r2 - 4u;                              cyc -= 1; CHK(0x040u);
    /* 3000040: sub   lr, lr, r8          */ lr -= r8;                                  cyc -= 1; CHK(0x044u);
    /* 3000044: ands  r2, r2, #3          */ r2 &= 3u; SET_LOGIC(r2);                   cyc -= 1; CHK(0x048u);
    /* 3000048: moveq r2, #4              */ cyc -= 1; if (z_f) r2 = 4u;                CHK(0x04cu);

L04C:
    /* 300004c: ldr   r6, [r5]            */ r6 = rd_u32(&wmix, r5, &cyc, 0);           cyc -= 1; CHK(0x050u);
    /* 3000050: ldr   r7, [r5, #1584]     */ r7 = rd_u32(&wmix, r5 + 0x630u, &cyc, 0);  cyc -= 1; CHK(0x054u);
L054:
    /* 3000054: ldrsb r0, [r3], #1        */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc); r3 += 1u; cyc -= 1; CHK(0x058u);
    /* 3000058: mul   r1, sl, r0          */ r1 = sl * r0;                              cyc -= 1; CHK(0x05cu);
    /* 300005c: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x060u);
    /* 3000060: add   r6, r1, r6, ror #8  */ r6 = r1 + ror32(r6, 8);                    cyc -= 1; CHK(0x064u);
    /* 3000064: mul   r1, fp, r0          */ r1 = fp * r0;                              cyc -= 1; CHK(0x068u);
    /* 3000068: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x06cu);
    /* 300006c: add   r7, r1, r7, ror #8  */ r7 = r1 + ror32(r7, 8);                    cyc -= 1; CHK(0x070u);
    /* 3000070: adds  r5, r5, #0x40000000 */ { uint32_t a = r5, d = a + 0x40000000u; SET_ADD(a, 0x40000000u, d); r5 = d; } cyc -= 1; CHK(0x074u);
    /* 3000074: bcc   L054                */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x054u); goto L054; } CHK(0x078u);
    /* 3000078: str   r7, [r5, #1584]     */ wr_u32(&wmix, r5 + 0x630u, r7, &cyc, 0);   cyc -= 1; CHK(0x07cu);
    /* 300007c: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x080u);
    /* 3000080: subs  r8, r8, #4          */ { uint32_t a = r8, d = a - 4u; SET_SUB(a, 4u, d); r8 = d; } cyc -= 1; CHK(0x084u);
    /* 3000084: bgt   L04C                */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x04cu); goto L04C; } CHK(0x088u);
    /* 3000088: adds  r8, r8, lr          */ { uint32_t a = r8, d = a + lr; SET_ADD(a, lr, d); r8 = d; } cyc -= 1; CHK(0x08cu);
    /* 300008c: beq   L1D4                */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x1d4u); goto L1D4; } CHK(0x090u);

L090:
    /* 3000090: ldr   r6, [r5]            */ r6 = rd_u32(&wmix, r5, &cyc, 0);           cyc -= 1; CHK(0x094u);
    /* 3000094: ldr   r7, [r5, #1584]     */ r7 = rd_u32(&wmix, r5 + 0x630u, &cyc, 0);  cyc -= 1; CHK(0x098u);
L098:
    /* 3000098: ldrsb r0, [r3], #1        */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc); r3 += 1u; cyc -= 1; CHK(0x09cu);
    /* 300009c: mul   r1, sl, r0          */ r1 = sl * r0;                              cyc -= 1; CHK(0x0a0u);
    /* 30000a0: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x0a4u);
    /* 30000a4: add   r6, r1, r6, ror #8  */ r6 = r1 + ror32(r6, 8);                    cyc -= 1; CHK(0x0a8u);
    /* 30000a8: mul   r1, fp, r0          */ r1 = fp * r0;                              cyc -= 1; CHK(0x0acu);
    /* 30000ac: bic   r1, r1, #0xff0000   */ r1 &= ~0x00ff0000u;                        cyc -= 1; CHK(0x0b0u);
    /* 30000b0: add   r7, r1, r7, ror #8  */ r7 = r1 + ror32(r7, 8);                    cyc -= 1; CHK(0x0b4u);
    /* 30000b4: subs  r2, r2, #1          */ { uint32_t a = r2, d = a - 1u; SET_SUB(a, 1u, d); r2 = d; } cyc -= 1; CHK(0x0b8u);
    /* 30000b8: beq   L108                */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x108u); goto L108; } CHK(0x0bcu);
L0BC:
    /* 30000bc: adds  r5, r5, #0x40000000 */ { uint32_t a = r5, d = a + 0x40000000u; SET_ADD(a, 0x40000000u, d); r5 = d; } cyc -= 1; CHK(0x0c0u);
    /* 30000c0: bcc   L098                */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x098u); goto L098; } CHK(0x0c4u);
    /* 30000c4: str   r7, [r5, #1584]     */ wr_u32(&wmix, r5 + 0x630u, r7, &cyc, 0);   cyc -= 1; CHK(0x0c8u);
    /* 30000c8: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x0ccu);
    /* 30000cc: subs  r8, r8, #4          */ { uint32_t a = r8, d = a - 4u; SET_SUB(a, 4u, d); r8 = d; } cyc -= 1; CHK(0x0d0u);
    /* 30000d0: bgt   L020                */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x020u); goto L020; } CHK(0x0d4u);
    /* 30000d4: b     L1D4                */ cyc -= 2; CHK(0x1d4u); goto L1D4;

L0D8:
    /* 30000d8: ldr   r0, [sp, #24]       */ r0 = rd_u32(&wstk, sp + 24u, &cyc, 0);     cyc -= 1; CHK(0x0dcu);
    /* 30000dc: cmp   r0, #0              */ SET_CMP0(r0);                              cyc -= 1; CHK(0x0e0u);
    /* 30000e0: beq   L0FC                */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x0fcu); goto L0FC; } CHK(0x0e4u);
    /* 30000e4: ldr   r3, [sp, #20]       */ r3 = rd_u32(&wstk, sp + 20u, &cyc, 0);     cyc -= 1; CHK(0x0e8u);
    /* 30000e8: rsb   r9, r2, #0          */ r9 = 0u - r2;                              cyc -= 1; CHK(0x0ecu);
L0EC:
    /* 30000ec: adds  r2, r0, r2          */ { uint32_t a = r0, d = a + r2; SET_ADD(a, r2, d); r2 = d; } cyc -= 1; CHK(0x0f0u);
    /* 30000f0: bgt   L1A4                */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x1a4u); goto L1A4; } CHK(0x0f4u);
    /* 30000f4: sub   r9, r9, r0          */ r9 -= r0;                                  cyc -= 1; CHK(0x0f8u);
    /* 30000f8: b     L0EC                */ cyc -= 2; CHK(0x0ecu); goto L0EC;

L0FC:
    /* 30000fc: pop   {r4, ip}            */ r4 = rd_u32(&wstk, sp, &cyc, 1);
                                             ip = rd_u32(&wstk, sp + 4u, &cyc, 1);
                                             sp += 8u;                                  cyc -= 1; CHK(0x100u);
    /* 3000100: mov   r2, #0              */ r2 = 0u;                                   cyc -= 1; CHK(0x104u);
    /* 3000104: b     L118                */ cyc -= 2; CHK(0x118u); goto L118;

L108:
    /* 3000108: ldr   r2, [sp, #16]       */ r2 = rd_u32(&wstk, sp + 16u, &cyc, 0);     cyc -= 1; CHK(0x10cu);
    /* 300010c: cmp   r2, #0              */ SET_CMP0(r2);                              cyc -= 1; CHK(0x110u);
    /* 3000110: ldrne r3, [sp, #12]       */ cyc -= 1; if (!z_f) r3 = rd_u32(&wstk, sp + 12u, &cyc, 0); CHK(0x114u);
    /* 3000114: bne   L0BC                */ cyc -= 1; if (!z_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0bcu); goto L0BC; } CHK(0x118u);

L118:
    /* 3000118: strb  r2, [r4]            */ wr_u8(&wch, r4, (uint8_t)r2, &cyc);        cyc -= 1; CHK(0x11cu);
    /* 300011c: lsr   r0, r5, #30         */ r0 = r5 >> 30;                             cyc -= 1; CHK(0x120u);
    /* 3000120: bic   r5, r5, #0xc0000000 */ r5 &= ~0xc0000000u;                        cyc -= 1; CHK(0x124u);
    /* 3000124: rsb   r0, r0, #3          */ r0 = 3u - r0;                              cyc -= 1; CHK(0x128u);
    /* 3000128: lsl   r0, r0, #3          */ r0 <<= 3;                                  cyc -= 1; CHK(0x12cu);
    /* 300012c: ror   r6, r6, r0          */ r6 = ror32(r6, r0 & 0xffu);                cyc -= 1; CHK(0x130u);
    /* 3000130: ror   r7, r7, r0          */ r7 = ror32(r7, r0 & 0xffu);                cyc -= 1; CHK(0x134u);
    /* 3000134: str   r7, [r5, #1584]     */ wr_u32(&wmix, r5 + 0x630u, r7, &cyc, 0);   cyc -= 1; CHK(0x138u);
    /* 3000138: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x13cu);
    /* 300013c: b     L1DC                */ cyc -= 2; CHK(0x1dcu); goto L1DC;

L140:
    /* 3000140: push  {r4, ip}            */ sp -= 8u;
                                             wr_u32(&wstk, sp, r4, &cyc, 1);
                                             wr_u32(&wstk, sp + 4u, ip, &cyc, 1);       cyc -= 1; CHK(0x144u);
    /* 3000144: ldr   lr, [r4, #28]       */ lr = rd_u32(&wch, r4 + 28u, &cyc, 0);      cyc -= 1; CHK(0x148u);
    /* 3000148: ldr   r1, [r4, #32]       */ r1 = rd_u32(&wch, r4 + 32u, &cyc, 0);      cyc -= 1; CHK(0x14cu);
    /* 300014c: mul   r4, ip, r1          */ r4 = ip * r1;                              cyc -= 1; CHK(0x150u);
    /* 3000150: ldrsb r0, [r3]            */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc);     cyc -= 1; CHK(0x154u);
    /* 3000154: ldrsb r1, [r3, #1]!       */ r3 += 1u; r1 = (uint32_t)rd_s8(&wsmp, r3, &cyc); cyc -= 1; CHK(0x158u);
    /* 3000158: sub   r1, r1, r0          */ r1 -= r0;                                  cyc -= 1; CHK(0x15cu);

L15C:
    /* 300015c: ldr   r6, [r5]            */ r6 = rd_u32(&wmix, r5, &cyc, 0);           cyc -= 1; CHK(0x160u);
    /* 3000160: ldr   r7, [r5, #1584]     */ r7 = rd_u32(&wmix, r5 + 0x630u, &cyc, 0);  cyc -= 1; CHK(0x164u);
L164:
    /* 3000164: mul   r9, lr, r1          */ r9 = lr * r1;                              cyc -= 1; CHK(0x168u);
    /* 3000168: add   r9, r0, r9, asr #23 */ r9 = r0 + (uint32_t)(((int32_t)r9) >> 23); cyc -= 1; CHK(0x16cu);
    /* 300016c: mul   ip, sl, r9          */ ip = sl * r9;                              cyc -= 1; CHK(0x170u);
    /* 3000170: bic   ip, ip, #0xff0000   */ ip &= ~0x00ff0000u;
#ifdef M4A_SABOTAGE
    ip ^= 0x01000000u;   /* the RED, on the path this variant actually runs */
#endif
                                                                                        cyc -= 1; CHK(0x174u);
    /* 3000174: add   r6, ip, r6, ror #8  */ r6 = ip + ror32(r6, 8);                    cyc -= 1; CHK(0x178u);
    /* 3000178: mul   ip, fp, r9          */ ip = fp * r9;                              cyc -= 1; CHK(0x17cu);
    /* 300017c: bic   ip, ip, #0xff0000   */ ip &= ~0x00ff0000u;                        cyc -= 1; CHK(0x180u);
    /* 3000180: add   r7, ip, r7, ror #8  */ r7 = ip + ror32(r7, 8);                    cyc -= 1; CHK(0x184u);
    /* 3000184: add   lr, lr, r4          */ lr += r4;                                  cyc -= 1; CHK(0x188u);
    /* 3000188: lsrs  r9, lr, #23         */ r9 = lr >> 23; SET_LOGIC(r9); c_f = (lr >> 22) & 1u; cyc -= 1; CHK(0x18cu);
    /* 300018c: beq   L1B0                */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x1b0u); goto L1B0; } CHK(0x190u);
    /* 3000190: bic   lr, lr, #0x3f800000 */ lr &= ~0x3f800000u;                        cyc -= 1; CHK(0x194u);
    /* 3000194: subs  r2, r2, r9          */ { uint32_t a = r2, d = a - r9; SET_SUB(a, r9, d); r2 = d; } cyc -= 1; CHK(0x198u);
    /* 3000198: ble   L0D8                */ cyc -= 1; if (COND_LE()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0d8u); goto L0D8; } CHK(0x19cu);
    /* 300019c: subs  r9, r9, #1          */ { uint32_t a = r9, d = a - 1u; SET_SUB(a, 1u, d); r9 = d; } cyc -= 1; CHK(0x1a0u);
    /* 30001a0: addeq r0, r0, r1          */ cyc -= 1; if (z_f) r0 += r1;               CHK(0x1a4u);
L1A4:
    /* 30001a4: ldrsbne r0, [r3, r9]!     */ if (!z_f) { r3 += r9;
                                                 r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc); }
                                             cyc -= 1; CHK(0x1a8u);
    /* 30001a8: ldrsb r1, [r3, #1]!       */ r3 += 1u; r1 = (uint32_t)rd_s8(&wsmp, r3, &cyc); cyc -= 1; CHK(0x1acu);
    /* 30001ac: sub   r1, r1, r0          */ r1 -= r0;                                  cyc -= 1; CHK(0x1b0u);
L1B0:
    /* 30001b0: adds  r5, r5, #0x40000000 */ { uint32_t a = r5, d = a + 0x40000000u; SET_ADD(a, 0x40000000u, d); r5 = d; } cyc -= 1; CHK(0x1b4u);
    /* 30001b4: bcc   L164                */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x164u); goto L164; } CHK(0x1b8u);
    /* 30001b8: str   r7, [r5, #1584]     */ wr_u32(&wmix, r5 + 0x630u, r7, &cyc, 0);   cyc -= 1; CHK(0x1bcu);
    /* 30001bc: str   r6, [r5], #4        */ wr_u32(&wmix, r5, r6, &cyc, 0); r5 += 4u;  cyc -= 1; CHK(0x1c0u);
    /* 30001c0: subs  r8, r8, #4          */ { uint32_t a = r8, d = a - 4u; SET_SUB(a, 4u, d); r8 = d; } cyc -= 1; CHK(0x1c4u);
    /* 30001c4: bgt   L15C                */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x15cu); goto L15C; } CHK(0x1c8u);
    /* 30001c8: sub   r3, r3, #1          */ r3 -= 1u;                                  cyc -= 1; CHK(0x1ccu);
    /* 30001cc: pop   {r4, ip}            */ r4 = rd_u32(&wstk, sp, &cyc, 1);
                                             ip = rd_u32(&wstk, sp + 4u, &cyc, 1);
                                             sp += 8u;                                  cyc -= 1; CHK(0x1d0u);
    /* 30001d0: str   lr, [r4, #28]       */ wr_u32(&wch, r4 + 28u, lr, &cyc, 0);       cyc -= 1; CHK(0x1d4u);
L1D4:
    /* 30001d4: str   r2, [r4, #24]       */ wr_u32(&wch, r4 + 24u, r2, &cyc, 0);       cyc -= 1; CHK(0x1d8u);
    /* 30001d8: str   r3, [r4, #40]       */ wr_u32(&wch, r4 + 40u, r3, &cyc, 0);       cyc -= 1; CHK(0x1dcu);
L1DC:
    /* 30001dc: ldr   r8, [sp]            */ r8 = rd_u32(&wstk, sp, &cyc, 0);           cyc -= 1;

    if (FAILED())
        return M4A_DECLINED;

    SAVE_REGS();
    return M4A_DONE;

#undef FAILED
}

static const m4a_variant m4a_v4_stereo3 = {
    "m4a-soundmainram-stereo3",
    m4a_code_v4_stereo3,
    (uint32_t)sizeof m4a_code_v4_stereo3,
    V4_EXIT_OFF,
    m4a_run_v4_stereo3,
};


/* ---------------------------------- variants 5 and 6: the byte-wise mixer */

/* A different mixer, not a rearrangement of the one above.
 *
 * Where the others pack four output samples into the byte lanes of a 32-bit word
 * (the `adds r5, #0x40000000` counter and the `ror #8` accumulate), this one just
 * reads a byte, adds to it, and writes it back — `ldrb / add r0, r0, r1, asr #8 /
 * strb`. So `r5` is a plain byte pointer with no counter hidden in its top bits,
 * and there is no partial-word flush to get right.
 *
 * The resampling is different too, and cheaper: instead of one multiply per output
 * sample it walks the phase accumulator forward in strides of four, then two, then
 * one (`cmp r7, r9, lsl #2` / `lsl #1` / plain), skipping whole input samples when
 * the pitch is high. `r9` carries the unit and `ip` the interpolation scale, and
 * both arrive in registers from the Thumb caller.
 *
 * Variant 6 is variant 5 with the right channel deleted — same shape, one
 * accumulator. It is one cart (Mr. Driller 2) and it is here because it cost the
 * copy, and because "one cart" is a fact from the census rather than a guess.
 */
static const uint8_t m4a_code_v5_bytes[] = {
    0x00,0x80,0x8d,0xe5, 0x0a,0xa0,0xd4,0xe5, 0x0b,0xb0,0xd4,0xe5, 0x01,0x00,0xd4,0xe5,   /* +000 */
    0x08,0x00,0x10,0xe3, 0x13,0x00,0x00,0x0a, 0xd1,0x60,0xd3,0xe0, 0x96,0x0b,0x01,0xe0,   /* +010 */
    0x30,0x06,0xd5,0xe5, 0x41,0x04,0x80,0xe0, 0x30,0x06,0xc5,0xe5, 0x96,0x0a,0x01,0xe0,   /* +020 */
    0x00,0x00,0xd5,0xe5, 0x41,0x04,0x80,0xe0, 0x01,0x00,0xc5,0xe4, 0x01,0x20,0x52,0xe2,   /* +030 */
    0x05,0x00,0x00,0x1a, 0x10,0x20,0x9d,0xe5, 0x00,0x00,0x52,0xe3, 0x0c,0x30,0x9d,0x15,   /* +040 */
    0x01,0x00,0x00,0x1a, 0x00,0x20,0xc4,0xe5, 0x39,0x00,0x00,0xea, 0x01,0x80,0x58,0xe2,   /* +050 */
    0xec,0xff,0xff,0xca, 0x34,0x00,0x00,0xea, 0x1c,0x70,0x94,0xe5, 0x20,0xe0,0x94,0xe5,   /* +060 */
    0x09,0x01,0x57,0xe1, 0x06,0x00,0x00,0x3a, 0x04,0x00,0x52,0xe3, 0x0d,0x00,0x00,0xda,   /* +070 */
    0x04,0x20,0x42,0xe2, 0x04,0x30,0x83,0xe2, 0x09,0x71,0x47,0xe0, 0x09,0x01,0x57,0xe1,   /* +080 */
    0xf8,0xff,0xff,0x2a, 0x89,0x00,0x57,0xe1, 0x04,0x00,0x00,0x3a, 0x02,0x00,0x52,0xe3,   /* +090 */
    0x04,0x00,0x00,0xda, 0x02,0x20,0x42,0xe2, 0x02,0x30,0x83,0xe2, 0x89,0x70,0x47,0xe0,   /* +0a0 */
    0x09,0x00,0x57,0xe1, 0x0b,0x00,0x00,0x3a, 0x01,0x20,0x52,0xe2, 0x05,0x00,0x00,0x1a,   /* +0b0 */
    0x10,0x20,0x9d,0xe5, 0x00,0x00,0x52,0xe3, 0x0c,0x30,0x9d,0x15, 0x02,0x00,0x00,0x1a,   /* +0c0 */
    0x00,0x20,0xc4,0xe5, 0x1a,0x00,0x00,0xea, 0x01,0x30,0x83,0xe2, 0x09,0x70,0x47,0xe0,   /* +0d0 */
    0x09,0x00,0x57,0xe1, 0xf3,0xff,0xff,0x2a, 0xd0,0x00,0xd3,0xe1, 0xd1,0x10,0xd3,0xe1,   /* +0e0 */
    0x00,0x10,0x41,0xe0, 0x91,0x07,0x06,0xe0, 0x96,0x0c,0x01,0xe0, 0xc1,0x6b,0x80,0xe0,   /* +0f0 */
    0x96,0x0b,0x01,0xe0, 0x30,0x06,0xd5,0xe5, 0x41,0x04,0x80,0xe0, 0x30,0x06,0xc5,0xe5,   /* +100 */
    0x96,0x0a,0x01,0xe0, 0x00,0x00,0xd5,0xe5, 0x41,0x04,0x80,0xe0, 0x01,0x00,0xc5,0xe4,   /* +110 */
    0x0e,0x70,0x87,0xe0, 0x01,0x80,0x58,0xe2, 0x02,0x00,0x00,0x0a, 0x09,0x00,0x57,0xe1,   /* +120 */
    0xec,0xff,0xff,0x3a, 0xcd,0xff,0xff,0xea, 0x1c,0x70,0x84,0xe5, 0x18,0x20,0x84,0xe5,   /* +130 */
    0x28,0x30,0x84,0xe5, 0x00,0x80,0x9d,0xe5, 0x01,0x00,0x8f,0xe2, 0x10,0xff,0x2f,0xe1,   /* +140 */
};
#define V5_EXIT_OFF  0x148u

static int m4a_run_v5_bytes(m4a_state *s, const m4a_bus *bus)
{
    const uint32_t base = s->pc;
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, ip, lr, sp;
    uint32_t n_f, z_f, c_f, v_f;
    int32_t  cyc = s->cycles;
    m4a_win  wch, wmix, wsmp, wstk;

    r0 = s->r[0];  r1 = s->r[1];  r2 = s->r[2];  r3 = s->r[3];
    r4 = s->r[4];  r5 = s->r[5];  r6 = s->r[6];  r7 = s->r[7];
    r8 = s->r[8];  r9 = s->r[9];  sl = s->r[10]; fp = s->r[11];
    ip = s->r[12]; sp = s->r[13]; lr = s->r[14];
    n_f = s->n; z_f = s->z; c_f = s->c; v_f = s->v;

    if (!writable_region(r5) || !writable_region(r4) || !writable_region(sp))
        return M4A_DECLINED;

    win_init(&wch,  bus);
    win_init(&wmix, bus);
    win_init(&wsmp, bus);
    win_init(&wstk, bus);

    /* No push here — this build keeps everything in registers — so the stack window
     * is only the three words it reads: [sp], [sp,#12], [sp,#16]. And the mix
     * pointer is a BYTE pointer, so it advances r8 bytes, not r8/4 words. */
    if (!win_hold(&wstk, sp, 20u) || !win_hold(&wch, r4, 44u) ||
        !win_hold(&wmix, r5, 0x630u + r8 + 4u) || !win_hold(&wsmp, r3, 2u))
        return M4A_DECLINED;

#define FAILED()  (wch.failed || wmix.failed || wsmp.failed || wstk.failed)

    /* 3000000: str   r8, [sp]            */ wr_u32(&wstk, sp, r8, &cyc, 0);            cyc -= 1; CHK(0x004u);
    /* 3000004: ldrb  sl, [r4, #10]       */ sl = rd_u8(&wch, r4 + 10u, &cyc);          cyc -= 1; CHK(0x008u);
    /* 3000008: ldrb  fp, [r4, #11]       */ fp = rd_u8(&wch, r4 + 11u, &cyc);          cyc -= 1; CHK(0x00cu);
    /* 300000c: ldrb  r0, [r4, #1]        */ r0 = rd_u8(&wch, r4 + 1u, &cyc);           cyc -= 1; CHK(0x010u);
    /* 3000010: tst   r0, #8              */ SET_LOGIC(r0 & 8u);                        cyc -= 1; CHK(0x014u);
    /* 3000014: beq   L068                */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x068u); goto L068; } CHK(0x018u);

L018:
    /* 3000018: ldrsb r6, [r3], #1        */ r6 = (uint32_t)rd_s8(&wsmp, r3, &cyc); r3 += 1u; cyc -= 1; CHK(0x01cu);
    /* 300001c: mul   r1, r6, fp          */ r1 = r6 * fp;                              cyc -= 1; CHK(0x020u);
    /* 3000020: ldrb  r0, [r5, #1584]     */ r0 = rd_u8(&wmix, r5 + 0x630u, &cyc);      cyc -= 1; CHK(0x024u);
    /* 3000024: add   r0, r0, r1, asr #8  */ r0 += (uint32_t)(((int32_t)r1) >> 8);      cyc -= 1; CHK(0x028u);
    /* 3000028: strb  r0, [r5, #1584]     */ wr_u8(&wmix, r5 + 0x630u, (uint8_t)r0, &cyc); cyc -= 1; CHK(0x02cu);
    /* 300002c: mul   r1, r6, sl          */ r1 = r6 * sl;                              cyc -= 1; CHK(0x030u);
    /* 3000030: ldrb  r0, [r5]            */ r0 = rd_u8(&wmix, r5, &cyc);               cyc -= 1; CHK(0x034u);
    /* 3000034: add   r0, r0, r1, asr #8  */ r0 += (uint32_t)(((int32_t)r1) >> 8);      cyc -= 1; CHK(0x038u);
    /* 3000038: strb  r0, [r5], #1        */ wr_u8(&wmix, r5, (uint8_t)r0, &cyc); r5 += 1u; cyc -= 1; CHK(0x03cu);
    /* 300003c: subs  r2, r2, #1          */ { uint32_t a = r2, d = a - 1u; SET_SUB(a, 1u, d); r2 = d; } cyc -= 1; CHK(0x040u);
    /* 3000040: bne   L05C                */ cyc -= 1; if (!z_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x05cu); goto L05C; } CHK(0x044u);
    /* 3000044: ldr   r2, [sp, #16]       */ r2 = rd_u32(&wstk, sp + 16u, &cyc, 0);     cyc -= 1; CHK(0x048u);
    /* 3000048: cmp   r2, #0              */ SET_CMP0(r2);                              cyc -= 1; CHK(0x04cu);
    /* 300004c: ldrne r3, [sp, #12]       */ cyc -= 1; if (!z_f) r3 = rd_u32(&wstk, sp + 12u, &cyc, 0); CHK(0x050u);
    /* 3000050: bne   L05C                */ cyc -= 1; if (!z_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x05cu); goto L05C; } CHK(0x054u);
    /* 3000054: strb  r2, [r4]            */ wr_u8(&wch, r4, (uint8_t)r2, &cyc);        cyc -= 1; CHK(0x058u);
    /* 3000058: b     L144                */ cyc -= 2; CHK(0x144u); goto L144;
L05C:
    /* 300005c: subs  r8, r8, #1          */ { uint32_t a = r8, d = a - 1u; SET_SUB(a, 1u, d); r8 = d; } cyc -= 1; CHK(0x060u);
    /* 3000060: bgt   L018                */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x018u); goto L018; } CHK(0x064u);
    /* 3000064: b     L13C                */ cyc -= 2; CHK(0x13cu); goto L13C;

L068:
    /* 3000068: ldr   r7, [r4, #28]       */ r7 = rd_u32(&wch, r4 + 28u, &cyc, 0);      cyc -= 1; CHK(0x06cu);
    /* 300006c: ldr   lr, [r4, #32]       */ lr = rd_u32(&wch, r4 + 32u, &cyc, 0);      cyc -= 1; CHK(0x070u);
L070:
    /* 3000070: cmp   r7, r9, lsl #2      */ { uint32_t b = r9 << 2, d = r7 - b; SET_SUB(r7, b, d); } cyc -= 1; CHK(0x074u);
    /* 3000074: bcc   L094                */ cyc -= 1; if (!c_f) { cyc -= 1; CHK(0x094u); goto L094; } CHK(0x078u);
L078:
    /* 3000078: cmp   r2, #4              */ { uint32_t d = r2 - 4u; SET_SUB(r2, 4u, d); } cyc -= 1; CHK(0x07cu);
    /* 300007c: ble   L0B8                */ cyc -= 1; if (COND_LE()) { cyc -= 1; CHK(0x0b8u); goto L0B8; } CHK(0x080u);
    /* 3000080: sub   r2, r2, #4          */ r2 -= 4u;                                  cyc -= 1; CHK(0x084u);
    /* 3000084: add   r3, r3, #4          */ r3 += 4u;                                  cyc -= 1; CHK(0x088u);
    /* 3000088: sub   r7, r7, r9, lsl #2  */ r7 -= (r9 << 2);                           cyc -= 1; CHK(0x08cu);
    /* 300008c: cmp   r7, r9, lsl #2      */ { uint32_t b = r9 << 2, d = r7 - b; SET_SUB(r7, b, d); } cyc -= 1; CHK(0x090u);
    /* 3000090: bcs   L078                */ cyc -= 1; if (c_f) { cyc -= 1; CHK(0x078u); goto L078; } CHK(0x094u);
L094:
    /* 3000094: cmp   r7, r9, lsl #1      */ { uint32_t b = r9 << 1, d = r7 - b; SET_SUB(r7, b, d); } cyc -= 1; CHK(0x098u);
    /* 3000098: bcc   L0B0                */ cyc -= 1; if (!c_f) { cyc -= 1; CHK(0x0b0u); goto L0B0; } CHK(0x09cu);
    /* 300009c: cmp   r2, #2              */ { uint32_t d = r2 - 2u; SET_SUB(r2, 2u, d); } cyc -= 1; CHK(0x0a0u);
    /* 30000a0: ble   L0B8                */ cyc -= 1; if (COND_LE()) { cyc -= 1; CHK(0x0b8u); goto L0B8; } CHK(0x0a4u);
    /* 30000a4: sub   r2, r2, #2          */ r2 -= 2u;                                  cyc -= 1; CHK(0x0a8u);
    /* 30000a8: add   r3, r3, #2          */ r3 += 2u;                                  cyc -= 1; CHK(0x0acu);
    /* 30000ac: sub   r7, r7, r9, lsl #1  */ r7 -= (r9 << 1);                           cyc -= 1; CHK(0x0b0u);
L0B0:
    /* 30000b0: cmp   r7, r9              */ { uint32_t d = r7 - r9; SET_SUB(r7, r9, d); } cyc -= 1; CHK(0x0b4u);
    /* 30000b4: bcc   L0E8                */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0e8u); goto L0E8; } CHK(0x0b8u);
L0B8:
    /* 30000b8: subs  r2, r2, #1          */ { uint32_t a = r2, d = a - 1u; SET_SUB(a, 1u, d); r2 = d; } cyc -= 1; CHK(0x0bcu);
    /* 30000bc: bne   L0D8                */ cyc -= 1; if (!z_f) { cyc -= 1; CHK(0x0d8u); goto L0D8; } CHK(0x0c0u);
    /* 30000c0: ldr   r2, [sp, #16]       */ r2 = rd_u32(&wstk, sp + 16u, &cyc, 0);     cyc -= 1; CHK(0x0c4u);
    /* 30000c4: cmp   r2, #0              */ SET_CMP0(r2);                              cyc -= 1; CHK(0x0c8u);
    /* 30000c8: ldrne r3, [sp, #12]       */ cyc -= 1; if (!z_f) r3 = rd_u32(&wstk, sp + 12u, &cyc, 0); CHK(0x0ccu);
    /* 30000cc: bne   L0DC                */ cyc -= 1; if (!z_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0dcu); goto L0DC; } CHK(0x0d0u);
    /* 30000d0: strb  r2, [r4]            */ wr_u8(&wch, r4, (uint8_t)r2, &cyc);        cyc -= 1; CHK(0x0d4u);
    /* 30000d4: b     L144                */ cyc -= 2; CHK(0x144u); goto L144;
L0D8:
    /* 30000d8: add   r3, r3, #1          */ r3 += 1u;                                  cyc -= 1; CHK(0x0dcu);
L0DC:
    /* 30000dc: sub   r7, r7, r9          */ r7 -= r9;                                  cyc -= 1; CHK(0x0e0u);
    /* 30000e0: cmp   r7, r9              */ { uint32_t d = r7 - r9; SET_SUB(r7, r9, d); } cyc -= 1; CHK(0x0e4u);
    /* 30000e4: bcs   L0B8                */ cyc -= 1; if (c_f) { cyc -= 1; CHK(0x0b8u); goto L0B8; } CHK(0x0e8u);
L0E8:
    /* 30000e8: ldrsb r0, [r3]            */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc);     cyc -= 1; CHK(0x0ecu);
    /* 30000ec: ldrsb r1, [r3, #1]        */ r1 = (uint32_t)rd_s8(&wsmp, r3 + 1u, &cyc); cyc -= 1; CHK(0x0f0u);
    /* 30000f0: sub   r1, r1, r0          */ r1 -= r0;                                  cyc -= 1; CHK(0x0f4u);
    /* 30000f4: mul   r6, r1, r7          */ r6 = r1 * r7;                              cyc -= 1; CHK(0x0f8u);
    /* 30000f8: mul   r1, r6, ip          */ r1 = r6 * ip;                              cyc -= 1; CHK(0x0fcu);
    /* 30000fc: add   r6, r0, r1, asr #23 */ r6 = r0 + (uint32_t)(((int32_t)r1) >> 23); cyc -= 1; CHK(0x100u);
    /* 3000100: mul   r1, r6, fp          */ r1 = r6 * fp;
#ifdef M4A_SABOTAGE
    r1 ^= 0x00000100u;   /* the RED, on the path this variant actually runs */
#endif
                                                                                        cyc -= 1; CHK(0x104u);
    /* 3000104: ldrb  r0, [r5, #1584]     */ r0 = rd_u8(&wmix, r5 + 0x630u, &cyc);      cyc -= 1; CHK(0x108u);
    /* 3000108: add   r0, r0, r1, asr #8  */ r0 += (uint32_t)(((int32_t)r1) >> 8);      cyc -= 1; CHK(0x10cu);
    /* 300010c: strb  r0, [r5, #1584]     */ wr_u8(&wmix, r5 + 0x630u, (uint8_t)r0, &cyc); cyc -= 1; CHK(0x110u);
    /* 3000110: mul   r1, r6, sl          */ r1 = r6 * sl;                              cyc -= 1; CHK(0x114u);
    /* 3000114: ldrb  r0, [r5]            */ r0 = rd_u8(&wmix, r5, &cyc);               cyc -= 1; CHK(0x118u);
    /* 3000118: add   r0, r0, r1, asr #8  */ r0 += (uint32_t)(((int32_t)r1) >> 8);      cyc -= 1; CHK(0x11cu);
    /* 300011c: strb  r0, [r5], #1        */ wr_u8(&wmix, r5, (uint8_t)r0, &cyc); r5 += 1u; cyc -= 1; CHK(0x120u);
    /* 3000120: add   r7, r7, lr          */ r7 += lr;                                  cyc -= 1; CHK(0x124u);
    /* 3000124: subs  r8, r8, #1          */ { uint32_t a = r8, d = a - 1u; SET_SUB(a, 1u, d); r8 = d; } cyc -= 1; CHK(0x128u);
    /* 3000128: beq   L138                */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x138u); goto L138; } CHK(0x12cu);
    /* 300012c: cmp   r7, r9              */ { uint32_t d = r7 - r9; SET_SUB(r7, r9, d); } cyc -= 1; CHK(0x130u);
    /* 3000130: bcc   L0E8                */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0e8u); goto L0E8; } CHK(0x134u);
    /* 3000134: b     L070                */ cyc -= 2; CHK(0x070u); goto L070;
L138:
    /* 3000138: str   r7, [r4, #28]       */ wr_u32(&wch, r4 + 28u, r7, &cyc, 0);       cyc -= 1; CHK(0x13cu);
L13C:
    /* 300013c: str   r2, [r4, #24]       */ wr_u32(&wch, r4 + 24u, r2, &cyc, 0);       cyc -= 1; CHK(0x140u);
    /* 3000140: str   r3, [r4, #40]       */ wr_u32(&wch, r4 + 40u, r3, &cyc, 0);       cyc -= 1; CHK(0x144u);
L144:
    /* 3000144: ldr   r8, [sp]            */ r8 = rd_u32(&wstk, sp, &cyc, 0);           cyc -= 1;

    if (FAILED())
        return M4A_DECLINED;

    SAVE_REGS();
    return M4A_DONE;

#undef FAILED
}

static const m4a_variant m4a_v5_bytes = {
    "m4a-soundmainram-bytes",
    m4a_code_v5_bytes,
    (uint32_t)sizeof m4a_code_v5_bytes,
    V5_EXIT_OFF,
    m4a_run_v5_bytes,
};

/* ---- variant 6: the same, with no right channel ---- */

static const uint8_t m4a_code_v6_bytes_mono[] = {
    0x00,0x80,0x8d,0xe5, 0x0a,0xa0,0xd4,0xe5, 0x01,0x00,0xd4,0xe5, 0x08,0x00,0x10,0xe3,   /* +000 */
    0x0f,0x00,0x00,0x0a, 0xd1,0x60,0xd3,0xe0, 0x96,0x0a,0x01,0xe0, 0x00,0x00,0xd5,0xe5,   /* +010 */
    0x41,0x04,0x80,0xe0, 0x01,0x00,0xc5,0xe4, 0x01,0x20,0x52,0xe2, 0x05,0x00,0x00,0x1a,   /* +020 */
    0x10,0x20,0x9d,0xe5, 0x00,0x00,0x52,0xe3, 0x0c,0x30,0x9d,0x15, 0x01,0x00,0x00,0x1a,   /* +030 */
    0x00,0x20,0xc4,0xe5, 0x35,0x00,0x00,0xea, 0x01,0x80,0x58,0xe2, 0xf0,0xff,0xff,0xca,   /* +040 */
    0x30,0x00,0x00,0xea, 0x1c,0x70,0x94,0xe5, 0x20,0xe0,0x94,0xe5, 0x09,0x01,0x57,0xe1,   /* +050 */
    0x06,0x00,0x00,0x3a, 0x04,0x00,0x52,0xe3, 0x0d,0x00,0x00,0xda, 0x04,0x20,0x42,0xe2,   /* +060 */
    0x04,0x30,0x83,0xe2, 0x09,0x71,0x47,0xe0, 0x09,0x01,0x57,0xe1, 0xf8,0xff,0xff,0x2a,   /* +070 */
    0x89,0x00,0x57,0xe1, 0x04,0x00,0x00,0x3a, 0x02,0x00,0x52,0xe3, 0x04,0x00,0x00,0xda,   /* +080 */
    0x02,0x20,0x42,0xe2, 0x02,0x30,0x83,0xe2, 0x89,0x70,0x47,0xe0, 0x09,0x00,0x57,0xe1,   /* +090 */
    0x0b,0x00,0x00,0x3a, 0x01,0x20,0x52,0xe2, 0x05,0x00,0x00,0x1a, 0x10,0x20,0x9d,0xe5,   /* +0a0 */
    0x00,0x00,0x52,0xe3, 0x0c,0x30,0x9d,0x15, 0x02,0x00,0x00,0x1a, 0x00,0x20,0xc4,0xe5,   /* +0b0 */
    0x16,0x00,0x00,0xea, 0x01,0x30,0x83,0xe2, 0x09,0x70,0x47,0xe0, 0x09,0x00,0x57,0xe1,   /* +0c0 */
    0xf3,0xff,0xff,0x2a, 0xd0,0x00,0xd3,0xe1, 0xd1,0x10,0xd3,0xe1, 0x00,0x10,0x41,0xe0,   /* +0d0 */
    0x91,0x07,0x06,0xe0, 0x96,0x0c,0x01,0xe0, 0xc1,0x6b,0x80,0xe0, 0x96,0x0a,0x01,0xe0,   /* +0e0 */
    0x00,0x00,0xd5,0xe5, 0x41,0x04,0x80,0xe0, 0x01,0x00,0xc5,0xe4, 0x0e,0x70,0x87,0xe0,   /* +0f0 */
    0x01,0x80,0x58,0xe2, 0x02,0x00,0x00,0x0a, 0x09,0x00,0x57,0xe1, 0xf0,0xff,0xff,0x3a,   /* +100 */
    0xd1,0xff,0xff,0xea, 0x1c,0x70,0x84,0xe5, 0x18,0x20,0x84,0xe5, 0x28,0x30,0x84,0xe5,   /* +110 */
    0x00,0x80,0x9d,0xe5, 0x01,0x00,0x8f,0xe2, 0x10,0xff,0x2f,0xe1,   /* +120 */
};
#define V6_EXIT_OFF  0x124u

static int m4a_run_v6_bytes_mono(m4a_state *s, const m4a_bus *bus)
{
    const uint32_t base = s->pc;
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, ip, lr, sp;
    uint32_t n_f, z_f, c_f, v_f;
    int32_t  cyc = s->cycles;
    m4a_win  wch, wmix, wsmp, wstk;

    r0 = s->r[0];  r1 = s->r[1];  r2 = s->r[2];  r3 = s->r[3];
    r4 = s->r[4];  r5 = s->r[5];  r6 = s->r[6];  r7 = s->r[7];
    r8 = s->r[8];  r9 = s->r[9];  sl = s->r[10]; fp = s->r[11];
    ip = s->r[12]; sp = s->r[13]; lr = s->r[14];
    n_f = s->n; z_f = s->z; c_f = s->c; v_f = s->v;

    if (!writable_region(r5) || !writable_region(r4) || !writable_region(sp))
        return M4A_DECLINED;

    win_init(&wch,  bus);
    win_init(&wmix, bus);
    win_init(&wsmp, bus);
    win_init(&wstk, bus);

    if (!win_hold(&wstk, sp, 20u) || !win_hold(&wch, r4, 44u) ||
        !win_hold(&wmix, r5, r8 + 4u) || !win_hold(&wsmp, r3, 2u))
        return M4A_DECLINED;

#define FAILED()  (wch.failed || wmix.failed || wsmp.failed || wstk.failed)

    /* 3000000: str   r8, [sp]            */ wr_u32(&wstk, sp, r8, &cyc, 0);            cyc -= 1; CHK(0x004u);
    /* 3000004: ldrb  sl, [r4, #10]       */ sl = rd_u8(&wch, r4 + 10u, &cyc);          cyc -= 1; CHK(0x008u);
    /* 3000008: ldrb  r0, [r4, #1]        */ r0 = rd_u8(&wch, r4 + 1u, &cyc);           cyc -= 1; CHK(0x00cu);
    /* 300000c: tst   r0, #8              */ SET_LOGIC(r0 & 8u);                        cyc -= 1; CHK(0x010u);
    /* 3000010: beq   L054                */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x054u); goto L054; } CHK(0x014u);

L014:
    /* 3000014: ldrsb r6, [r3], #1        */ r6 = (uint32_t)rd_s8(&wsmp, r3, &cyc); r3 += 1u; cyc -= 1; CHK(0x018u);
    /* 3000018: mul   r1, r6, sl          */ r1 = r6 * sl;                              cyc -= 1; CHK(0x01cu);
    /* 300001c: ldrb  r0, [r5]            */ r0 = rd_u8(&wmix, r5, &cyc);               cyc -= 1; CHK(0x020u);
    /* 3000020: add   r0, r0, r1, asr #8  */ r0 += (uint32_t)(((int32_t)r1) >> 8);      cyc -= 1; CHK(0x024u);
    /* 3000024: strb  r0, [r5], #1        */ wr_u8(&wmix, r5, (uint8_t)r0, &cyc); r5 += 1u; cyc -= 1; CHK(0x028u);
    /* 3000028: subs  r2, r2, #1          */ { uint32_t a = r2, d = a - 1u; SET_SUB(a, 1u, d); r2 = d; } cyc -= 1; CHK(0x02cu);
    /* 300002c: bne   L048                */ cyc -= 1; if (!z_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x048u); goto L048; } CHK(0x030u);
    /* 3000030: ldr   r2, [sp, #16]       */ r2 = rd_u32(&wstk, sp + 16u, &cyc, 0);     cyc -= 1; CHK(0x034u);
    /* 3000034: cmp   r2, #0              */ SET_CMP0(r2);                              cyc -= 1; CHK(0x038u);
    /* 3000038: ldrne r3, [sp, #12]       */ cyc -= 1; if (!z_f) r3 = rd_u32(&wstk, sp + 12u, &cyc, 0); CHK(0x03cu);
    /* 300003c: bne   L048                */ cyc -= 1; if (!z_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x048u); goto L048; } CHK(0x040u);
    /* 3000040: strb  r2, [r4]            */ wr_u8(&wch, r4, (uint8_t)r2, &cyc);        cyc -= 1; CHK(0x044u);
    /* 3000044: b     L120                */ cyc -= 2; CHK(0x120u); goto L120;
L048:
    /* 3000048: subs  r8, r8, #1          */ { uint32_t a = r8, d = a - 1u; SET_SUB(a, 1u, d); r8 = d; } cyc -= 1; CHK(0x04cu);
    /* 300004c: bgt   L014                */ cyc -= 1; if (COND_GT()) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x014u); goto L014; } CHK(0x050u);
    /* 3000050: b     L118                */ cyc -= 2; CHK(0x118u); goto L118;

L054:
    /* 3000054: ldr   r7, [r4, #28]       */ r7 = rd_u32(&wch, r4 + 28u, &cyc, 0);      cyc -= 1; CHK(0x058u);
    /* 3000058: ldr   lr, [r4, #32]       */ lr = rd_u32(&wch, r4 + 32u, &cyc, 0);      cyc -= 1; CHK(0x05cu);
L05C:
    /* 300005c: cmp   r7, r9, lsl #2      */ { uint32_t b = r9 << 2, d = r7 - b; SET_SUB(r7, b, d); } cyc -= 1; CHK(0x060u);
    /* 3000060: bcc   L080                */ cyc -= 1; if (!c_f) { cyc -= 1; CHK(0x080u); goto L080; } CHK(0x064u);
L064:
    /* 3000064: cmp   r2, #4              */ { uint32_t d = r2 - 4u; SET_SUB(r2, 4u, d); } cyc -= 1; CHK(0x068u);
    /* 3000068: ble   L0A4                */ cyc -= 1; if (COND_LE()) { cyc -= 1; CHK(0x0a4u); goto L0A4; } CHK(0x06cu);
    /* 300006c: sub   r2, r2, #4          */ r2 -= 4u;                                  cyc -= 1; CHK(0x070u);
    /* 3000070: add   r3, r3, #4          */ r3 += 4u;                                  cyc -= 1; CHK(0x074u);
    /* 3000074: sub   r7, r7, r9, lsl #2  */ r7 -= (r9 << 2);                           cyc -= 1; CHK(0x078u);
    /* 3000078: cmp   r7, r9, lsl #2      */ { uint32_t b = r9 << 2, d = r7 - b; SET_SUB(r7, b, d); } cyc -= 1; CHK(0x07cu);
    /* 300007c: bcs   L064                */ cyc -= 1; if (c_f) { cyc -= 1; CHK(0x064u); goto L064; } CHK(0x080u);
L080:
    /* 3000080: cmp   r7, r9, lsl #1      */ { uint32_t b = r9 << 1, d = r7 - b; SET_SUB(r7, b, d); } cyc -= 1; CHK(0x084u);
    /* 3000084: bcc   L09C                */ cyc -= 1; if (!c_f) { cyc -= 1; CHK(0x09cu); goto L09C; } CHK(0x088u);
    /* 3000088: cmp   r2, #2              */ { uint32_t d = r2 - 2u; SET_SUB(r2, 2u, d); } cyc -= 1; CHK(0x08cu);
    /* 300008c: ble   L0A4                */ cyc -= 1; if (COND_LE()) { cyc -= 1; CHK(0x0a4u); goto L0A4; } CHK(0x090u);
    /* 3000090: sub   r2, r2, #2          */ r2 -= 2u;                                  cyc -= 1; CHK(0x094u);
    /* 3000094: add   r3, r3, #2          */ r3 += 2u;                                  cyc -= 1; CHK(0x098u);
    /* 3000098: sub   r7, r7, r9, lsl #1  */ r7 -= (r9 << 1);                           cyc -= 1; CHK(0x09cu);
L09C:
    /* 300009c: cmp   r7, r9              */ { uint32_t d = r7 - r9; SET_SUB(r7, r9, d); } cyc -= 1; CHK(0x0a0u);
    /* 30000a0: bcc   L0D4                */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0d4u); goto L0D4; } CHK(0x0a4u);
L0A4:
    /* 30000a4: subs  r2, r2, #1          */ { uint32_t a = r2, d = a - 1u; SET_SUB(a, 1u, d); r2 = d; } cyc -= 1; CHK(0x0a8u);
    /* 30000a8: bne   L0C4                */ cyc -= 1; if (!z_f) { cyc -= 1; CHK(0x0c4u); goto L0C4; } CHK(0x0acu);
    /* 30000ac: ldr   r2, [sp, #16]       */ r2 = rd_u32(&wstk, sp + 16u, &cyc, 0);     cyc -= 1; CHK(0x0b0u);
    /* 30000b0: cmp   r2, #0              */ SET_CMP0(r2);                              cyc -= 1; CHK(0x0b4u);
    /* 30000b4: ldrne r3, [sp, #12]       */ cyc -= 1; if (!z_f) r3 = rd_u32(&wstk, sp + 12u, &cyc, 0); CHK(0x0b8u);
    /* 30000b8: bne   L0C8                */ cyc -= 1; if (!z_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0c8u); goto L0C8; } CHK(0x0bcu);
    /* 30000bc: strb  r2, [r4]            */ wr_u8(&wch, r4, (uint8_t)r2, &cyc);        cyc -= 1; CHK(0x0c0u);
    /* 30000c0: b     L120                */ cyc -= 2; CHK(0x120u); goto L120;
L0C4:
    /* 30000c4: add   r3, r3, #1          */ r3 += 1u;                                  cyc -= 1; CHK(0x0c8u);
L0C8:
    /* 30000c8: sub   r7, r7, r9          */ r7 -= r9;                                  cyc -= 1; CHK(0x0ccu);
    /* 30000cc: cmp   r7, r9              */ { uint32_t d = r7 - r9; SET_SUB(r7, r9, d); } cyc -= 1; CHK(0x0d0u);
    /* 30000d0: bcs   L0A4                */ cyc -= 1; if (c_f) { cyc -= 1; CHK(0x0a4u); goto L0A4; } CHK(0x0d4u);
L0D4:
    /* 30000d4: ldrsb r0, [r3]            */ r0 = (uint32_t)rd_s8(&wsmp, r3, &cyc);     cyc -= 1; CHK(0x0d8u);
    /* 30000d8: ldrsb r1, [r3, #1]        */ r1 = (uint32_t)rd_s8(&wsmp, r3 + 1u, &cyc); cyc -= 1; CHK(0x0dcu);
    /* 30000dc: sub   r1, r1, r0          */ r1 -= r0;                                  cyc -= 1; CHK(0x0e0u);
    /* 30000e0: mul   r6, r1, r7          */ r6 = r1 * r7;                              cyc -= 1; CHK(0x0e4u);
    /* 30000e4: mul   r1, r6, ip          */ r1 = r6 * ip;                              cyc -= 1; CHK(0x0e8u);
    /* 30000e8: add   r6, r0, r1, asr #23 */ r6 = r0 + (uint32_t)(((int32_t)r1) >> 23); cyc -= 1; CHK(0x0ecu);
    /* 30000ec: mul   r1, r6, sl          */ r1 = r6 * sl;
#ifdef M4A_SABOTAGE
    r1 ^= 0x00000100u;
#endif
                                                                                        cyc -= 1; CHK(0x0f0u);
    /* 30000f0: ldrb  r0, [r5]            */ r0 = rd_u8(&wmix, r5, &cyc);               cyc -= 1; CHK(0x0f4u);
    /* 30000f4: add   r0, r0, r1, asr #8  */ r0 += (uint32_t)(((int32_t)r1) >> 8);      cyc -= 1; CHK(0x0f8u);
    /* 30000f8: strb  r0, [r5], #1        */ wr_u8(&wmix, r5, (uint8_t)r0, &cyc); r5 += 1u; cyc -= 1; CHK(0x0fcu);
    /* 30000fc: add   r7, r7, lr          */ r7 += lr;                                  cyc -= 1; CHK(0x100u);
    /* 3000100: subs  r8, r8, #1          */ { uint32_t a = r8, d = a - 1u; SET_SUB(a, 1u, d); r8 = d; } cyc -= 1; CHK(0x104u);
    /* 3000104: beq   L114                */ cyc -= 1; if (z_f) { cyc -= 1; CHK(0x114u); goto L114; } CHK(0x108u);
    /* 3000108: cmp   r7, r9              */ { uint32_t d = r7 - r9; SET_SUB(r7, r9, d); } cyc -= 1; CHK(0x10cu);
    /* 300010c: bcc   L0D4                */ cyc -= 1; if (!c_f) { cyc -= 1; if (FAILED()) return M4A_DECLINED; CHK(0x0d4u); goto L0D4; } CHK(0x110u);
    /* 3000110: b     L05C                */ cyc -= 2; CHK(0x05cu); goto L05C;
L114:
    /* 3000114: str   r7, [r4, #28]       */ wr_u32(&wch, r4 + 28u, r7, &cyc, 0);       cyc -= 1; CHK(0x118u);
L118:
    /* 3000118: str   r2, [r4, #24]       */ wr_u32(&wch, r4 + 24u, r2, &cyc, 0);       cyc -= 1; CHK(0x11cu);
    /* 300011c: str   r3, [r4, #40]       */ wr_u32(&wch, r4 + 40u, r3, &cyc, 0);       cyc -= 1; CHK(0x120u);
L120:
    /* 3000120: ldr   r8, [sp]            */ r8 = rd_u32(&wstk, sp, &cyc, 0);           cyc -= 1;

    if (FAILED())
        return M4A_DECLINED;

    SAVE_REGS();
    return M4A_DONE;

#undef FAILED
}

static const m4a_variant m4a_v6_bytes_mono = {
    "m4a-soundmainram-bytes-mono",
    m4a_code_v6_bytes_mono,
    (uint32_t)sizeof m4a_code_v6_bytes_mono,
    V6_EXIT_OFF,
    m4a_run_v6_bytes_mono,
};

static const m4a_variant m4a_v2_stereo = {
    "m4a-soundmainram-stereo",
    m4a_code_v2_stereo,
    (uint32_t)sizeof m4a_code_v2_stereo,
    V2_EXIT_OFF,
    m4a_run_v2_stereo,
};

static const m4a_variant m4a_v1_mono = {
    "m4a-soundmainram-mono",
    m4a_code_v1_mono,
    (uint32_t)sizeof m4a_code_v1_mono,
    V1_EXIT_OFF,
    m4a_run_v1_mono,
};

const m4a_variant *const m4a_variants[] = {
    &m4a_v1_mono,
    &m4a_v2_stereo,
    &m4a_v3_stereo2,
    &m4a_v4_stereo3,
    &m4a_v5_bytes,
    &m4a_v6_bytes_mono,
    0
};

/* ------------------------------------------------------------- discovery */

const m4a_variant *m4a_identify(const uint8_t *code, uint32_t len)
{
    int i;
    for (i = 0; m4a_variants[i]; i++) {
        const m4a_variant *v = m4a_variants[i];
        if (len >= v->size && memcmp(code, v->code, v->size) == 0)
            return v;
    }
    return 0;
}

const m4a_variant *m4a_scan(const uint8_t *mem, uint32_t len, uint32_t base_addr,
                            uint32_t *out_pc)
{
    uint32_t off;
    /* ARM code, word-aligned wherever a game chooses to put it. */
    for (off = 0; off + 4u <= len; off += 4u) {
        const m4a_variant *v = m4a_identify(mem + off, len - off);
        if (v) {
            if (out_pc)
                *out_pc = base_addr + off;
            return v;
        }
    }
    return 0;
}
