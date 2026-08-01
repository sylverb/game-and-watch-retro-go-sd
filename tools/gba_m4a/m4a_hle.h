/* M4A software-mixer HLE — the ABI.
 *
 * WHAT THIS IS
 * ------------
 * Nearly every commercial GBA game uses Nintendo's M4A ("Sappy") sound library,
 * and M4A mixes its PCM channels **in software, on the guest CPU**. It copies an
 * ARM routine (`SoundMainRAM`) into IWRAM at boot and runs it once per channel
 * per frame. On a Game & Watch that routine is the single most expensive thing
 * the emulator does:
 *
 *     FFTA, real gameplay : 37.1% of all guest instructions
 *     FFTA, menu screen   : 33.7%           (it is a per-frame CONSTANT — the
 *     Pokemon Emerald     : 27.3%            music mixes the same amount of
 *                                            audio whatever is on screen)
 *
 * The cost is not the algorithm, it is the interpretation: ~71 host instructions
 * to interpret each guest one. This runs the same block natively — same integer
 * arithmetic, same memory writes, same guest cycle count — so the guest timeline
 * is bit-identical and only the host work disappears.
 *
 * It is the same idea as the idle-loop skip, applied to a loop that is not idle:
 * recognise a known block by its bytes, and execute its MEANING instead of its
 * instructions.
 *
 * WHY A BYTE SIGNATURE AND NOT A GAME TABLE
 * -----------------------------------------
 * The block is library code — the same bytes in every game that links that
 * version of M4A. Matching the bytes proves the code we are replacing IS the
 * code we transliterated. A per-game table would be a guess about identity; the
 * bytes are identity. (The generated table in m4a_sigs.c still exists, but only
 * to name the variants and to record which games were A/B proven — see prove.sh.)
 *
 * WHAT THE CALLER MUST GUARANTEE
 * ------------------------------
 * Nothing. m4a_hle_run() declines (returns 0, guest state untouched) whenever it
 * meets anything it is not certain about, and the caller then interprets the
 * block normally. Declining is always safe; being wrong is not.
 */
#ifndef M4A_HLE_H
#define M4A_HLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Guest CPU state the block reads and writes. Register numbering is ARM's:
 * r[13] is sp, r[14] is lr. r[15] (pc) is not used by the block.
 *
 * The flags travel in AND out. The block does not read them before its first
 * `tst`, so passing them in changes nothing today — but `tst` sets only N and Z,
 * leaving C and V as it found them, and those are the flags the guest sees when
 * the block returns. A transliteration that dropped them would be right about
 * the music and wrong about whatever the caller does next. */
typedef struct {
    uint32_t r[16];
    int32_t  cycles;   /* guest cycles; the block subtracts what it spends */
    uint32_t n, z, c, v;
    uint32_t pc;       /* where to resume — set when the block gives way */
} m4a_state;

/* What m4a_variant::run gives back. */
#define M4A_DECLINED    0   /* nothing was touched; interpret the block as usual */
#define M4A_DONE        1   /* the block finished; resume at entry + exit_off      */
#define M4A_YIELD       2   /* it gave way mid-block; resume at state->pc          */
#define M4A_FRAME_DONE  3   /* ... and the frame ended: return from execute_arm    */

/* The emulator's memory, seen the way the interpreter sees it.
 *
 * map(): host pointer for a guest address, or NULL if the address is not
 *        plainly mapped. *span receives how many bytes past `addr` are
 *        contiguously valid — gpSP maps in 32 KB pages, so a span may end
 *        before the region does, and the caller re-maps. A NULL return makes
 *        the HLE decline.
 *
 * cost(): what the interpreter would charge for an access at `addr`.
 *         c8n/c32n are non-sequential (ldr/str), c32s is sequential (ldm/stm).
 *         Charging the real cost is what keeps the guest timeline identical.
 */
typedef struct {
    uint8_t *(*map)(void *ctx, uint32_t addr, uint32_t *span);
    void     (*cost)(void *ctx, uint32_t addr, int *c8n, int *c32n, int *c32s);

    /* Out of cycles, mid-block. Move the hardware along and give me more.
     *
     * This is the part that makes the whole thing honest. The block is thousands
     * of guest cycles long and the emulator runs the CPU in slices of a scanline
     * or less, so the interpreter does NOT run this block in one go: it falls out
     * of its loop several times on the way through, lets the video, the timers
     * and the DMA catch up, and comes back. Run the block atomically and you have
     * moved every one of those hardware events to after the music instead of
     * during it — the screen and the audio come out the same, and the clock does
     * not, and a game that reads the clock is then a different game.
     *
     * So the block stops where the interpreter stops — after the same instruction,
     * on the same cycle — and calls this. `s` carries the exact state, including
     * `pc`, so that an interrupt raised in here stacks the right return address.
     *
     * Returns M4A_OK_CONTINUE to carry on (s->cycles refilled), or M4A_YIELD /
     * M4A_FRAME_DONE, which the block passes straight back to its caller. */
    int      (*refill)(void *ctx, m4a_state *s);

    void *ctx;
} m4a_bus;

#define M4A_OK_CONTINUE  (-1)   /* refill only: keep going */

/* One recognised block: what it looks like, and what runs it. */
typedef struct m4a_variant {
    const char    *name;
    const uint8_t *code;      /* the exact ARM bytes, from the entry point */
    uint32_t       size;      /* how many of them */
    uint32_t       exit_off;  /* where the interpreter resumes, from the entry */
    /* Runs the block. Returns 1 if it did (state advanced to exit_off), or 0 if
     * it declined and the caller must interpret. Guest state is untouched on 0. */
    int (*run)(m4a_state *s, const m4a_bus *bus);
} m4a_variant;

/* The variants we know, NULL-terminated. Generated — see m4a_sigs.c. */
extern const m4a_variant *const m4a_variants[];

/* Does `code` (guest bytes at some IWRAM address) start a block we know?
 * Returns the variant, or NULL. `len` is how many bytes are readable there. */
const m4a_variant *m4a_identify(const uint8_t *code, uint32_t len);

/* Search a region of guest memory (typically the whole 32 KB of IWRAM) for a
 * block we know. Returns the variant and writes the guest address of its entry
 * point to *out_pc; returns NULL if there is nothing to find.
 *
 * Games copy the mixer into IWRAM during sound init, so this finds nothing on
 * the first frames and then finds it for the rest of the run. Call it once per
 * frame until it hits; it is a memcmp over 32 KB and costs nothing next to a
 * frame of emulation. */
const m4a_variant *m4a_scan(const uint8_t *mem, uint32_t len, uint32_t base_addr,
                            uint32_t *out_pc);

#ifdef __cplusplus
}
#endif

#endif /* M4A_HLE_H */
