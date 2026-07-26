/* Wiring the M4A HLE into gpSP.
 *
 * Three small jobs, and they are all here so that gpSP's own files carry as
 * little of this as possible (cpu.cc gets one `if`, and gba_memory.c gets
 * nothing at all):
 *
 *   1. A bus adapter, so the HLE sees memory exactly as the interpreter does —
 *      the same 32 KB page pointers and the same waitstate costs. Anything else
 *      and the two would not be the same program, which is the whole game.
 *
 *   2. The scan. Games copy the mixer into IWRAM during sound init, which has
 *      not happened yet on frame 0, so we look once per frame until we find it
 *      and then stop looking. A memcmp over 32 KB, a handful of times, next to
 *      an emulated frame: free.
 *
 *   3. A verify mode. With M4A_HLE_VERIFY defined, every hooked block is run
 *      BOTH ways from the same state — interpreter and native — and every
 *      register, every cycle and every byte either wrote is compared. That is
 *      what proves the claim; the shipping build then runs only the native one.
 *      A test that has never failed proves nothing, so prove.sh also runs a
 *      build with the transliteration deliberately broken, and checks that this
 *      catches it.
 */
#include "m4a_hle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* gpSP internals we borrow. Declared by hand rather than by including gpSP's
 * headers: those pull in a "common.h" that collides with the firmware's, which
 * is a fight this file has no reason to pick. Keep these in step with gpSP —
 * the compiler cannot check them for you. */
extern unsigned char *memory_map_read[8 * 1024];
extern unsigned char  ws_cyc_seq[16][2];
extern unsigned char  ws_cyc_nseq[16][2];
extern unsigned int   reg[64];          /* gpSP's register file (r0..r15 and more) */
unsigned int update_gba(int remaining_cycles);

/* These come straight from gpSP's cpu.h and main.h. They are copied rather than
 * included for the reason at the top of the file, which means the compiler cannot
 * check them — so they are spelled with their source, and a gpSP merge must read
 * this block. Getting cycles_to_run's mask wrong (it is 0x7FFF, not a 30-bit one)
 * hands the CPU a budget of hundreds of thousands of cycles and the emulated
 * machine simply stops advancing. */
#define REG_PC           15      /* cpu.h: REG_PC          = 15 */
#define REG_CPSR         16      /* cpu.h: REG_CPSR        = 16 */
#define CPU_HALT_STATE   18      /* cpu.h: CPU_HALT_STATE  = 18 */
#define CPU_ACTIVE        0      /* cpu.h: CPU_ACTIVE      =  0 */
#define M4A_FRAME_BIT    0x80000000u  /* main.h: completed_frame(c) = (c) & 0x80000000 */
#define M4A_CHANGED_PC   0x40000000u  /* main.c: changed_pc = 0x40000000 */
#define M4A_CYCLE_MASK   0x7FFFu      /* main.h: cycles_to_run(c) = (c) & 0x7FFF */
extern unsigned int   cpu_ticks;       /* every guest cycle the hardware was told about */
extern unsigned int   execute_cycles;  /* the size of the slice the CPU is inside */

#ifdef M4A_HLE_VERIFY
/* Bumped by gpSP whenever cpu_ticks advances for a reason that is not "the block
 * executed an instruction": an interrupt taken, a DMA stall, a halt. */
unsigned int m4a_irq_raises;
/* What the block costs the INTERPRETER, summed over the instructions whose PC is
 * inside it and nothing else. cpu_ticks cannot answer this — the interpreter runs
 * the block over several slices, and cpu_ticks also advances for the DMA stalls
 * and interrupt handlers that land in the gaps. This does not. */
unsigned int m4a_blk_cycles;
unsigned int m4a_blk_insns;
unsigned int m4a_blk_size;
#endif

/* Where we hook. 0 means "not found (yet)" — a legal ARM PC is never 0 here. */
unsigned int m4a_hook_pc;
unsigned int m4a_hook_exit_pc;

static const m4a_variant *s_variant;
static int s_scan_done;      /* found it, or given up */
static int s_frames_scanned;

/* Games set their sound engine up in the first few frames. If a hundred have
 * gone by with no mixer in IWRAM, this game does not have the one we know, and
 * scanning every frame forever would be a tax on every game that does not. */
#define M4A_SCAN_FRAME_LIMIT  600

/* ----------------------------------------------------------- bus adapter */

static unsigned char *m4a_map(void *ctx, uint32_t addr, uint32_t *span)
{
    unsigned char *page;
    (void)ctx;

    /* The GBA bus decodes 28 bits; above that the interpreter takes its slow
     * path (open bus, mirrors, I/O side effects) and so must we — by declining. */
    if (addr >= 0x10000000u)
        return 0;

    page = memory_map_read[addr >> 15];
    if (!page)
        return 0;

    /* gpSP indexes a page with (addr & 0x7FFF), so the pointer we hand back is
     * the page base offset to `addr`, valid to the end of that 32 KB page. */
    *span = 0x8000u - (addr & 0x7FFFu);
    return page + (addr & 0x7FFFu);
}

static void m4a_cost(void *ctx, uint32_t addr, int *c8n, int *c32n, int *c32s)
{
    unsigned int region = addr >> 24;
    (void)ctx;

    /* Past the bus, the interpreter charges nothing — see fast_read_memory's
     * `if (_address < 0x10000000)`. Mirror that, including the zero. */
    if (region > 15u) {
        *c8n = *c32n = *c32s = 0;
        return;
    }
    *c8n  = ws_cyc_nseq[region][0];
    *c32n = ws_cyc_nseq[region][1];
    *c32s = ws_cyc_seq[region][1];
}

/* Out of cycles, mid-block: move the hardware along and hand back a fresh budget.
 *
 * This is the same thing gpSP's own loop does when its slice runs out — and it has
 * to be, because the mixer block is thousands of cycles long and a slice is a
 * scanline at most, so the interpreter passes through here eight or ten times in
 * a single call of the mixer. Do it in the same places, on the same cycles, and
 * the emulated machine cannot tell which of us ran the code.
 *
 * Two things must be true before update_gba() is called, and both were learned by
 * getting them wrong:
 *
 *   - reg[REG_PC] must be the address of the NEXT instruction of the block. An
 *     interrupt raised in here stacks reg[REG_PC] as its return address. Leave it
 *     pointing at the top of the mixer and the game comes back from the interrupt
 *     and mixes the whole frame again.
 *
 *   - the flags must be in REG_CPSR. They live in the interpreter's locals while
 *     it runs, and in ours while we run, and update_gba() reads the register.
 */
static int m4a_refill(void *ctx, m4a_state *s)
{
    unsigned int ret, cpsr;
    int i;
    (void)ctx;

    for (i = 0; i < 15; i++)
        reg[i] = s->r[i];
    reg[REG_PC] = s->pc;

    cpsr = reg[REG_CPSR] & 0x0FFFFFFFu;
    cpsr |= (s->n << 31) | (s->z << 30) | (s->c << 29) | (s->v << 28);
    reg[REG_CPSR] = cpsr;

    ret = update_gba(s->cycles);

    /* The frame ended inside the mixer. gpSP's loop returns from execute_arm()
     * here, and so must we — with the PC on the next instruction, so the next call
     * picks the block up where it left off. */
    if (ret & M4A_FRAME_BIT)
        return M4A_FRAME_DONE;

    /* An interrupt was taken, or the CPU was halted / stalled by a DMA. Either way
     * the PC is no longer ours and the interpreter has to drive. It will finish
     * the rest of this block itself — slowly, and correctly, which is the right
     * trade for something that (measured over eleven thousand blocks) does not
     * actually happen. */
    if ((ret & M4A_CHANGED_PC) || reg[CPU_HALT_STATE] != CPU_ACTIVE) {
        for (i = 0; i < 15; i++)
            s->r[i] = reg[i];
        s->pc     = reg[REG_PC];
        s->cycles = (int)(ret & M4A_CYCLE_MASK);
        return M4A_YIELD;
    }

    s->cycles = (int)(ret & M4A_CYCLE_MASK);
    return M4A_OK_CONTINUE;
}

static const m4a_bus s_bus = { m4a_map, m4a_cost, m4a_refill, 0 };

/* ---------------------------------------------------------------- the scan */

void m4a_hle_reset(void)
{
    m4a_hook_pc      = 0;
    m4a_hook_exit_pc = 0;
    s_variant        = 0;
    s_scan_done      = 0;
    s_frames_scanned = 0;
}

/* Call once per frame. Cheap, and stops calling itself once it has an answer. */
void m4a_hle_scan_frame(void)
{
    unsigned char *iwram_page;
    uint32_t pc = 0;
    const m4a_variant *v;

    if (s_scan_done)
        return;
    if (++s_frames_scanned > M4A_SCAN_FRAME_LIMIT) {
        s_scan_done = 1;
        return;
    }

    iwram_page = memory_map_read[0x3000000 >> 15];
    if (!iwram_page)
        return;

    v = m4a_scan(iwram_page, 0x8000u, 0x3000000u, &pc);
    if (!v)
        return;

    s_variant        = v;
    m4a_hook_pc      = pc;
    m4a_hook_exit_pc = pc + v->exit_off;
    s_scan_done      = 1;
#ifdef M4A_HLE_VERIFY
    m4a_blk_size     = v->size;
#endif
}

const char *m4a_hle_variant_name(void)
{
    return s_variant ? s_variant->name : "none";
}

/* ------------------------------------------------------------- the hook */

/* Called from cpu.cc when the ARM PC lands on m4a_hook_pc. `regs` is gpSP's
 * reg[] array, `cycles` its cycles_remaining, and n/z/c/v its live flag locals —
 * which are the truth inside the interpreter's loop, not REG_CPSR. Returns 1 if
 * the block ran (the caller then sets PC to m4a_hook_exit_pc), 0 if it declined
 * and the caller must interpret the block as usual. */
int m4a_hle_execute(unsigned int *regs, int *cycles,
                    unsigned int *n, unsigned int *z,
                    unsigned int *c, unsigned int *v)
{
    m4a_state st;
    int i, rc;

    if (!s_variant)
        return M4A_DECLINED;

    for (i = 0; i < 16; i++)
        st.r[i] = regs[i];
    st.cycles = *cycles;
    st.n = *n; st.z = *z; st.c = *c; st.v = *v;
    st.pc = m4a_hook_pc;       /* the block's base; CHK() builds resume PCs from it */

    rc = s_variant->run(&st, &s_bus);
    if (rc == M4A_DECLINED)
        return M4A_DECLINED;

    for (i = 0; i < 16; i++)
        regs[i] = st.r[i];
    *cycles = st.cycles;
    *n = st.n; *z = st.z; *c = st.c; *v = st.v;
    if (rc != M4A_DONE)
        regs[15] = st.pc;      /* gave way mid-block: this is where to pick up */
    return rc;
}

/* --------------------------------------------------------- verify mode */
#ifdef M4A_HLE_VERIFY

/* The verifier re-runs the block OFFLINE, over restored memory, purely to see
 * what it computes — the interpreter has already run it for real. So it must not
 * be allowed to move the hardware along a second time: it gets a budget it cannot
 * exhaust, and a refill that would be a bug if it were ever reached. What we then
 * read off is how much of that budget the block spent, which is the number to
 * compare against what the block cost the interpreter. */
static int m4a_refill_never(void *ctx, m4a_state *s)
{
    (void)ctx; (void)s;
    fprintf(stderr, "M4A VERIFY: the offline re-run ran out of an unexhaustible "
                    "budget. That is not a mixer, it is a loop.\n");
    exit(5);
}

static const m4a_bus s_bus_offline = { m4a_map, m4a_cost, m4a_refill_never, 0 };

#define M4A_OFFLINE_BUDGET  (1 << 28)

/* Same as m4a_hle_execute(), but offline: no hardware, no yielding, and the
 * answer is "what did it compute and what did it cost", not "what happened". */
static int m4a_hle_execute_offline(unsigned int *regs, long *cost,
                                   unsigned int *n, unsigned int *z,
                                   unsigned int *c, unsigned int *v)
{
    m4a_state st;
    int i, rc;

    if (!s_variant)
        return 0;

    for (i = 0; i < 16; i++)
        st.r[i] = regs[i];
    st.cycles = M4A_OFFLINE_BUDGET;
    st.n = *n; st.z = *z; st.c = *c; st.v = *v;
    st.pc = m4a_hook_pc;

    rc = s_variant->run(&st, &s_bus_offline);
    if (rc != M4A_DONE)
        return 0;

    for (i = 0; i < 16; i++)
        regs[i] = st.r[i];
    *cost = (long)M4A_OFFLINE_BUDGET - (long)st.cycles;
    *n = st.n; *z = st.z; *c = st.c; *v = st.v;
    return 1;
}

/* The interpreter and the native block are run from the same state, over the
 * same memory, and everything either of them touched is compared. Registers and
 * cycles are easy. Memory is done by snapshotting the three windows the block
 * can write — the channel struct, the mix buffer, and the guest stack below sp —
 * running one, saving the result, restoring, running the other, and diffing.
 *
 * The mix buffer window is bounded by r8: the block emits r8 samples, four to a
 * word, so it advances the mix pointer by r8 bytes, plus the one extra word the
 * partial-word flush at L3734 can store. */
#define SNAP_CH    64u
#define SNAP_STK   64u

typedef struct {
    unsigned char ch[SNAP_CH];
    unsigned char stk[SNAP_STK];
    unsigned char *mix;
    uint32_t mix_addr, mix_len;
} m4a_snap;

static int snap_read(uint32_t addr, unsigned char *dst, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        uint32_t span;
        unsigned char *p = m4a_map(0, addr + i, &span);
        if (!p)
            return 0;
        dst[i] = *p;
    }
    return 1;
}

static void snap_write(uint32_t addr, const unsigned char *src, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++) {
        uint32_t span;
        unsigned char *p = m4a_map(0, addr + i, &span);
        if (p)
            *p = src[i];
    }
}

static int snap_take(m4a_snap *s, const unsigned int *regs)
{
    uint32_t r4 = regs[4], r5 = regs[5] & 0x3FFFFFFFu, r8 = regs[8], sp = regs[13];

    /* The block emits r8 samples, four to a word, so the LEFT mix pointer walks
     * r8 bytes forward (plus the one extra word the partial flush can store). The
     * stereo variant writes the RIGHT channel a fixed 0x630 further on, and a
     * snapshot that stopped at the left one would compare half the music and call
     * it identical. Cover both; the mono variant simply never touches the tail. */
    s->mix_addr = r5;
    s->mix_len  = 0x630u + r8 + 8u;
    s->mix = (unsigned char *)malloc(s->mix_len);
    if (!s->mix)
        return 0;
    if (!snap_read(r4, s->ch, SNAP_CH) ||
        !snap_read(sp - 8u, s->stk, SNAP_STK) ||
        !snap_read(s->mix_addr, s->mix, s->mix_len)) {
        free(s->mix);
        s->mix = 0;
        return 0;
    }
    return 1;
}

static void snap_restore(const m4a_snap *s, const unsigned int *regs)
{
    snap_write(regs[4], s->ch, SNAP_CH);
    snap_write(regs[13] - 8u, s->stk, SNAP_STK);
    snap_write(s->mix_addr, s->mix, s->mix_len);
}

static void snap_free(m4a_snap *s)
{
    free(s->mix);
    s->mix = 0;
}

static unsigned long g_verify_blocks;
static unsigned long g_verify_declined;

/* Saved entry state, so the post-interpreter comparison has something to compare
 * against. cpu.cc calls _pre before letting the interpreter run the block, and
 * _post when the interpreter reaches the exit PC. */
static unsigned int  v_regs_in[16];
static int           v_cycles_in;
static unsigned int  v_n_in, v_z_in, v_c_in, v_v_in;
/* The interpreter runs this block over SEVERAL slices — it is thousands of cycles
 * long and a slice is a scanline at most — so its cycles_remaining when it reaches
 * the exit is a fresh budget, not "what it started with minus what it spent". The
 * only honest way to ask what the block cost it is to count what the hardware was
 * told: cpu_ticks, plus whatever has been spent inside the current slice and not
 * reported yet (execute_cycles - cycles_remaining). Subtract the same quantity at
 * entry and you have the true cost, however many times the loop went round. */
static unsigned int  v_ticks_in;
static int           v_slice_used_in;
static unsigned int  v_irq_in;
static unsigned long g_verify_cycles_checked;
static unsigned long g_verify_cycles_skipped;
static m4a_snap      v_snap_in;
static int           v_armed;

void m4a_hle_verify_pre(const unsigned int *regs, int cycles,
                        unsigned int n, unsigned int z,
                        unsigned int c, unsigned int v)
{
    int i;
    if (!s_variant)
        return;
    for (i = 0; i < 16; i++)
        v_regs_in[i] = regs[i];
    v_cycles_in = cycles;
    v_n_in = n; v_z_in = z; v_c_in = c; v_v_in = v;
    v_ticks_in      = cpu_ticks;
    v_slice_used_in = (int)execute_cycles - cycles;
    v_irq_in        = m4a_irq_raises;
    m4a_blk_cycles  = 0;
    m4a_blk_insns   = 0;
    v_armed = snap_take(&v_snap_in, regs);
}

void m4a_hle_verify_post(const unsigned int *regs_after, int cycles_after,
                         unsigned int n_after, unsigned int z_after,
                         unsigned int c_after, unsigned int v_after)
{
    m4a_snap after_interp;
    unsigned int regs_hle[16];
    unsigned int n_hle, z_hle, c_hle, v_hle;
    long hle_cost = 0;
    int i, bad = 0;
    (void)cycles_after;

    if (!v_armed)
        return;
    v_armed = 0;

    /* What the interpreter left behind. */
    if (!snap_take(&after_interp, v_regs_in)) {
        snap_free(&v_snap_in);
        return;
    }

    /* Put memory back the way it was, and run the native block over it. */
    snap_restore(&v_snap_in, v_regs_in);
    for (i = 0; i < 16; i++)
        regs_hle[i] = v_regs_in[i];
    n_hle = v_n_in; z_hle = v_z_in; c_hle = v_c_in; v_hle = v_v_in;

    if (!m4a_hle_execute_offline(regs_hle, &hle_cost, &n_hle, &z_hle, &c_hle, &v_hle)) {
        /* Declining is legal, but it must be rare — if it is not, the hook is
         * not paying for itself and we want to hear about it. */
        g_verify_declined++;
        snap_restore(&after_interp, v_regs_in);   /* leave the interpreter's result */
        snap_free(&after_interp);
        snap_free(&v_snap_in);
        return;
    }

    g_verify_blocks++;

    /* r15 (pc) is the caller's business; r7 and r11 the block never touches. */
    for (i = 0; i < 15; i++) {
        if (i == 7 || i == 11)
            continue;
        if (regs_hle[i] != regs_after[i]) {
            fprintf(stderr, "M4A VERIFY: r%d  interp=%08x  hle=%08x\n",
                    i, regs_after[i], regs_hle[i]);
            bad = 1;
        }
    }
    {
        long interp_cost = (long)m4a_blk_cycles;   /* in-block instructions only */
        g_verify_cycles_checked++;
        if (interp_cost != hle_cost) {
            fprintf(stderr, "M4A VERIFY: guest cycles  interp=%ld  hle=%ld  (delta %ld)"
                            "  [%u in-block instructions]\n",
                    interp_cost, hle_cost, hle_cost - interp_cost, m4a_blk_insns);
            bad = 1;
        }
    }
    (void)v_ticks_in; (void)v_slice_used_in; (void)v_irq_in;
    if (n_hle != n_after || z_hle != z_after ||
        c_hle != c_after || v_hle != v_after) {
        fprintf(stderr, "M4A VERIFY: flags  interp=%u%u%u%u  hle=%u%u%u%u\n",
                n_after, z_after, c_after, v_after, n_hle, z_hle, c_hle, v_hle);
        bad = 1;
    }

    {
        m4a_snap after_hle;
        if (snap_take(&after_hle, v_regs_in)) {
            if (memcmp(after_hle.ch, after_interp.ch, SNAP_CH) != 0) {
                fprintf(stderr, "M4A VERIFY: channel struct differs\n");
                bad = 1;
            }
            if (memcmp(after_hle.stk, after_interp.stk, SNAP_STK) != 0) {
                fprintf(stderr, "M4A VERIFY: guest stack differs\n");
                bad = 1;
            }
            if (after_hle.mix_len == after_interp.mix_len &&
                memcmp(after_hle.mix, after_interp.mix, after_hle.mix_len) != 0) {
                uint32_t k;
                for (k = 0; k < after_hle.mix_len; k++)
                    if (after_hle.mix[k] != after_interp.mix[k])
                        break;
                fprintf(stderr, "M4A VERIFY: mix buffer differs at +%u of %u "
                                "(interp=%02x hle=%02x)\n",
                        k, after_hle.mix_len, after_interp.mix[k], after_hle.mix[k]);
                bad = 1;
            }
            snap_free(&after_hle);
        }
    }

    if (bad) {
        fprintf(stderr, "M4A VERIFY: FAILED after %lu good blocks\n", g_verify_blocks);
        exit(3);
    }

    snap_free(&after_interp);
    snap_free(&v_snap_in);
}

void m4a_hle_verify_report(void)
{
    fprintf(stderr,
            "M4A VERIFY: %lu blocks identical in registers, flags and memory; "
            "%lu of them also exact on guest cycles (%lu had an interrupt land "
            "inside, where the cost is not the block's to answer for); "
            "%lu declined\n",
            g_verify_blocks, g_verify_cycles_checked, g_verify_cycles_skipped,
            g_verify_declined);
    if (g_verify_blocks == 0) {
        fprintf(stderr, "M4A VERIFY: FAIL — the hook never fired. A test that "
                        "never ran is not a test.\n");
        exit(4);
    }
    if (g_verify_cycles_checked == 0) {
        fprintf(stderr, "M4A VERIFY: FAIL — not one block's cycle count could be "
                        "checked. The cycle model is the part most easily got "
                        "wrong, so an unchecked one is not a pass.\n");
        exit(4);
    }
}

#endif /* M4A_HLE_VERIFY */
