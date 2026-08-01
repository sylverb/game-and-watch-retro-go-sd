#include <odroid_system.h>
             
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <SDL.h>

#include "porting.h"
#include "crc32.h"

#include <gfx.h>
#include "gw_lcd.h"
#include <pce.h>
#include <romdb.h>
#include "pce_cd.h"
#include "pce_scsi.h"
#include "pce_adpcm.h"
#include "syscard_load.h"
#ifdef PCE_ENABLE_ARCADE_CARD
#include "arcade_card.h"
#endif
#include "pce_input_sdl.h"
#include "pce_audio.h"

extern int linux_savestate_req;
extern int linux_loadstate_req;

/* Path to the disc .cue (argv[1]); NULL = run System Card only. */
static const char *g_cue_path = NULL;

static bool host_LoadState(const char *savePathName);
static bool host_SaveState(const char *savePathName);

static void pce_state_path(char *out, size_t out_size)
{
	snprintf(out, out_size, "%s.state", g_cue_path);
}

static void pce_state_alt_path(char *out, size_t out_size)
{
	snprintf(out, out_size, "%s.state2", g_cue_path);
}

static void pce_sram_path(char *out, size_t out_size)
{
	snprintf(out, out_size, "%s.sram", g_cue_path);
}

static void pce_sram_load(void)
{
	if (!g_cue_path) return;
	pce_bram_init();
	char path[1024];
	pce_sram_path(path, sizeof(path));
	FILE *f = fopen(path, "rb");
	if (f) { fread(PCE.bram, 1, 0x800, f); fclose(f); }
	pce_bram_format_if_needed();
}

static void pce_sram_save(void)
{
	if (!g_cue_path) return;
	char path[1024];
	pce_sram_path(path, sizeof(path));
	FILE *f = fopen(path, "wb");
	if (f) { fwrite(PCE.bram, 1, 0x800, f); fclose(f); }
}

static void pce_linux_save_state(void)
{
	if (!g_cue_path)
		return;
	char path[1024];
	pce_state_path(path, sizeof(path));
	if (!host_SaveState(path))
		fprintf(stderr, "PCE: save failed '%s'\n", path);
}

static void pce_linux_load_state(void)
{
	if (!g_cue_path)
		return;
	char path[1024];
	pce_state_path(path, sizeof(path));
	if (!host_LoadState(path))
		fprintf(stderr, "PCE: load failed '%s'\n", path);
}

/* Deterministic per-instruction PC trace (host only). h6280_run() calls
 * pce_scsi_pc_tick() each instruction when g_pcecd_trace is set; we turn it on
 * right after the force-CLI so we capture EXACTLY what the game/vblank does
 * once interrupts are live — no Heisenbug (host timing is cycle-deterministic). */
static void dump_mem(uint16_t start, int len);   /* fwd */
int g_pcecd_trace = 0;
int g_pce_kill_timer = 0;
unsigned long g_tick_count = 0;
int g_frame_now = 0;
uint16_t g_ring[256]; int g_ridx = 0;
void pce_scsi_pc_tick(uint16_t pc)
{
    static int n = 0;          /* distinct (run-length-compressed) entries written */
    static int run = 0;        /* consecutive repeats of last_pc */
    static uint16_t last_pc = 0xFFFF;
    static FILE *tf = NULL;
    static int started = 0;
    static int dumped_entry = 0, dumped_idle = 0;
    /* Self-arm: ignore everything (incl. the long System Card boot) until the CPU
     * first lands on the REAL game exec entry (from the disc IPL: load=$6000,
     * exec=$6000), then trace the full game init from there. */
    #define GAME_EXEC 0x6000
    { extern unsigned long g_tick_count; g_tick_count++; }   /* liveness counter */
    { /* poll-watch: who installs the WRAM IRQ1 handler at $30fd? */
      static uint8_t last30fd = 0xAA;
      uint8_t cur = PCE.RAM[0x10fd];
      if (cur != last30fd) {
          printf("[wp] $30fd: %02x -> %02x near pc=%04x\n", last30fd, cur, pc);
          fflush(stdout);
          last30fd = cur;
      } }
    { /* poll-watch: bank 0x7f (CD RAM 0x2E000, slots 92-95 of the New-game 256KB
       * load — the copy loop's LDX $DFF4 block-count source). Decides the fork:
       * never nonzero = the transfer itself lost lba 4232-4235; nonzero->zero =
       * something erases it later (log the pc that did). Watch the two decisive
       * bytes at instruction granularity — cheap, no sum. */
      static uint8_t l0 = 0xAA, lf4 = 0xAA;
      uint8_t c0 = PCE.MemoryMapR[0x7f][0], cf4 = PCE.MemoryMapR[0x7f][0x1ff4];
      if (c0 != l0 || cf4 != lf4) {
          printf("[wp7f] f%d bank7f[0]=%02x->%02x [$DFF4]=%02x->%02x at pc=%04x\n",
                 g_frame_now, l0, c0, lf4, cf4, pc);
          fflush(stdout);
          l0 = c0; lf4 = cf4;
      } }
    { /* New-game copy-loop entry snapshot (first 3 hits of $45b0 past f13000):
       * MMR map + which bank $DFF4 REALLY resolves to + its value + X/zp pointer
       * + zero-map of all 32 CD RAM banks. Settles whether the garbage block
       * count comes from a bank the transfer left empty, and WHICH bank that is
       * (the earlier "bank 0x7f" attribution assumed MMR[6]=0x7f — verify it). */
      static int ng_hits = 0;
      if (ng_hits < 8 && pc == 0x45b0) {
          ng_hits++;
          printf("[ng] hit%d f%d pc=45b0 X=%02x A=%02x Y=%02x P=%02x MMR:", ng_hits,
                 g_frame_now, CPU_PCE.X, CPU_PCE.A, CPU_PCE.Y, CPU_PCE.P);
          for (int b = 0; b < 8; b++) printf(" %d:%02x", b, PCE.MMR[b]);
          printf("\n[ng]   $DFF4 -> bank %02x off 1ff4 = %02x; zp $24-$26 = %02x %02x %02x\n",
                 PCE.MMR[6], PCE.MemoryMapR[PCE.MMR[6]][0x1ff4],
                 PCE.RAM[0x24], PCE.RAM[0x25], PCE.RAM[0x26]);
          { extern uint8_t *PageR[8];
            printf("[ng]   fetch 45b0..b2 = %02x %02x %02x (JSR -> $%02x%02x)\n",
                   PageR[2][0x45b0], PageR[2][0x45b1], PageR[2][0x45b2],
                   PageR[2][0x45b2], PageR[2][0x45b1]); }
          printf("[ng]   CD-RAM zero-map (68..87):");
          for (int v = 0x68; v <= 0x87; v++) {
              uint32_t s = 0;
              for (int k = 0; k < 0x2000; k++) s += PCE.MemoryMapR[v][k];
              printf(" %02x:%c", v, s ? '+' : '0');
          }
          printf("\n"); fflush(stdout);
      } }
    { /* watch the self-modified JSR operand at $45b1-$45b2 (bank 0x69 phys
       * 0x5b1/0x5b2): the death is `45b0: JSR $0431` — a garbage patch. Log every
       * change with the writer pc + regs; the last change before death is the
       * culprit instruction. */
      static uint8_t jl = 0xAA, jh = 0xAA;
      uint8_t cl = PCE.MemoryMapR[0x69][0x05b1], ch = PCE.MemoryMapR[0x69][0x05b2];
      if (cl != jl || ch != jh) {
          printf("[wpjsr] f%d JSR opnd %02x%02x -> %02x%02x at pc=%04x A=%02x X=%02x Y=%02x\n",
                 g_frame_now, jh, jl, ch, cl, pc, CPU_PCE.A, CPU_PCE.X, CPU_PCE.Y);
          fflush(stdout);
          jl = cl; jh = ch;
      } }
    { /* I-flag transitions: who enables interrupts before the vectorless
       * decompressor runs? (real handoff is I-set; the last clear before f792
       * is the gate that lets the fatal IRQ through) */
      static int li = -1, icnt = 0;
      int ci = (CPU_PCE.P & 0x04) ? 1 : 0;
      if (ci != li && icnt < 60) {
          icnt++;
          printf("[iflag] f%d I=%d at pc=%04x P=%02x\n", g_frame_now, ci, pc, CPU_PCE.P);
          fflush(stdout);
      }
      li = ci; }
    { /* MMR[7] (vector page) remap history — who mapped CD RAM 0x68 into the
       * vector page, and does anyone ever restore the System Card there? */
      static int lm7 = -1, mcnt = 0;
      if ((int)PCE.MMR[7] != lm7 && mcnt < 40) {
          mcnt++;
          printf("[mmr7] f%d MMR7 %02x -> %02x at pc=%04x\n",
                 g_frame_now, lm7 < 0 ? 0 : lm7, PCE.MMR[7], pc);
          fflush(stdout);
      }
      lm7 = PCE.MMR[7]; }
    { /* PSG driver forensics: who calls $c000 (driver START, re-arms the fatal
       * 7kHz timer) — print the JSR return addr off the stack; plus every write
       * to zp $e7 (driver-active flag) and $f8 (clock mode: 0=TIMER 1=VSYNC). */
      static int c0cnt = 0;
      if (pc == 0xc000 && c0cnt < 12) {
          c0cnt++;
          uint8_t sp = CPU_PCE.S;
          uint16_t ret = (uint16_t)(PCE.RAM[0x100 + ((sp + 1) & 0xff)]
                       | (PCE.RAM[0x100 + ((sp + 2) & 0xff)] << 8));
          printf("[psg] f%d c000 START called from %04x (JSR at %04x) e7=%02x f8=%02x ff=%02x MMR4=%02x MMR5=%02x MMR7=%02x\n",
                 g_frame_now, ret, (uint16_t)(ret - 2), PCE.RAM[0xe7], PCE.RAM[0xf8],
                 PCE.RAM[0xff], PCE.MMR[4], PCE.MMR[5], PCE.MMR[7]);
          printf("[psg]   stack S=%02x:", sp);
          for (int k = 1; k <= 10; k++) printf(" %02x", PCE.RAM[0x100 + ((sp + k) & 0xff)]);
          printf("\n");
          fflush(stdout);
      }
      static int lc19 = 0;
      if (pc == 0xc019 && lc19 < 8) {
          lc19++;
          printf("[psg] f%d c019 STOP entered e7=%02x\n", g_frame_now, PCE.RAM[0xe7]);
          fflush(stdout);
      }
      static uint8_t le7 = 0xAA, lf8 = 0xAA; static int ecnt = 0;
      /* silent ring of the last 8 $f8 writers — printed at the c000 call so we
       * can see whether the game's mode argument was clobbered en route */
      static struct { uint16_t pc; uint8_t v; int f; } f8ring[8]; static int f8i = 0;
      if (PCE.RAM[0xf8] != lf8) {
          f8ring[f8i % 8].pc = pc; f8ring[f8i % 8].v = PCE.RAM[0xf8];
          f8ring[f8i % 8].f = g_frame_now; f8i++;
      }
      if (pc == 0xc000 && f8i) {
          printf("[psg]   last $f8 writers:");
          for (int k = 0; k < 8 && k < f8i; k++) {
              int idx = ((f8i - 1 - k) % 8 + 8) % 8;
              printf(" f%d:%04x=%02x", f8ring[idx].f, f8ring[idx].pc, f8ring[idx].v);
          }
          printf("\n");
          printf("[psg]   caller code bank68[$1f0..$23f] ($81f0..):");
          for (int k = 0; k < 0x50; k++) {
              if ((k % 16) == 0) printf("\n[psg]   %04x:", 0x81f0 + k);
              printf(" %02x", PCE.MemoryMapR[0x68][0x1f0 + k]);
          }
          printf("\n"); fflush(stdout);
      }
      if ((PCE.RAM[0xe7] != le7 || PCE.RAM[0xf8] != lf8) && ecnt < 40) {
          ecnt++;
          printf("[psg] f%d zp e7 %02x->%02x f8 %02x->%02x near pc=%04x\n",
                 g_frame_now, le7, PCE.RAM[0xe7], lf8, PCE.RAM[0xf8], pc);
          fflush(stdout);
      }
      le7 = PCE.RAM[0xe7]; lf8 = PCE.RAM[0xf8]; }
    { /* dest-bank walk during the New-game 256KB store: MMR4/MMR5 history */
      static int lm4 = -1, lm5 = -1, mc45 = 0;
      if ((PCE.MMR[4] != lm4 || PCE.MMR[5] != lm5) && mc45 < 140 && g_frame_now >= 770) {
          mc45++;
          printf("[mmr45] f%d MMR4 %02x->%02x MMR5 %02x->%02x at pc=%04x\n",
                 g_frame_now, lm4 < 0 ? 0 : lm4, PCE.MMR[4],
                 lm5 < 0 ? 0 : lm5, PCE.MMR[5], pc);
          fflush(stdout);
      }
      lm4 = PCE.MMR[4]; lm5 = PCE.MMR[5]; }
    { /* trampoline forensics: P (I-flag) at each MMR7 toggle edge + one dump of
       * the WRAM trampoline code itself ($3c40-$3c9f). If the real code SEIs
       * around the bank68 window, an IRQ landing inside it means OUR I-flag or
       * dispatch timing diverges; if it doesn't SEI, the game relies on the
       * timer being off/handled elsewhere. */
      static int tcnt = 0, tdump = 0;
      if ((pc == 0x3c5c || pc == 0x3c65) && tcnt < 24) {
          tcnt++;
          printf("[tramp] f%d pc=%04x P=%02x (I=%d)\n", g_frame_now, pc, CPU_PCE.P,
                 (CPU_PCE.P & 0x04) ? 1 : 0);
          fflush(stdout);
      }
      static int cdump = 0;
      if (!cdump && g_frame_now == 786) {
          cdump = 1;
          printf("[code] bank69[$560..$60f] ($4560..):");
          for (int k = 0; k < 0xb0; k++) {
              if ((k % 16) == 0) printf("\n[code] %04x:", 0x4560 + k);
              printf(" %02x", PCE.MemoryMapR[0x69][0x560 + k]);
          }
          printf("\n[code] bank69[$700..$77f] ($4700..):");
          for (int k = 0; k < 0x80; k++) {
              if ((k % 16) == 0) printf("\n[code] %04x:", 0x4700 + k);
              printf(" %02x", PCE.MemoryMapR[0x69][0x700 + k]);
          }
          printf("\n"); fflush(stdout);
      }
      if (!tdump && g_frame_now == 790) {
          tdump = 1;
          printf("[tramp] WRAM $3c40-$3c9f:");
          for (int k = 0; k < 0x60; k++) {
              if ((k % 16) == 0) printf("\n[tramp] $%04x:", 0x3c40 + k);
              printf(" %02x", PCE.RAM[0x1c40 + k]);
          }
          printf("\n"); fflush(stdout);
      } }
    { /* gate-2 probes: (a) the block-count store at $459a — A = the count byte
       * just read from the decompression stream via $4790, with the stream
       * pointer (zp $46/47) and source banks; (b) decompression entry $458b —
       * stream pointer start + CD-RAM zero-map = raw-store coverage. */
      static int c459a = 0, c458b = 0;
      if (pc == 0x459a && c459a < 6) {
          c459a++;
          printf("[g2] f%d COUNT@459a A=%02x stream $%02x%02x MMR4=%02x MMR5=%02x MMR6=%02x\n",
                 g_frame_now, CPU_PCE.A, PCE.RAM[0x47], PCE.RAM[0x46],
                 PCE.MMR[4], PCE.MMR[5], PCE.MMR[6]);
          fflush(stdout);
      }
      /* who fills the WRAM compressed-stream buffer at $35b2+, and who points
       * zp $46/47 at it — the count byte 0xF7 is read from ~$35d4 */
      static uint8_t l35 = 0xAA; static int c35 = 0;
      if (PCE.RAM[0x15b2] != l35 && c35 < 8) {
          c35++;
          printf("[g2] f%d WRAM $35b2 %02x->%02x near pc=%04x\n",
                 g_frame_now, l35, PCE.RAM[0x15b2], pc);
          fflush(stdout);
      }
      l35 = PCE.RAM[0x15b2];
      static uint8_t l47 = 0xAA; static int c47 = 0;
      { uint8_t h = PCE.RAM[0x47];
        int d = (int)h - (int)l47;
        if (h != l47 && (d < 0 || d > 1) && c47 < 12) {
            c47++;
            printf("[g2] f%d zp$47 %02x->%02x (ptr $%02x%02x) at pc=%04x\n",
                   g_frame_now, l47, h, h, PCE.RAM[0x46], pc);
            fflush(stdout);
        }
        l47 = h; }
      /* stream-pointer setter at $477d (STA $4a / STX $46 / STY $47): who calls
       * it and with what — the fatal call passes $35b2 (WRAM variables!) */
      static int c477d = 0;
      if (pc == 0x477d && c477d < 10) {
          c477d++;
          uint8_t sp2 = CPU_PCE.S;
          uint16_t ret2 = (uint16_t)(PCE.RAM[0x100 + ((sp2 + 1) & 0xff)]
                        | (PCE.RAM[0x100 + ((sp2 + 2) & 0xff)] << 8));
          printf("[g2] f%d SETPTR@477d A=%02x X=%02x Y=%02x caller=%04x MMR2=%02x MMR4=%02x\n",
                 g_frame_now, CPU_PCE.A, CPU_PCE.X, CPU_PCE.Y,
                 (uint16_t)(ret2 - 2), PCE.MMR[2], PCE.MMR[4]);
          fflush(stdout);
      }
      /* stream REFILL at $47b3: the game CD-READs one unit into WRAM $3500 via
       * the syscard param block (zp $f8-$ff) + BIOS fn X=$36. Log entry params
       * and the staging content on return — no SCSI READ ever shows in diag. */
      static int c47b3 = 0;
      if (pc == 0x47d1 && c47b3 < 6) {
          c47b3++;
          { uint8_t *ar = pce_adpcm_ram();
            printf("[g2]   ADPCM-RAM[35f8..3618]:");
            for (int k = 0x35f8; k < 0x3618; k++) printf(" %02x", ar[k]);
            printf("\n[g2]   ADPCM-RAM[0000..000f]:");
            for (int k = 0; k < 16; k++) printf(" %02x", ar[k]);
            printf("\n"); }
          printf("[g2] f%d REFILL-CALL $4a=%02x params f8=%02x f9=%02x fa=%02x fb=%02x fc=%02x fd=%02x fe=%02x ff=%02x\n",
                 g_frame_now, PCE.RAM[0x4a], PCE.RAM[0xf8], PCE.RAM[0xf9],
                 PCE.RAM[0xfa], PCE.RAM[0xfb], PCE.RAM[0xfc], PCE.RAM[0xfd],
                 PCE.RAM[0xfe], PCE.RAM[0xff]);
          fflush(stdout);
      }
      static int c47d4 = 0;
      if (pc == 0x47d4 && c47d4 < 6) {
          c47d4++;
          printf("[g2] f%d REFILL-RET A=%02x $3500-0f:", g_frame_now, CPU_PCE.A);
          for (int k = 0; k < 16; k++) printf(" %02x", PCE.RAM[0x1500 + k]);
          printf("\n"); fflush(stdout);
      }
      if (pc == 0x458b && c458b < 4) {
          c458b++;
          printf("[g2] f%d DECOMP-ENTRY@458b stream $%02x%02x MMR4=%02x MMR5=%02x zero-map:",
                 g_frame_now, PCE.RAM[0x47], PCE.RAM[0x46], PCE.MMR[4], PCE.MMR[5]);
          for (int v = 0x68; v <= 0x87; v++) {
              uint32_t s7 = 0;
              for (int k = 0; k < 0x2000; k++) s7 += PCE.MemoryMapR[v][k];
              printf(" %02x:%c", v, s7 ? '+' : '0');
          }
          printf("\n"); fflush(stdout);
      } }
    { /* entries into the BRK death loop: stamp each transition (prev!=4dff) */
      static int deaths = 0;
      static uint16_t prev_pc2 = 0;
      /* silent ring of the last 12 I-flag transitions; printed at death so we
       * can see WHO last enabled interrupts before the fatal window */
      static struct { int f; uint16_t pc; uint8_t p; } iring[12]; static int ii = 0;
      static int lastI = -1;
      { int curI = (CPU_PCE.P & 0x04) ? 1 : 0;
        if (curI != lastI) {
            iring[ii % 12].f = g_frame_now; iring[ii % 12].pc = pc;
            iring[ii % 12].p = CPU_PCE.P; ii++; lastI = curI;
        } }
      if (deaths < 10 && pc == 0x4dff && prev_pc2 != 0x4dff) {
          deaths++;
          extern uint8_t *PageR[8];
          printf("[iring] last I-flag transitions (newest first):\n");
          for (int k = 0; k < 12 && k < ii; k++) {
              int idx = ((ii - 1 - k) % 12 + 12) % 12;
              printf("[iring]  f%d pc=%04x P=%02x I=%d\n", iring[idx].f,
                     iring[idx].pc, iring[idx].p, (iring[idx].p & 4) ? 1 : 0);
          }
          printf("[code] decompressor bank69[$780..$84f] ($4780..):");
          for (int k = 0; k < 0xd0; k++) {
              if ((k % 16) == 0) printf("\n[code] %04x:", 0x4780 + k);
              printf(" %02x", PCE.MemoryMapR[0x69][0x780 + k]);
          }
          printf("\n[code] WRAM $3d10-$3d6f:");
          for (int k = 0; k < 0x60; k++) {
              if ((k % 16) == 0) printf("\n[code] %04x:", 0x3d10 + k);
              printf(" %02x", PCE.RAM[0x1d10 + k]);
          }
          printf("\n[code] hook table $2200-$220f:");
          for (int k = 0; k < 16; k++) printf(" %02x", PCE.RAM[0x200 + k]);
          printf("\n");
          printf("[ng] ENTER $4dff #%d at f%d from pc=%04x\n", deaths, g_frame_now, prev_pc2);
          printf("[ng]   fetch-view 45b0..b2 = %02x %02x %02x | MemR[69][5b0..2] = %02x %02x %02x MMR2=%02x\n",
                 PageR[2][0x45b0], PageR[2][0x45b1], PageR[2][0x45b2],
                 PCE.MemoryMapR[0x69][0x5b0], PCE.MemoryMapR[0x69][0x5b1],
                 PCE.MemoryMapR[0x69][0x5b2], PCE.MMR[2]);
          { uint8_t *pr2 = PageR[2] + 2 * 0x2000;
            printf("[ng]   PageR[2]+bias=%p MemoryMapR[MMR2]=%p", (void *)pr2,
                   (void *)PCE.MemoryMapR[PCE.MMR[2]]);
            for (int v = 0; v < 256; v++)
                if (PCE.MemoryMapR[v] == pr2) printf(" -> PageR[2] IS bank %02x", v);
            printf("\n"); }
          fflush(stdout);
      }
      prev_pc2 = pc; }
    { static int lr = 0;
      if ((int)PCE.Timer.running != lr) {
          printf("[tick] Timer.running %d->%d at pc=%04x (reload=%d) code:",
                 lr, (int)PCE.Timer.running, pc, (int)PCE.Timer.reload);
          { extern uint8_t *PageR[8];
            for (int k = -8; k < 8; k++)
                printf(" %02x", PageR[((uint16_t)(pc+k)) >> 13][(uint16_t)(pc + k)]);
            printf(" MMR6=%02x\n", PCE.MMR[6]); }
          fflush(stdout);
          lr = (int)PCE.Timer.running;
      } }
    if (!started) { started = 1; }   /* arm immediately: entry differs per game */

    /* Ring buffer of the last 256 PCs. When the game FIRST reaches its hang loop
     * ($6257 = JMP self), dump the ring so we see EXACTLY the path (and the
     * conditional branch) that diverted into the halt — the decisive evidence. */
    #define RING 256
    extern uint16_t g_ring[]; extern int g_ridx;   /* file-scope: main loop dumps at f14000 */
    #define ring g_ring
    #define ridx g_ridx
    static int ring_done = 0;
    ring[ridx++ % RING] = pc;
    if (!ring_done && pc == 0x6257 && ridx > 1) {
        ring_done = 1;
        printf("[host] ===== PATH INTO $6257 HANG (last %d PCs) =====\n", RING);
        int start = ridx % RING;
        for (int k = 0; k < RING; k++) {
            int i = (start + k) % RING;
            printf("%04x ", ring[i]);
            if ((k % 16) == 15) printf("\n");
        }
        printf("\n[host] ===== END PATH =====\n");
        fflush(stdout);
    }
    /* Print the FIRST 16 instructions with the EXACT opcode the CPU fetches
     * (PageR[pc>>13][pc], same expression as imm_operand) vs pce_read8, plus the
     * bank mapping for that page — to settle the trace-vs-dump contradiction. */
    if (dumped_entry < 80) {
        dumped_entry++;
        extern uint8_t *PageR[8];
        uint8_t opc_raw = PageR[pc >> 13][pc];     /* what the CPU executes */
        uint8_t opc_r8  = pce_read8(pc);
        printf("[host] #%02d pc=%04x op(raw)=%02x op(r8)=%02x MMR[%d]=%02x A=%02x X=%02x Y=%02x S=%02x P=%02x\n",
               dumped_entry, pc, opc_raw, opc_r8, pc >> 13, PCE.MMR[pc >> 13],
               CPU_PCE.A, CPU_PCE.X, CPU_PCE.Y, CPU_PCE.S, CPU_PCE.P);
        fflush(stdout);
    }
    (void)dumped_idle;

    /* Game-space-only ring (pc < $E000 = not System Card ROM): records the last
     * GREC game instructions with regs/flags, dumped when the hang ($6257) is
     * first reached — so we see the game's OWN decision logic + the branch that
     * chose the dead-end, with all the BIOS noise filtered out. */
    #define GREC 200
    static struct { uint16_t pc, o; uint8_t a, x, y, p, m2, m7; } gr[GREC];
    static int gi = 0, gdone = 0;
    if (pc < 0xE000) {
        extern uint8_t *PageR[8];
        int s = gi % GREC;
        gr[s].pc = pc; gr[s].o = PageR[pc >> 13][pc];
        gr[s].a = CPU_PCE.A; gr[s].x = CPU_PCE.X; gr[s].y = CPU_PCE.Y; gr[s].p = CPU_PCE.P;
        gr[s].m2 = PCE.MMR[2]; gr[s].m7 = PCE.MMR[7];
        gi++;
    }
    static int dumped_4dff = 0;
    if (!dumped_4dff && pc == 0x4dff) {
        dumped_4dff = 1;
        { extern int g_frame_now; printf("[host] first $4dff at frame %d\n", g_frame_now); }
        printf("[host] ===== LAST 40 instrs (newest first) before/at $4dff =====\n");
        for (int k = 0; k < 40 && k < gi; k++) {
            int i = ((gi - 1 - k) % GREC + GREC) % GREC;
            printf("[-%02d] %04x:%02x A%02x X%02x Y%02x P%02x M2=%02x M7=%02x\n",
                   k, gr[i].pc, gr[i].o, gr[i].a, gr[i].x, gr[i].y, gr[i].p, gr[i].m2, gr[i].m7);
        }
        printf("[host] ===== END =====\n");
        fflush(stdout);
    }
    /* Targeted trace of the decisive BIOS call JSR $E0DE @ $6050 — whose returned
     * Carry decides hang ($6219) vs continue ($6058). Arm when we first reach the
     * $6050 call site; log every instruction (incl. System Card) until it returns
     * to $6053, so we see what $E0DE checks and where Carry gets set. */
    static int e0de = 0, e0de_arm = 0;
    if (!e0de_arm && pc == 0x6050) e0de_arm = 1;
    if (e0de_arm && e0de < 300) {
        e0de++;
        extern uint8_t *PageR[8];
        uint8_t o0 = PageR[pc >> 13][pc];
        printf("[host] E %04x:%02x A=%02x X=%02x Y=%02x P=%02x [C%d]\n",
               pc, o0, CPU_PCE.A, CPU_PCE.X, CPU_PCE.Y, CPU_PCE.P, CPU_PCE.P & 1);
        if (pc == 0x6053) e0de = 999;   /* stop after return */
        fflush(stdout);
    }

    if (!gdone && gi == 30000) {
        gdone = 1;
        printf("[host] ===== STEADY-STATE: last %d GAME INSTRS @ gi=30000 =====\n", GREC);
        int n = gi < GREC ? gi : GREC;
        int start = gi < GREC ? 0 : (gi % GREC);
        for (int k = 0; k < n; k++) {
            int i = (start + k) % GREC;
            printf("%04x:%02x[A%02x X%02x Y%02x P%02x] ", gr[i].pc, gr[i].o,
                   gr[i].a, gr[i].x, gr[i].y, gr[i].p);
            if ((k % 6) == 5) printf("\n");
        }
        printf("\n[host] ===== END GAME RING =====\n");
        fflush(stdout);
    }
    if (n >= 200000) return;
    if (!tf) tf = fopen("pc_trace.txt", "w");
    if (!tf) return;
    /* Run-length compress: idle loops (e.g. 6257 6257 ...) collapse to one line
     * "6257 xN" so the actual control flow / init sequence is readable. */
    if (pc == last_pc) { run++; return; }
    if (run > 0) { fprintf(tf, "x%d\n", run + 1); run = 0; }
    last_pc = pc;
    fprintf(tf, "%04x ", pc);
    n++;
    if (n == 200000) { fprintf(tf, "...cap\n"); fflush(tf); }
}

#undef printf
#define APP_ID 20

#define JOY_A       0x01
#define JOY_B       0x02
#define JOY_SELECT  0x04
#define JOY_RUN     0x08
#define JOY_UP      0x10
#define JOY_RIGHT   0x20
#define JOY_DOWN    0x40
#define JOY_LEFT    0x80

#define NVS_KEY_SAVE_SRAM "sram"

/* Mednafen nominal PCE viewport (see main_play.c). */
#define WIDTH    320
#define HEIGHT   240
#define BPP      2
#define SCALE    2

typedef uint16_t pixel_t;
static uint16_t mypalette[256];
#define COLOR_RGB(r, g, b) ((((r) << 13) & 0xf800) + (((g) << 8) & 0x07e0) + (((b) << 3) & 0x001f))


#define AUDIO_SAMPLE_RATE   (48000)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60)

// Use 60Hz for GB
#define AUDIO_BUFFER_LENGTH_GB (AUDIO_SAMPLE_RATE / 60)
#define AUDIO_BUFFER_LENGTH_DMA_GB ((2 * AUDIO_SAMPLE_RATE) / 60)

#define FB_INTERNAL_OFFSET_X  (((XBUF_WIDTH - current_width) / 2) > 0 ? ((XBUF_WIDTH - current_width) / 2) : 0)
#define FB_INTERNAL_OFFSET    (((XBUF_HEIGHT - current_height) / 2 + 16) * XBUF_WIDTH + FB_INTERNAL_OFFSET_X)
static uint8_t emulator_framebuffer_pce[XBUF_WIDTH * XBUF_HEIGHT];


static odroid_video_frame_t update1 = {WIDTH, HEIGHT, WIDTH * 2, 2, 0xFF, -1, NULL, NULL, 0, {}};
static odroid_video_frame_t update2 = {WIDTH, HEIGHT, WIDTH * 2, 2, 0xFF, -1, NULL, NULL, 0, {}};

static bool saveSRAM = false;
int g_load_frame;   /* frame of the most recent state load (post-load stuck-probe) */

// 3 pages
uint8_t state_save_buffer[192 * 1024];

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *fb_texture;
uint16_t fb_data[WIDTH * HEIGHT * BPP];

SDL_AudioSpec wanted;
void fill_audio(void *udata, Uint8 *stream, int len);

static uint8_t PCE_EXRAM_BUF[0x8000];
static int framePerSecond=0;

static int current_height, current_width;
#define PCE_SAMPLE_RATE   (22050)
#define AUDIO_BUFFER_LENGTH_PCE  (PCE_SAMPLE_RATE / 60)
//static short audioBuffer_pce[ AUDIO_BUFFER_LENGTH_PCE * 2];

//The frames per second cap timer
int capTimer;

/**
 * Describes what is saved in a save state. Changing the order will break
 * previous saves so add a place holder if necessary. Eventually we could use
 * the keys to make order irrelevant...
 */
#define SVAR_1(k, v) { 1, k, &v }
#define SVAR_2(k, v) { 2, k, &v }
#define SVAR_4(k, v) { 4, k, &v }
#define SVAR_A(k, v) { sizeof(v), k, &v }
#define SVAR_N(k, v, n) { n, k, &v }
#define SVAR_END { 0, "\0\0\0\0", 0 }

static const char SAVESTATE_HEADER[8] = "PCE_V007";
static const struct
{
	size_t len;
	char key[16];
	void *ptr;
} SaveStateVars[] =
{
	// Arrays
	SVAR_A("RAM", PCE.RAM),      SVAR_A("VRAM", PCE.VRAM),  SVAR_A("SPRAM", PCE.SPRAM),
	SVAR_A("PAL", PCE.Palette),  SVAR_A("MMR", PCE.MMR),

	// CPU registers
	SVAR_2("CPU.PC", CPU_PCE.PC),    SVAR_1("CPU.A", CPU_PCE.A),    SVAR_1("CPU.X", CPU_PCE.X),
	SVAR_1("CPU.Y", CPU_PCE.Y),      SVAR_1("CPU.P", CPU_PCE.P),    SVAR_1("CPU.S", CPU_PCE.S),

	// Misc
	SVAR_4("Cycles", Cycles),                   SVAR_4("MaxCycles", PCE.MaxCycles),
	SVAR_1("SF2", PCE.SF2),                     SVAR_2("VBlankFL", PCE.VBlankFL),

	// IRQ
	SVAR_1("irq_mask", CPU_PCE.irq_mask),           SVAR_1("irq_mask_delay", CPU_PCE.irq_mask_delay),
	SVAR_1("irq_lines", CPU_PCE.irq_lines),

	// PSG
	SVAR_1("psg.ch", PCE.PSG.ch),               SVAR_1("psg.vol", PCE.PSG.volume),
	SVAR_1("psg.lfo_f", PCE.PSG.lfo_freq),      SVAR_1("psg.lfo_c", PCE.PSG.lfo_ctrl),
	SVAR_N("psg.ch0", PCE.PSG.chan[0], 40),     SVAR_N("psg.ch1", PCE.PSG.chan[1], 40),
	SVAR_N("psg.ch2", PCE.PSG.chan[2], 40),     SVAR_N("psg.ch3", PCE.PSG.chan[3], 40),
	SVAR_N("psg.ch4", PCE.PSG.chan[4], 40),     SVAR_N("psg.ch5", PCE.PSG.chan[5], 40),

	// VCE
    SVAR_1("vce_cr", PCE.VCE.CR),               SVAR_1("vce_dot_clock", PCE.VCE.dot_clock),
	SVAR_A("vce_regs", PCE.VCE.regs),           SVAR_2("vce_reg", PCE.VCE.reg),

	// VDC
	SVAR_A("vdc_regs", PCE.VDC.regs),           SVAR_1("vdc_reg", PCE.VDC.reg),
	SVAR_1("vdc_status", PCE.VDC.status),       SVAR_1("vdc_vram", PCE.VDC.vram),
	SVAR_1("vdc_satb", PCE.VDC.satb),			SVAR_4("vdc_pen_irqs", PCE.VDC.pending_irqs),

	// Timer
	SVAR_1("timer_reload", PCE.Timer.reload),   SVAR_1("timer_running", PCE.Timer.running),
	SVAR_1("timer_counter", PCE.Timer.counter), SVAR_4("timer_next", PCE.Timer.cycles_counter),
	SVAR_2("timer_freq", PCE.Timer.cycles_per_line),

	SVAR_END
};

void set_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    uint16_t col = 0xffff;
    if (index != 255)  {
        col = COLOR_RGB(r,g,b);
    }
    mypalette[index] = col;
}

void init_color_pals() {
    printf("init_color_pals()\n");

    for (int i = 0; i < 255; i++) {
        // GGGRR RBB
          set_color(i, (i & 0x1C)>>2, (i & 0xE0) >> 5, (i & 0x03) );
    }
    set_color(255, 0x3f, 0x3f, 0x3f);
}

void odroid_display_force_refresh(void)
{
    // forceVideoRefresh = true;
}


void fill_audio(void *udata, Uint8 *stream, int len)
{

}

static void pce_fb_clear(void)
{
    memset(emulator_framebuffer_pce, PCE.Palette[0], sizeof(emulator_framebuffer_pce));
}

static inline void pce_norm_vdc_size(int *w, int *h)
{
    if (*w < 8) *w = 8;
    if (*w > 512) *w = 512;
    if (*h < 8) *h = 8;
    if (*h > 256) *h = 256;
}

static void pce_display_size(int *w, int *h)
{
    *w = IO_VDC_SCREEN_WIDTH;
    *h = IO_VDC_SCREEN_HEIGHT;
    pce_norm_vdc_size(w, h);
}

static int pce_fb_offset(void)
{
    int w = IO_VDC_SCREEN_WIDTH;
    int h = IO_VDC_SCREEN_HEIGHT;
    pce_norm_vdc_size(&w, &h);
    int offx = (XBUF_WIDTH - w) / 2;
    if (offx < 0) offx = 0;
    return ((XBUF_HEIGHT - h) / 2 + 16) * XBUF_WIDTH + offx;
}

uint8_t *osd_gfx_framebuffer(void){
    return emulator_framebuffer_pce + pce_fb_offset();
}

void osd_gfx_set_mode(int width, int height) {
	init_color_pals();
    pce_norm_vdc_size(&width, &height);
    if (width != current_width || height != current_height) {
        pce_fb_clear();
        gfx_reset(false);
    }
    current_width = width;
    current_height = height;
}

void pce_input_read(odroid_gamepad_state_t* out_state) {
    unsigned char rc = 0;
    if (out_state->values[ODROID_INPUT_LEFT])   rc |= JOY_LEFT;
    if (out_state->values[ODROID_INPUT_RIGHT])  rc |= JOY_RIGHT;
    if (out_state->values[ODROID_INPUT_UP])     rc |= JOY_UP;
    if (out_state->values[ODROID_INPUT_DOWN])   rc |= JOY_DOWN;
    if (out_state->values[ODROID_INPUT_A])      rc |= JOY_A;
    if (out_state->values[ODROID_INPUT_B])      rc |= JOY_B;
    if (out_state->values[ODROID_INPUT_START])  rc |= JOY_RUN;
    if (out_state->values[ODROID_INPUT_SELECT]) rc |= JOY_SELECT;
    PCE.Joypad.regs[0] = rc;
}

int init_window(int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
        return 0;

    window = SDL_CreateWindow("emulator",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width * SCALE, height * SCALE,
        0);
    if (!window)
        return 0;

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
        return 0;

    fb_texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        width, height);
    if (!fb_texture)
        return 0;

    return 0;
}

static bool host_LoadState(const char *savePathName)
{
    printf("Loading state from %s...\n", savePathName);

	char buffer[512];

	FILE *fp = fopen(savePathName, "rb");
	if (fp == NULL)
		return false;

	fread(&buffer, 8, 1, fp);

	if (memcmp(&buffer, SAVESTATE_HEADER, 8) != 0)
	{
		MESSAGE_ERROR("Loading state failed: Header mismatch\n");
		fclose(fp);
		return false;
	}

	for (int i = 0; SaveStateVars[i].len > 0; i++)
	{
		printf("Loading %s (%d)\n", SaveStateVars[i].key, SaveStateVars[i].len);
		fread(SaveStateVars[i].ptr, SaveStateVars[i].len, 1, fp);
	}

	/* PCE-CD: restore the 256KB CD RAM streamed after the core state. */
	if (g_cue_path) {
		for (int v = 0x68; v <= 0x87; v++)
			fread(PCE.MemoryMapW[v], 0x2000, 1, fp);
		pce_scsi_reset();   /* mirror the device LoadState: SCSI back to idle */
		uint32_t cdda[1 + PCE_SCSI_CDDA_STATE_WORDS];
		if (fread(cdda, sizeof(cdda), 1, fp) == 1 && cdda[0] == 0x41444443u)
			pce_scsi_cdda_set(cdda + 1);
		/* ADPCM engine + 64KB RAM block (mirrors device LoadState). */
#ifdef PCE_ADPCM_STATE_WORDS
		uint32_t adpc[1 + PCE_ADPCM_STATE_WORDS];
		if (fread(adpc, sizeof(adpc), 1, fp) == 1 && adpc[0] == 0x43504441u) {
			fread(pce_adpcm_ram(), 1, 0x10000, fp);
			pce_adpcm_set(adpc + 1);
		} else {
			pce_adpcm_reset();
		}
		{ uint32_t scsx = 0; static pce_scsi_state_t sst;
		  if (fread(&scsx, sizeof(scsx), 1, fp) == 1 && scsx == 0x58534353u &&
		      fread(&sst, sizeof(sst), 1, fp) == 1)
		      pce_scsi_state_set(&sst); }
#ifdef PCE_ENABLE_ARCADE_CARD
		{ long arc_pos = ftell(fp); uint32_t arcd = 0;
		  static pce_arcade_card_state_t acst;
		  if (fread(&arcd, sizeof(arcd), 1, fp) == 1 && arcd == PCE_ARCADE_CARD_STATE_MAGIC &&
		      fread(&acst, sizeof(acst), 1, fp) == 1) {
		      pce_arcade_card_state_set(&acst);
		      if (acst.ram_used && pce_arcade_card_ram())
		          fread(pce_arcade_card_ram(), 1, 0x200000, fp);
		  } else if (arc_pos >= 0)
		      fseek(fp, arc_pos, SEEK_SET);
		}
#endif
	}

	for(int i = 0; i < 8; i++)
	{
		pce_bank_set(i, PCE.MMR[i]);
	}

	gfx_reset(true);

	osd_gfx_set_mode(IO_VDC_SCREEN_WIDTH, IO_VDC_SCREEN_HEIGHT);
	pce_fb_clear();

	fclose(fp);

	printf("Loaded state: %s\n", savePathName);
	return true;
}

static bool host_SaveState(const char *savePathName)
{
    printf("Saving state to %s...\n", savePathName);

	FILE *fp = fopen(savePathName, "wb");
	if (fp == NULL)
		return false;

	fwrite(SAVESTATE_HEADER, sizeof(SAVESTATE_HEADER), 1, fp);

	for (int i = 0; SaveStateVars[i].len > 0; i++)
	{
		printf("Saving %s (%d)\n", SaveStateVars[i].key, SaveStateVars[i].len);
		fwrite(SaveStateVars[i].ptr, SaveStateVars[i].len, 1, fp);
	}

	/* PCE-CD: stream the 256KB CD RAM (banks 0x68-0x87) after the core state,
	 * mirroring the device SaveState. */
	if (g_cue_path) {
		for (int v = 0x68; v <= 0x87; v++)
			fwrite(PCE.MemoryMapR[v], 0x2000, 1, fp);
		uint32_t cdda[1 + PCE_SCSI_CDDA_STATE_WORDS] = { 0x41444443u /* 'CDDA' */ };
		pce_scsi_cdda_get(cdda + 1);
		fwrite(cdda, sizeof(cdda), 1, fp);
		/* ADPCM engine + 64KB RAM block (mirrors device SaveState). */
#ifdef PCE_ADPCM_STATE_WORDS
		uint32_t adpc[1 + PCE_ADPCM_STATE_WORDS] = { 0x43504441u /* 'ADPC' */ };
		pce_adpcm_get(adpc + 1);
		fwrite(adpc, sizeof(adpc), 1, fp);
		fwrite(pce_adpcm_ram(), 1, 0x10000, fp);
		/* Full SCSI engine (in-flight transfer) — mirrors device SaveState. */
		{ uint32_t scsx = 0x58534353u; static pce_scsi_state_t sst;
		  pce_scsi_state_get(&sst);
		  fwrite(&scsx, sizeof(scsx), 1, fp);
		  fwrite(&sst, sizeof(sst), 1, fp); }
#ifdef PCE_ENABLE_ARCADE_CARD
		{ uint32_t arcd = PCE_ARCADE_CARD_STATE_MAGIC;
		  static pce_arcade_card_state_t acst;
		  pce_arcade_card_state_get(&acst);
		  fwrite(&arcd, sizeof(arcd), 1, fp);
		  fwrite(&acst, sizeof(acst), 1, fp);
		  if (acst.ram_used && pce_arcade_card_ram())
		      fwrite(pce_arcade_card_ram(), 1, 0x200000, fp);
		}
#endif
#endif
	}

	fclose(fp);

	printf("Saved state: %s\n", savePathName);
	return true;
}

void pcm_submit(void)
{

}

size_t
pce_osd_getromdata(unsigned char **data)
{
    return syscard_get_data(data);
}

const struct {
	const uint32_t crc;
	const char *Name;
	const uint32_t Flags;
} pceRomFlags[] = {
	{0x00000000, "Unknown", JAP},
	{0xF0ED3094, "Blazing Lazers", USA | TWO_PART_ROM},
	{0xB4A1B0F6, "Blazing Lazers", USA | TWO_PART_ROM},
	{0x55E9630D, "Legend of Hero Tonma", USA | US_ENCODED},
	{0x083C956A, "Populous", JAP | ONBOARD_RAM},
	{0x0A9ADE99, "Populous", JAP | ONBOARD_RAM},
};

int LoadCard(const char *name) {
    int offset;
    size_t rom_length = pce_osd_getromdata(&PCE.ROM);
    offset = rom_length & 0x1fff;
       
       
       PCE.ROM_SIZE = (rom_length - offset) / 0x2000;
       PCE.ROM_DATA = PCE.ROM + offset;
       PCE.ROM_CRC = crc32_le(0, PCE.ROM, rom_length);
       
       uint8_t IDX = 0;
       uint8_t ROM_MASK = 1;

       while (ROM_MASK < PCE.ROM_SIZE) ROM_MASK <<= 1;
       ROM_MASK--;

       printf("Rom Size: %d, B1:%X, B2:%X, B3:%X, B4:%X\n" , rom_length, PCE.ROM[0], PCE.ROM[1],PCE.ROM[2],PCE.ROM[3]);

       for (int index = 0; index < KNOWN_ROM_COUNT; index++) {
           if (PCE.ROM_CRC == pceRomFlags[index].crc) {
               IDX = index;
               break;
           }
       }

       printf("Game Name: %s\n", pceRomFlags[IDX].Name);
       printf("Game Region: %s\n", (pceRomFlags[IDX].Flags & JAP) ? "Japan" : "USA");

       // US Encrypted
    if ((pceRomFlags[IDX].Flags & US_ENCODED) || PCE.ROM_DATA[0x1FFF] < 0xE0) {

		unsigned char inverted_nibble[16] = {
			0, 8, 4, 12, 2, 10, 6, 14,
			1, 9, 5, 13, 3, 11, 7, 15
		};

		for (int x = 0; x < PCE.ROM_SIZE * 0x2000; x++) {
			unsigned char temp = PCE.ROM_DATA[x] & 15;

			PCE.ROM_DATA[x] &= ~0x0F;
			PCE.ROM_DATA[x] |= inverted_nibble[PCE.ROM_DATA[x] >> 4];

			PCE.ROM_DATA[x] &= ~0xF0;
			PCE.ROM_DATA[x] |= inverted_nibble[temp] << 4;
		}
    }

	// For example with Devil Crush 512Ko
    if (pceRomFlags[IDX].Flags & TWO_PART_ROM) 
        PCE.ROM_SIZE = 0x30;

    // Game ROM
    for (int i = 0; i < 0x80; i++) {
        if (PCE.ROM_SIZE == 0x30) {
            switch (i & 0x70) {
            case 0x00:
            case 0x10:
            case 0x50:
                PCE.MemoryMapR[i] = PCE.ROM_DATA + (i & ROM_MASK) * 0x2000;
                break;
            case 0x20:
            case 0x60:
                PCE.MemoryMapR[i] = PCE.ROM_DATA + ((i - 0x20) & ROM_MASK) * 0x2000;
                break;
            case 0x30:
            case 0x70:
                PCE.MemoryMapR[i] = PCE.ROM_DATA + ((i - 0x10) & ROM_MASK) * 0x2000;
                break;
            case 0x40:
                PCE.MemoryMapR[i] = PCE.ROM_DATA + ((i - 0x20) & ROM_MASK) * 0x2000;
                break;
            }
        } else {
            PCE.MemoryMapR[i] = PCE.ROM_DATA + (i & ROM_MASK) * 0x2000;
        }
        PCE.MemoryMapW[i] = PCE.NULLRAM;
    }

    // Allocate the card's onboard ram
    if (pceRomFlags[IDX].Flags & ONBOARD_RAM) {
        PCE.ExRAM = PCE.ExRAM ?: PCE_EXRAM_BUF;
        PCE.MemoryMapR[0x40] = PCE.MemoryMapW[0x40] = PCE.ExRAM;
        PCE.MemoryMapR[0x41] = PCE.MemoryMapW[0x41] = PCE.ExRAM + 0x2000;
        PCE.MemoryMapR[0x42] = PCE.MemoryMapW[0x42] = PCE.ExRAM + 0x4000;
        PCE.MemoryMapR[0x43] = PCE.MemoryMapW[0x43] = PCE.ExRAM + 0x6000;
    }

    // Mapper for roms >= 1.5MB (SF2, homebrews)
    if (PCE.ROM_SIZE >= 192)
        PCE.MemoryMapW[0x00] = PCE.IOAREA;

    return 0;
}

int
InitPCE(int samplerate, bool stereo, const char *huecard)
{
	if (gfx_init())
		return 1;

//	if (psg_init(samplerate, stereo))
//		return 1;

	if (pce_init())
		return 1;

	if (huecard && LoadCard(huecard))
		return 1;

	/* PCE-CD host harness: map 256KB CD RAM (banks 0x68-0x87) like the device
	 * LoadCartPCE does, then mount the real CUE/BIN so the System Card's $1800
	 * SCSI reads hit it. pce_cd_read_sector uses fopen/fread = works on host. */
	if (g_cue_path) {
		static uint8_t *cdram = NULL;
		if (!cdram) cdram = (uint8_t *)malloc(0x40000);
		memset(cdram, 0, 0x40000);
		for (int v = 0x68; v <= 0x87; v++)
			PCE.MemoryMapR[v] = PCE.MemoryMapW[v] = cdram + (uint32_t)(v - 0x68) * 0x2000;
		static pce_cd_toc_t s_toc;
		if (pce_cd_parse_cue(g_cue_path, &s_toc)) {
			pce_scsi_set_disc(&s_toc, true);
			printf("CD mounted: %s (%d tracks)\n", g_cue_path, s_toc.num_tracks);
		} else {
			pce_scsi_set_disc(NULL, false);
			printf("CD mount FAILED: %s\n", g_cue_path);
		}
		/* BRAM: per-game .sram file. */
		pce_sram_load();

#ifdef PCE_ENABLE_ARCADE_CARD
		if (!getenv("PCE_ARCADE_CARD") || strcmp(getenv("PCE_ARCADE_CARD"), "0") != 0) {
			pce_arcade_card_init();
			pce_arcade_card_map_banks();
			printf("Arcade Card enabled (2MB RAM, banks $40-$43, I/O $1A00-$1BFF)\n");
		}
#endif
	}

	gfx_reset(0);
	pce_reset(0);

	return 0;
}

void init(void)
{
    printf("init()\n");
    odroid_system_init(APP_ID, AUDIO_SAMPLE_RATE);
    odroid_system_emu_init(&host_LoadState, &host_SaveState, NULL, NULL, NULL, NULL, NULL);

    // Hack: Use the same buffer twice
    update1.buffer = fb_data;
    update2.buffer = fb_data;

    //saveSRAM = odroid_settings_app_int32_get(NVS_KEY_SAVE_SRAM, 0);
    saveSRAM = false;

    // Load ROM
    InitPCE(0,0,"game.pce");
    pce_audio_init();

    // Video
    memset(fb_data, 0, sizeof(fb_data));
}

void pce_osd_gfx_blit(bool drawFrame) {
    static uint32_t lastFPSTime = 0;
    static uint32_t frames = 0;
    static int wantedTime = 1000 / 60;
    int xScale = 0;
    int y=0, offsetY, offsetX = 0;
    uint8_t *fbTmp;

    if (!drawFrame) {
        memset(fb_data,0,sizeof(fb_data));
        return;
    }

    uint32_t currentTime = HAL_GetTick();
    uint32_t delta = currentTime - lastFPSTime;

    frames++;
    if (delta >= 1000) {
        framePerSecond = (10000 * frames) / delta;
        printf("FPS: %d.%d, frames %d, delta %d ms\n", framePerSecond / 10, framePerSecond % 10, frames, delta);
        frames = 0;
        lastFPSTime = currentTime;
    }

    odroid_display_scaling_t scaling = ODROID_DISPLAY_SCALING_OFF;

    memset(fb_data, 0, sizeof(fb_data));

    int disp_w, disp_h;
    pce_display_size(&disp_w, &disp_h);

    int cropX = 0, cropY = 0;
    int renderWidth = disp_w;
    int renderHeight = disp_h;
    int offsetX = 0, offsetY = 0;

    if (renderWidth > WIDTH) {
        cropX = (renderWidth - WIDTH) / 2;
        renderWidth = WIDTH;
    } else if (scaling == ODROID_DISPLAY_SCALING_OFF) {
        offsetX = (WIDTH - renderWidth) / 2;
    }

    if (renderHeight > HEIGHT) {
        cropY = (renderHeight - HEIGHT) / 2;
        renderHeight = HEIGHT;
    } else {
        offsetY = (HEIGHT - renderHeight) / 2;
    }

    if (disp_w > 0 && scaling != ODROID_DISPLAY_SCALING_OFF) {
        xScale = (disp_w << 8) / WIDTH;
    }

    uint8_t *emuFrameBuffer = osd_gfx_framebuffer();
    pixel_t *framebuffer_active = fb_data;

    for (y = 0; y < renderHeight; y++) {
        fbTmp = emuFrameBuffer + ((y + cropY) * XBUF_WIDTH);
        pixel_t *dst = framebuffer_active + (y + offsetY) * WIDTH + offsetX;
        if (xScale) {
            for (int x = 0; x < WIDTH; x++)
                dst[x] = mypalette[fbTmp[((x * xScale) >> 8) + cropX]];
        } else {
            for (int x = 0; x < renderWidth; x++)
                dst[x] = mypalette[fbTmp[x + cropX]];
        }
    }

    SDL_UpdateTexture(fb_texture, NULL, fb_data, WIDTH * BPP);
    SDL_RenderCopy(renderer, fb_texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    //If frame finished early
    int frameTicks = SDL_GetTicks() - capTimer;
    if( frameTicks < wantedTime )
    {
        //Wait remaining time
        SDL_Delay( wantedTime - frameTicks );
    }
}


void osd_log(int type, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

/* One-shot dump of the CPU bank mapping (MMR) + the System Card RAM hook-vector
 * area ($2200-$225F lives in PCE.RAM offset 0x200). Lets us SEE whether the game
 * registered a vsync/IRQ handler pointing at its own code (0x6xxx/0x8xxx). */
static void dump_mem(uint16_t start, int len)
{
    {
        extern uint8_t *PageR[8];
        printf("[host] dump_mem sees PageR[2]=%p first-byte@0x4de0=%02x\n",
               (void *)PageR[2], pce_read8((uint16_t)0x4de0));
    }
    /* NB: loop var must NOT be named 'a' — pce_read8 is a statement-expr macro
     * whose internal 'uint16_t a = (addr)' CAPTURES an outer 'a' in the addr
     * expression (it read garbage addresses for weeks). */
    for (int base = start; base < start + len; base += 16) {
        printf("[host] $%04x:", base);
        for (int j = 0; j < 16; j++) printf(" %02x", pce_read8((uint16_t)(base + j)));
        printf("\n");
    }
}

static void dump_state(const char *tag)
{
    printf("[host] === STATE %s ===\n", tag);
    printf("[host] MMR:");
    for (int i = 0; i < 8; i++) printf(" %d:%02x", i, PCE.MMR[i]);
    printf("\n");
    for (int base = 0x200; base < 0x260; base += 16) {
        printf("[host] $%04x:", 0x2000 + base);
        for (int j = 0; j < 16; j++) printf(" %02x", PCE.RAM[base + j]);
        printf("\n");
    }
    printf("[host] --- exec vector $2280-$228F ---\n"); dump_mem(0x2280, 16);
    printf("[host] --- program @ $6250-$62BF ---\n");   dump_mem(0x6250, 112);
    printf("[host] --- subroutine @ $6350-$637F ---\n"); dump_mem(0x6350, 48);
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    if (argc > 1) g_cue_path = argv[1];
    const char *syscard = syscard_find_default();
    if (!syscard) {
        fprintf(stderr, "No System Card found. Set PCE_SYSCARD or place syscard3.pce in cwd.\n");
        return 1;
    }
    if (syscard_load_file(syscard))
        return 1;
    if (getenv("PCE_KILL_TIMER")) { g_pce_kill_timer = 1; printf("[host] TIMER HARD-KILLED\n"); }
    /* Headless trace mode: argv[2] = number of frames to run then exit (so the
     * deterministic SCSI/PC diag can be inspected without an infinite loop). */
    int max_frames = (argc > 2) ? atoi(argv[2]) : 0;

    printf("retro-go-pce debug mode (set PCE_TRACE=1 for CPU trace)\n");
    printf("Savestates: F2 save / F4 load -> <cue>.state  (GWAUTOLOAD=1 to load at boot)\n");

    init_window(WIDTH, HEIGHT);
    init();

    odroid_gamepad_state_t joystick = {0};
    int frame = 0;
    bool forced_cli = false;
    bool dumped_6257 = false;
    /* argv[3] == "cli" → force-CLI at 0x6257 (run the game IRQ-enabled). Without
     * it we observe the game's NATURAL init: does it CLI / register a vsync hook
     * on its own, or idle at 0x6257 with interrupts still masked? */
    bool do_cli = (argc > 3 && strcmp(argv[3], "cli") == 0);
    g_pcecd_trace = getenv("PCE_TRACE") ? 1 : 0;

    {
        const char *al = getenv("GWAUTOLOAD");
        if (al && al[0] && al[0] != '0')
            linux_loadstate_req = 1;
    }

    while (true)
    {

        //Start cap timer
        capTimer = SDL_GetTicks();
        //wdog_refresh();
        bool drawFrame = true;// common_emu_frame_loop();

        pce_sdl_input_poll(&joystick);

        if (linux_quit_req)
            break;

        if (linux_savestate_req) {
            linux_savestate_req = 0;
            pce_linux_save_state();
        }
        if (linux_loadstate_req) {
            linux_loadstate_req = 0;
            pce_linux_load_state();
        }

        if (getenv("PCE_NO_MASH")) {
            /* Control run: NO injected input at all (the fixed-device resume path). */
        } else if (getenv("PCE_RESUME_RUNHOLD")) {
            joystick.values[ODROID_INPUT_START] =
                (g_load_frame && frame >= g_load_frame + 60 && frame < g_load_frame + 150) ? 1 : 0;
        } else if (g_cue_path && !getenv("PCE_NO_AUTOSTART")) {
            if (frame >= 45 && frame <= 90)
                joystick.values[ODROID_INPUT_DOWN] = 1;
            if (frame >= 90 && frame <= 210)
                joystick.values[ODROID_INPUT_START] = 1;
        } else {
        const char *mu = getenv("PCE_MASH_UNTIL");
        if (!mu || frame < atoi(mu)) {
        joystick.values[ODROID_INPUT_START] = ((frame % 200) >= 60 && (frame % 200) < 90) ? 1 : 0;
        joystick.values[ODROID_INPUT_A] = ((frame % 200) >= 140 && (frame % 200) < 170) ? 1 : 0;
        }
        }
        pce_input_read(&joystick);

        /* Same per-frame hook the device main loop calls: pumps the chunked
         * SCSI->ADPCM DMA (<=8KB/frame) so ADPCM loads complete. */
        pce_scsi_run();

        /* EXPERIMENT: force the timer off before the New-game copy window —
         * if the game then reaches in-game, root cause confirmed. */
        { extern int g_frame_now; g_frame_now = frame; }
        /* (timer suppression experiment removed — timer exonerated) */

        /* Frame-level bank 0x7f zero/nonzero transitions (coarse cross-check for
         * the [wp7f] instruction-level watch — catches bulk fills the two watched
         * bytes might miss). */
        {
            static int nz_state = -1;
            uint32_t s7 = 0;
            for (int k = 0; k < 0x2000; k++) s7 += PCE.MemoryMapR[0x7f][k];
            int nz = (s7 != 0);
            if (nz != nz_state) {
                printf("[host] f%d BANK7F %s sum=%08lx first16=", frame,
                       nz ? "NONZERO" : "ZERO", (unsigned long)s7);
                for (int k = 0; k < 16; k++) printf("%02x ", PCE.MemoryMapR[0x7f][k]);
                printf("\n"); fflush(stdout);
                nz_state = nz;
            }
        }

        /* Watchpoint: catch the exact frame Timer.running flips (no IO write
         * ever sets it per the [io] log -> struct corruption suspect). */
        {
            static int last_run = -1, last_reload = -1;
            if ((int)PCE.Timer.running != last_run || (int)PCE.Timer.reload != last_reload) {
                printf("[host] f%d TIMER CHANGE running=%d reload=%d counter=%d\n",
                       frame, (int)PCE.Timer.running, (int)PCE.Timer.reload, (int)PCE.Timer.counter);
                last_run = PCE.Timer.running; last_reload = PCE.Timer.reload;
            }
        }

        /* Post-load stuck-probe: after any state load, sample the 6280 PC + the
         * CD-DA/ADPCM stream every 25 frames for ~15s. The device freeze hits
         * ~1s (60 frames) after load, so a stable PC cluster here IS the repro. */
        {
            extern int g_load_frame;   /* set by the self-test block on each load */
            if (g_load_frame && frame > g_load_frame &&
                frame <= g_load_frame + 900 && (frame % 25) == 0) {
                uint32_t cst[PCE_SCSI_CDDA_STATE_WORDS];
                pce_scsi_cdda_get(cst);
                printf("[host] postload f%d pc=%04x P=%02x irql=%02x cdda lba=%lu play=%lu adpcm=%d\n",
                       frame, CPU_PCE.PC, CPU_PCE.P, CPU_PCE.irq_lines,
                       (unsigned long)cst[1], (unsigned long)(cst[0]), pce_adpcm_playing());
            }
        }

        /* Stuck-probe: late in the run, sample where the 6280 sits each 50
         * frames (a spin loop shows up as a stable PC cluster). */
        { const char *pf = getenv("PCE_PROBE_FROM");
          if (pf && frame > atoi(pf) && (frame % 50) == 0) {
              extern unsigned long g_tick_count;
              static unsigned long lt2;
              uint32_t cst[PCE_SCSI_CDDA_STATE_WORDS]; pce_scsi_cdda_get(cst);
              printf("[host] gprobe f%d pc=%04x P=%02x cdda lba=%lu st=%lu adpcm=%d ticks=%lu\n",
                     frame, CPU_PCE.PC, CPU_PCE.P, (unsigned long)cst[1],
                     (unsigned long)cst[0], pce_adpcm_playing(), g_tick_count - lt2);
              lt2 = g_tick_count;
          } }
        if (frame > 13000 && (frame % 50) == 0) {
            extern unsigned long g_tick_count;
            static unsigned long last_ticks;
            printf("[host] probe f%d pc=%04x P=%02x ticks/50f=%lu\n",
                   frame, CPU_PCE.PC, CPU_PCE.P, g_tick_count - last_ticks);
            last_ticks = g_tick_count;
        }
        if (frame == 14000) {
            /* Compare what the 256KB stage load left in CD RAM vs the disc. */
            printf("[host] CDRAM bank68[0..15]: ");
            for (int k = 0; k < 16; k++) printf("%02x ", PCE.MemoryMapR[0x68][k]);
            printf("\n[host] CDRAM bank69[0xdf0..0xdff]: ");
            for (int k = 0; k < 16; k++) printf("%02x ", PCE.MemoryMapR[0x69][0xdf0 + k]);
            printf("\n[host] CDRAM bank80[0..15]: ");
            for (int k = 0; k < 16; k++) printf("%02x ", PCE.MemoryMapR[0x80][k]);
            /* Decisive: which bank does PageR[2] REALLY point at? */
            {
                extern uint8_t *PageR[8];
                uint8_t *pr2 = PageR[2] + 2 * 0x2000;   /* undo the -P*0x2000 bias */
                printf("[host] PageR[2]+0x4000=%p MemoryMapR[MMR[2]=0x%02x]=%p\n",
                       (void *)pr2, PCE.MMR[2], (void *)PCE.MemoryMapR[PCE.MMR[2]]);
                for (int v = 0; v < 256; v++)
                    if (PCE.MemoryMapR[v] == pr2)
                        printf("[host] PageR[2] actually maps BANK 0x%02x\n", v);
                printf("[host] TRIPLE $4df0: read8=%02x PageR2=%02x MapR69=%02x (ptrs %p %p)\n",
                       pce_read8(0x4df0), PageR[2][0x4df0], PCE.MemoryMapR[0x69][0xdf0],
                       (void *)(PageR[2] + 0x4df0), (void *)(PCE.MemoryMapR[0x69] + 0xdf0));
            }
            {   /* full CD RAM dump for offline diff vs the disc image */
                FILE *df = fopen("cdram_dump.bin", "wb");
                if (df) {
                    for (int v = 0x68; v <= 0x87; v++)
                        fwrite(PCE.MemoryMapR[v], 0x2000, 1, df);
                    fclose(df);
                    printf("[host] cdram_dump.bin written (256KB banks 68-87)\n");
                }
            }
            printf("[host] manual read8 $4de0-$4e0f: ");
            for (int k = 0; k < 0x30; k++) printf("%02x ", pce_read8((uint16_t)(0x4de0 + k)));
            printf("\n[host] CPU_PCE.PC raw=%08x\n", (unsigned)CPU_PCE.PC);
            { extern uint16_t g_ring[]; extern int g_ridx;
              printf("[host] ===== LIVE RING (last 256 exec PCs) =====\n");
              int st = g_ridx % 256;
              for (int k = 0; k < 256; k++) {
                  printf("%04x ", g_ring[(st + k) % 256]);
                  if ((k % 16) == 15) printf("\n");
              } printf("[host] ===== END RING =====\n"); fflush(stdout); }
            printf("[host] bank68 vectors $FFF0-$FFFF: ");
            for (int k = 0; k < 16; k++) printf("%02x ", PCE.MemoryMapR[0x68][0x1ff0 + k]);
            printf("\n[host] Timer: running=%d reload=%d counter=%d cyc=%d per_line=%d\n",
                   (int)PCE.Timer.running, (int)PCE.Timer.reload, (int)PCE.Timer.counter,
                   (int)PCE.Timer.cycles_counter, (int)PCE.Timer.cycles_per_line);
            printf("\n[host] --- wait-loop + dispatch code ---\n");
            dump_mem(0x45a0, 0x30);
            dump_mem(0x47e0, 0x60);
            dump_mem(0x3d20, 0x40);
            dump_mem(0xe6b0, 0x30);
            printf("\n[host] --- stuck loop code around PC ---\n");
            dump_mem(CPU_PCE.PC - 0x20, 0x50);
            printf("[host] MMR: ");
            for (int b = 0; b < 8; b++) printf("%02x ", PCE.MMR[b]);
            printf("\n[host] VDC status=%02x pending_irqs=%d irq_lines=%02x\n",
                   PCE.VDC.status, (int)PCE.VDC.pending_irqs, CPU_PCE.irq_lines);
            fflush(stdout);
        }

        /* First time the game reaches its idle loop, dump the registered hook
         * vectors + bank mapping so we can see what (if anything) init set up. */
        if (!dumped_6257 && CPU_PCE.PC == 0x6257) {
            dumped_6257 = true;
            printf("[host] reached 0x6257 idle frame=%d P=%02x irqmask=%02x\n",
                   frame, CPU_PCE.P, CPU_PCE.irq_mask);
            dump_state("at-6257");
        }

        /* Optional: same interrupt-enable the device harness uses — the System
         * Card hands off with FL_I set; clear it once so VBlank IRQs run. */
        if (do_cli && !forced_cli && CPU_PCE.PC == 0x6257 && (CPU_PCE.P & 0x04)) {
            forced_cli = true;
            CPU_PCE.P &= ~0x04;
            printf("[host] FORCE-CLI at 0x6257 frame=%d\n", frame);
        }

        for (PCE.Scanline = 0; PCE.Scanline < 263; ++PCE.Scanline) {
            gfx_run();
        }
        pce_osd_gfx_blit(drawFrame);
        pce_pcm_submit();

        /* CD-DA verify: dump this frame's CD-DA PCM (44100 stereo s16,
         * bit-perfect passthrough) to a raw file so we can confirm the audio
         * decode produces real music. */
        {
            static FILE *cf = NULL, *af = NULL;
            static int16_t cb[800 * 2], ab[800 * 2];
            if (!cf) cf = fopen("cdda.pcm", "wb");
            if (!af) af = fopen("adpcm.pcm", "wb");
            int n = pce_scsi_cdda_fill(cb, 735);
            if (cf && n > 0) fwrite(cb, sizeof(int16_t) * 2, n, cf);
            /* Resume regression probe: across the frame-1100/1110 save/load
             * self-test, n must stay >0 (the CD-DA block re-arms the stream). */
            if (g_cue_path && frame >= 1105 && frame <= 1115)
                printf("[host] f%d cdda_n=%d\n", (int)frame, n);
            extern int pce_adpcm_fill(int16_t *, int);
            int an = pce_adpcm_fill(ab, 735);
            if (af && an > 0) fwrite(ab, sizeof(int16_t) * 2, an, af);
        }

        pce_adpcm_frame_end();

        // Prevent overflow
        PCE.Timer.cycles_counter -= Cycles;
        PCE.MaxCycles -= Cycles;
        Cycles = 0;

        /* Save/load round-trip self-test (PCE-CD): save at 1100, corrupt the CD
         * RAM at 1105, load at 1110 — the checksum must return to the saved one. */
        if (g_cue_path && max_frames >= 1115 && !getenv("PCE_NO_SELFTEST")) {
            static uint32_t sum_pre = 0;
            char state_path[1024];
            char state_alt_path[1024];
            pce_state_path(state_path, sizeof(state_path));
            pce_state_alt_path(state_alt_path, sizeof(state_alt_path));
            /* COLD-RESUME repro (device flow): fresh boot, then load a PREVIOUS
             * session's in-game save early — exactly what 'Resume game' does. */
            static int cold_done = 0;
            if (!cold_done && frame == 700 && getenv("PCE_COLD_RESUME")) {
                cold_done = 1;
                printf("[host] COLD-RESUME load @700\n");
                host_LoadState(state_alt_path);
                g_load_frame = frame;
            }
            static int save2_f = 0;
            if (getenv("PCE_COLD_RESUME")) save2_f = -1;   /* skip in-session tests */
            /* Device-freeze repro trigger: save in the EXACT peripheral state the
             * device freeze-save was taken in — CDDA streaming (play=1) past a
             * given LBA (mode=1 loop music), ADPCM already ended. The old
             * pce_adpcm_playing() trigger captured the OPPOSITE state, which is
             * why the harness never reproduced the load-then-freeze. */
            const char *cdda_env = getenv("PCE_SAVE_CDDA_LBA");
            if (save2_f == 0 && cdda_env) {
                uint32_t cst[PCE_SCSI_CDDA_STATE_WORDS];
                pce_scsi_cdda_get(cst);
                if ((cst[0] & 1u) && cst[1] >= (uint32_t)atoi(cdda_env)) {
                    save2_f = frame;
                    host_SaveState(state_alt_path);
                    printf("[host] SAVE2 @%d (CDDA play lba=%lu end=%lu mode=%lu adpcm=%d)\n",
                           frame, (unsigned long)cst[1], (unsigned long)cst[3],
                           (unsigned long)cst[4], pce_adpcm_playing());
                }
            }
            if (save2_f == 0 && !cdda_env && frame >= 5000 && pce_adpcm_playing()) {
                save2_f = frame;
                host_SaveState(state_alt_path);
                printf("[host] SAVE2 @%d (in-game, ADPCM PLAYING)\n", frame);
            } else if (save2_f > 0 && frame == save2_f + 10) {
                host_LoadState(state_alt_path);
                g_load_frame = frame;
                printf("[host] LOAD2 @%d (in-game)\n", frame);
            } else if (frame == 1100) {
                for (int v = 0x68; v <= 0x87; v++)
                    for (int k = 0; k < 0x2000; k++) sum_pre += PCE.MemoryMapR[v][k];
                host_SaveState(state_path);
                printf("[host] SAVE @1100 cdram_sum=%08x\n", sum_pre);
            } else if (frame == 1105) {
                memset(PCE.MemoryMapW[0x80], 0xEE, 0x2000);   /* corrupt one bank */
                uint32_t s = 0;
                for (int v = 0x68; v <= 0x87; v++)
                    for (int k = 0; k < 0x2000; k++) s += PCE.MemoryMapR[v][k];
                printf("[host] CORRUPT @1105 cdram_sum=%08x (should differ)\n", s);
            } else if (frame == 1110) {
                host_LoadState(state_path);
                uint32_t s = 0;
                for (int v = 0x68; v <= 0x87; v++)
                    for (int k = 0; k < 0x2000; k++) s += PCE.MemoryMapR[v][k];
                printf("[host] LOAD @1110 cdram_sum=%08x -> %s\n", s,
                       s == sum_pre ? "MATCH (save/load OK)" : "MISMATCH (BUG)");
            }
        }

        if (max_frames && ++frame >= max_frames) {
            printf("[host] done %d frames, PC=%04x P=%02x irql=%02x\n",
                   frame, CPU_PCE.PC, CPU_PCE.P, CPU_PCE.irq_lines);
            printf("[host] --- code around final PC ---\n");
            dump_mem((uint16_t)(CPU_PCE.PC - 0x40), 0x80);
            { extern uint16_t g_ring[]; extern int g_ridx;
              printf("[host] ===== FINAL RING (last 256 exec PCs) =====\n");
              int st = g_ridx % 256;
              for (int k = 0; k < 256; k++) {
                  printf("%04x ", g_ring[(st + k) % 256]);
                  if ((k % 16) == 15) printf("\n");
              } printf("[host] ===== END RING =====\n"); }
            dump_state("end");
            /* Dump the live framebuffer to a PPM so we can SEE what's on screen. */
            {
                FILE *pf = fopen("frame_end.ppm", "wb");
                if (pf) {
                    uint8_t *efb = osd_gfx_framebuffer();
                    fprintf(pf, "P6\n%d %d\n255\n", current_width, current_height);
                    for (int yy = 0; yy < current_height; yy++) {
                        uint8_t *rowp = efb + yy * XBUF_WIDTH;
                        for (int xx = 0; xx < current_width; xx++) {
                            uint16_t px = mypalette[rowp[xx]];
                            uint8_t r = ((px >> 11) & 0x1f) << 3;
                            uint8_t g = ((px >> 5) & 0x3f) << 2;
                            uint8_t b = (px & 0x1f) << 3;
                            fputc(r, pf); fputc(g, pf); fputc(b, pf);
                        }
                    }
                    fclose(pf);
                    printf("[host] wrote frame_end.ppm %dx%d\n", current_width, current_height);
                }
            }
            break;
        }
    }

    pce_sram_save();
    SDL_Quit();

    return 0;
}
