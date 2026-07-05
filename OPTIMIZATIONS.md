# EarthBound PPU — optimization log & pending ideas

**Current state: ~33.5 fps and climbing** (overworld), up from ~20 at the start.
Per-frame PPU render ≈ 15.9 ms after the BG row-plan cache + transparent-tile
elision + OBJ sprite bucketing (BGPROF `TOTAL=159`, 0.1 ms units; was 214):
`BG≈79` (was 118), `COMP≈33`, `CLR≈30`, `SND≈9`, `OBJ≈3` (was 22), `WIN≈0`.
With OBJ mined out, `COMP≈33` is the largest non-BG slice — but it was
**investigated and ruled out** (see below): `-O3` already unswitches its
color-math/brightness invariants, and a ceiling stub proved removing the
remaining per-pixel OBJ work yields zero (M7 hides it). The per-phase micro-opt
vein is exhausted; the next real lever is **frameskip**. Source line references
are to `external/earthbound/src/snes/ppu_render.c` unless otherwise noted.

**BG is two halves, not one monolith.** A stub A/B split `BG` ~50/50 between
`emit_tile_run` internals (the per-pixel decode + priority compositing — the
genuinely irreducible 3-layer software-PPU core) and `render_bg_scanline`'s
*column loop* (per-tile tilemap address arithmetic + `ppu.vram` reads + field
extraction). The column-loop half had never been touched; it turned out to be
both ~7/8 redundant across a tile row's scanlines (→ row-plan cache) **and**
mostly transparent tiles that emit nothing (→ tile elision). Together those took
BG 118 → 79. The remaining `emit_tile_run` compositing of the ~20% non-blank
tiles is the wall.

## Shipped gains — the real bottleneck was vsync quantization

EB's original ~20 fps cap was **vsync quantization**, not PPU compute. The
shipped wins on `earthbound`, in order:

- `127a20f3` triple-buffering: **20 → 28 fps**
- `7fc78e11` `-O3` on `ppu_render.c` only: **28 → 30.3 fps**
- **#4 skip `line_out` clear in wide mode** (below): **30.3 → 33.3 fps**
  (CLR 50 → 30, TOTAL 238 → 217 in 0.1 ms units).
- **Hoist `eff_tm/ts_line` fill out of the loop** (`ppu_render_frame_ex`):
  when there are no windows and no per-scanline TM/TS HDMA, `base_tm/base_ts`
  are frame-constant, so the per-scanline `memset(eff_tm_line/eff_ts_line)` is
  filled once before the loop instead of 240×. ~150 KB/frame removed; TOTAL
  217 → 214 — a small, near-noise-floor but correct work-elimination.
  Windowed / TM-HDMA scenes keep the original per-scanline fill.
- **BG tilemap row-plan cache** (first *algorithmic* BG win — see "Row cache"
  below): **TOTAL 214 → 195, BG 118 → 105 (−11%)**, submodule `bcb99275`. The
  column loop recomputed an identical per-column plan on every scanline of a
  tile row; cache it once and replay it. Removed real redundant work in the
  half of BG nothing had touched.
- **Elide fully-transparent tiles from the row plan** (extends the row cache):
  **TOTAL 195 → 173, BG 105 → 79 (−25%)**, submodule `46b079fe`. ~80% of
  overworld BG tiles are all-zero (transparent on every row); detect them once
  at plan-build time and drop them from the plan, so they cost nothing on the
  row's other scanlines. Output-equivalent by construction. `emit_calls` fell
  27360 → 3776/frame.
- **OBJ per-frame sprite bucketing** (third *algorithmic* redundancy win, same
  shape as the row cache): **TOTAL 178 → 159, OBJ 22 → 3 (−86%)**, submodule
  `5555a539`. `render_obj_scanline` rescanned all 128 OAM sprites on every
  scanline (~28.7 k sprite-tests/frame, the vast majority rejected by
  `row<0||row>=h`). `build_obj_buckets()` now runs once per frame, culls
  off-screen sprites and buckets the rest by the 8-scanline band(s) they cover;
  each scanline iterates only sprites overlapping its band. Priority (lower OAM
  index wins) preserved by 127→0 fill + overwrite-on-emit. Output-equivalent by
  construction. Gated `#if PPU_OBJ_BUCKET` (single render context only).

Key lesson from this work: **eliminating work beats relocating it.** Every
attempt to move the *same* work to faster memory (ITCM/DTCM, see "Reverted")
landed in the noise; every win came from *removing* work — `-O3` codegen, #4's
dead memset, and the row-plan cache's redundant column recomputation. EB's
compositing core is **compute-bound** at 280 MHz (three full-width layers in
software), but "compute-bound" is not the same as "no redundant work": the row
cache proved there was still removable work outside the per-pixel inner loop.
When hunting further gains, look for redundant/dead work, not faster homes — and
note that per-pixel and memory-placement micro-opts have a *perfect failure
record* here (the M7 cache/pipeline hides them; see the reverted uniform-mask
attempt below).

**The remaining real levers:**
- **Frameskip.** zelda3 — the same per-scanline software-PPU architecture, also
  capped at ~30 fps — ships `ZELDA3_LIMIT_30FPS=1` (renders every other game
  frame). Still the single biggest untapped lever. Open question worth settling
  first: is EB's game logic render-coupled (i.e. running in slow-motion below
  60 Hz), which would make frameskip a *speed fix*, not just a smoothness trade?
- **More algorithmic PPU work.** The row cache only addressed the column-loop
  half of BG; the per-pixel compositing half (`emit_tile_run` internals) is the
  remaining ~50% and has no obvious single hotspot. Reductions must *remove*
  per-pixel/per-tile work, not relocate it.

## ★ Row cache: BG tilemap row-plan cache (SHIPPED, BG −11%)

The first *algorithmic* win to move BG, and the first to touch the column-loop
half. `render_bg_scanline`'s column loop (tilemap address arithmetic +
`ppu.vram` tilemap reads + entry field extraction) ran every scanline, but
vertical scroll is frame-constant, so the up-to-8 output scanlines that share a
tile row read **identical** tilemap entries and produce the same per-column plan
`{tile_num, palette, prio, flip, screen_x, run length}`. That recomputation was
~7/8 redundant.

Fix: decode the per-column plan once per tile row into `bg_row_plan[bg][]` and
replay it for the row's other scanlines. Per-scanline pixel decode/compositing
(`emit_tile_run`) still runs every line (it depends on the row-within-tile), so
only the column-derivation work is saved.

- **Correctness:** keyed on `(tile_row_map, scroll_x, render_width)`, invalidated
  at frame start. A replay therefore only happens for same-frame scanlines that
  map to the same tile row at the same scroll — exactly when the plan is provably
  identical. 8×8 tiles only; 16×16 and bg2 HDMA distortion (per-scanline scroll)
  fall back to the full loop. Single render context only (the cache is global);
  `#if PPU_BG_ROW_CACHE` auto-disables when `PPU_NUM_RENDER_CONTEXTS != 1`.
- **Measured (overworld, same scene):** BG 118 → 105, TOTAL 214 → 195.
- **Cost:** +1.3 KB `.text`, +2 KB BSS (RAM_EMU) — the M7 cache/pipeline did
  *not* hide this gain (unlike the relocations), because it removes a real
  dependent-integer-op + VRAM-read chain, not a pipelined-away load.
- **Verified** visually correct on hardware (overworld).

### Extension: elide fully-transparent tiles (SHIPPED, BG −25% more)

A natural follow-on, `46b079fe`. An all-zero tile graphic is palette index 0 on
every pixel, so `emit_tile_run` skips all 8 (`if (_cidx==0) continue`) and writes
nothing. `tile_fully_blank()` detects this once while building the row plan and
**omits the tile from the plan** — so it isn't emitted on *any* of the tile row's
scanlines (vs. re-paying the per-row blank-skip each time). Output-equivalent by
construction. On the overworld ~80% of BG tiles are fully transparent, so:

- **Measured:** BG 105 → 79, TOTAL 195 → 173; `emit_calls` 27360 → 3776/frame,
  `elided` ≈ 2968/frame. Verified visually correct (overworld + battle).
- Composes with the cache: the plan shrinks to ~5 entries/layer, so replay is
  nearly free. The full-tile read happens only on the 1-in-8 build scanlines.
- BGPROF gained an `elided` field.

### Not pursued: fold the wide-mode temp+merge into emit (probe B)

Instrumentation showed `temp_lyr=240` on the overworld = exactly **one** of the
three layers is non-filling (rendered to a temp buffer at SNES width, then merged
into the viewport). Folding that merge pass into `emit_tile_run` (via a screen-x
offset) would remove one per-pixel merge pass, but it's a much smaller target than
the elision and meaningfully more complex (priority + window mask applied during
merge). Shelved unless every last unit is wanted.

## Reverted: composite (COMP) per-pixel OBJ-skip — ceiling proved noise (2026-05-30)

After the OBJ bucketing win, `COMP≈33` was the largest non-BG slice, so it was
investigated (NEXT_OPTIMIZATION.md Candidate 1). Two findings, both negative:

1. **`-O3` already mined the obvious part.** The `.lst`
   (`objdump -dS build/earthbound/ppu_render.o`) shows GCC emits **5 specialized
   comp loops**, dispatched at loop entry on `brightness==0x0F` and
   `color_math_active`. The overworld runs the no-CM/full-brightness variant
   where neither `blend_colors` nor the brightness multiply appears per pixel.
   Candidate 1's "hoist the color-math/brightness invariants" is done by the
   compiler; reimplementing it by hand only adds I-cache bloat.
2. **Removing the remaining per-pixel OBJ work yields nothing.** Built a BG-only
   composite fast path for sprite-free / no-window / no-CM / full-brightness
   lines (`render_obj_scanline` returns per-line sprite presence; the line then
   takes `line_out[x] = bg_gp>0 ? best_bg_color[x] : backdrop`, no OBJ priority
   machinery). Real version: **COMP 33 → 33, TOTAL 159 → 158 (noise)**. A ceiling
   stub forcing the BG-only loop on *every* eligible line (sprites vanish) still
   measured **COMP 33** — so even the upper bound is zero. The per-pixel
   `obj_prio` load + threshold compare were already pipelined/predicted away by
   the M7. Same wall as the uniform-mask attempt below. **Fully reverted**
   (working-tree only; never committed). Don't re-attempt COMP micro-opts.

## Reverted: no-window uniform-mask fast path (measured net loss)

Hoisting the per-pixel window-mask check (`tm_line[_sx] & layer_bit`) out of
`EMIT_PIXELS` when no layer windows are active (the mask is then loop-invariant).
On hardware: **BG 118 → 118 (zero change), TOTAL 214 → 219 (slightly worse)** —
reverted. The mask check was never on the critical path: the not-taken branch is
perfectly predicted and the loads hit L1 D-cache (~1 cycle), pipelining behind
the compositing on the dual-issue M7. The +2.8 KB of `-O3`-unswitched variants
it added cost more (I-cache) than the phantom work it removed. Same wall as
ITCM/DTCM/AoS — confirming per-pixel inner-loop micro-opts are dead here; the row
cache won by removing work *outside* that loop.

## Reverted: EarthBound-wide -O3

- Compiling EarthBound sources at `-O3` instead of `-O2` (for speed) grew
  `EarthBound.bin` ~36% but still fit RAM_EMU.
- Booted fine to the title/file-select screen, but **BSODs immediately** when
  the overworld loads (after a file is selected).
- On-screen `Hardfault PC=velocity_store` is **misleading**. Real status:
  `CFSR=0x00000400` (BFSR IMPRECISERR), `BFAR=0` invalid, `HFSR=0x40000000`
  (FORCED) — an imprecise bus fault. A wild store drained through the
  Cortex-M7 store buffer late; the reported PC (inside `velocity_store`'s many
  cacheable SRAM stores) is innocent.
- Root cause: a **latent UB in the file-load path** (likely an uninitialized
  pointer landing on a bus-rejecting address) that `-O3` codegen exposes but
  `-O2` leaves benign. Adding `-fno-strict-aliasing` did **not** fix it, so it
  is not an aliasing bug.
- **Action taken:** EB sources stay at `-O2` in `Makefile.common`
  (`earthbound_obj_prereq_gen`); only `ppu_render.c` is forced to `-O3` via
  `PPU_FORCE_SPEED_OPT` (that TU is not on the file-load path). **Do not**
  re-bump the whole EB build to `-O3` until the wild store is root-caused.
- **How to root-cause when revisiting:** the BSOD now prints `CFSR`/`HFSR`/`BFAR`
  (added to `BSOD()` in `Core/Src/main.c`). Use `gdb` single-step — each step
  drains the store buffer, making the fault precise so `BFAR` pins the address.
  Start from the file-select-confirm handler down through `reset_party_state()`
  in `overworld.c`.

## Reverted: ITCM placement (both code and VRAM) — measured dead end

Two hardware A/B tests on the G&W (60-frame BGPROF average, overworld; values
in 0.1 ms units as reported by `PPU_PROFILE`):

| Config | TOTAL | BG |
|--------|-------|-----|
| **A (baseline)** — all code/data in cacheable AXI-SRAM | 238 | 118 |
| **B — VRAM → ITCM** (64 KB buffer) | 234 | **120** |
| **C — hot code → ITCM** (`ppu_render.o .text`, ~16 KB) | 239 | 119 |

All three are within run-to-run jitter (~2% of ~24 ms). In config B the BG read
path it targets actually got *worse* (118 → 120). **ITCM is a confirmed dead end
for EB FPS** — the render is neither VRAM-read-latency-bound (B) nor
I-cache-eviction-bound (C; the hot code was already I-cache-resident). Both
experiments were fully reverted (no residual changes in `external/earthbound`
or the firmware).

Implementation notes, in case ITCM is ever revisited for a *different* reason
(e.g. freeing RAM_EMU, not speed):

- **VRAM → ITCM:** gate `ppu.vram` on `PPU_VRAM_EXTERNAL` (pointer vs inline
  array, keep it the FIRST struct field); `ppu_init` must preserve+zero the
  pointer **unconditionally** (ITCM base is `0x0`, so a valid buffer can have
  address 0 — never NULL-test it); the port calls `itc_calloc(1,0x10000)` +
  `ppu_set_vram()` (failure sentinel is `0xffffffff`, not NULL). The 13
  `sizeof(ppu.vram)` bounds checks (sprite/overworld/map_loader) MUST become
  `VRAM_SIZE` or a pointer silently collapses them to 4 and drops VRAM writes.
- **Code → ITCM:** a linker `.eb_itcram` section (VMA=ITCM, `AT > CORES`)
  selecting `build/earthbound/ppu_render.o (.text .text*)`, placed *before*
  `.overlay_earthbound` so first-match pulls it out of the overlay (no
  EXCLUDE_FILE). Ship it as a separate `EarthBound_itcm.bin`; at boot stage it
  through RAM then `memcpy` → ITCM (the SD path may DMA, and **ITCM is not
  DMA-reachable**) + `DSB`/`ISB`. The linker auto-inserts long-call veneers
  (`__printf_veneer`, `__memset_veneer`, …) at the end of the section.
- **★ Critical gotcha (cost a BSOD):** `PatchCodeRodataOffset` in
  `main_earthbound.c` scans **RAM_EMU only**. `.rodata_earthbound` (printf
  format strings, LUTs, the asset blob) loads to a per-boot-varying address and
  every literal-pool pointer to it is rewritten by that scan. Any `.o` whose
  code/data you move OUT of `.overlay_earthbound` is no longer scanned, so its
  rodata pointers keep the `0xCAFF..` link address → BSOD with PC inside newlib
  `_vfiprintf_r` (garbage format string). Fix: re-run `PatchRodataRange()` over
  the relocated region with the SAME base/offset. (Now documented in the
  `PatchCodeRodataOffset` comment, commit `ddbfd4a9`.)

## Reverted: DTCM placement of per-scanline buffers (was idea #1)

The per-scanline working buffers (`line_out`, `best_bg_color/gp_lm`,
`sub_bg_*`, `obj_*`, `eff_tm/ts_line`, `cm_prevented_line`, the `temp_*` set,
~7 KB) were moved to DTCM. **Zero measurable gain.** They are touched every
pixel of every scanline, so they are *already* permanently d-cache resident in
their AXI-SRAM home — an M7 cache hit is ~1 cycle = DTCM speed, and there were
no misses to remove. The "AXI contention with code fetch" rationale is also
near-zero since hot loops run from the I-cache. (Only ~1.5 KB of DTCM padding
was free anyway.) Scaffolding (`.dtcm_eb` section, `-DPPU_LINEBUF_ATTR`) removed.

## Reverted: AoS-packed pixel stores (was idea #2)

`main_color[]` + `main_gp_lm[]` packed into one `uint32_t` per pixel. On
hardware this was a **net loss**: the per-scanline clear doubled (memset
2 → 4 B/pixel, since packing forces clearing the color half too), while the
priority check was already a single load — so the second store just became a
pack `orr` and the M7 store buffer had already been pipelining the two narrow
stores. CLR got worse, BG unchanged. Reverted.

## Attempted this session (done / declined)

### 4. Skip `memset(line_out, 0)` in wide mode — ✅ DONE (30.3 → 33.3 fps)

In wide mode (`fb_x_offset == 0 && render_width == EB_VIEWPORT_WIDTH`) the
compositor writes every output pixel (verified: the composite loop covers
`[0, EB_VIEWPORT_WIDTH)` with no per-pixel skips), so the per-scanline
`line_out` clear only ever painted left/right borders that don't exist there.
Guarded the clear on a hoisted loop-invariant (`line_out_full_cover`). Removed
640 B × 240 ≈ 150 KB/frame of memset. **Measured: CLR 50 → 30, TOTAL 238 → 217,
FPS 30.3 → 33.3 (+10%).** The vertical-border-scanline clear and the
non-wide/centered path are untouched.

### 5. Combine the per-scanline memsets — ❌ NOT WORTH IT (declined)

The idea was to fold CLR's separate memsets into one. But the remaining clears
are *already* conditionally skipped — `sub_bg_gp` only on color-math scanlines
(rare), `obj_prio` only when sprites are enabled. Those `if`s ARE the
work-elimination this item wanted. Combining into one contiguous unconditional
memset would FORCE the usually-skipped `sub_bg_gp` clear on most scanlines —
adding work (the same failure mode as the reverted AoS-pack). A careful combine
that preserves the conditionals saves only ~one memset *call* setup per scanline
(the zeroing *work* is irreducible) = sub-noise, while adding fragile BSS-layout
coupling to the hot path. After #4 took the one real work-elimination in CLR,
nothing here is worth the risk.

### Hoist `eff_tm/ts_line` fill out of the loop — ✅ DONE (217 → 214)

See the "Shipped gains" bullet above. When there are no windows and no
per-scanline TM/TS HDMA, the per-scanline `eff_tm_line`/`eff_ts_line` fills are
frame-constant, so they're done once before the loop. ~150 KB/frame removed;
TOTAL 217 → 214 — small (near the noise floor) but a correct work-elimination
with no downside. Windowed / TM-HDMA scenes keep the per-scanline fill.

### Async-clear the per-scanline buffers via DMA — ❌ declined (analysis only)

Premise check first: `CLR` does **not** clear the LCD framebuffer. The
framebuffer is never per-frame-cleared (the compositor overwrites every pixel —
that's why #4 worked), and the one framebuffer-bound clear (`line_out`) is
already gone via #4. What `CLR` now zeroes is the *internal* per-scanline
accumulators (`best_bg_gp_lm`, `obj_prio`), which must be cleared each scanline
because BG/OBJ write them sparsely and the compositor reads every pixel.
Triple-buffering is about output frames and does not apply here.

Offloading those clears to MDMA (ping-pong: clear scanline N+1 while the CPU
composites N) is *possible* but a poor trade:
- **Cache coherency is the blocker.** The buffers are cacheable AXI-SRAM and
  hot per-pixel; MDMA writes bypass the D-cache → stale reads. Fixes all hurt:
  per-scanline `SCB_InvalidateDCache_by_Addr` (overhead + bug-prone),
  non-cacheable buffers (slows the *hot* inner loop — likely a net loss), or
  DTCM (clean, but DTCM is ~full — only ~1.5 KB free, need ~3 KB for ping-pong).
- **Per-scanline MDMA setup ×240/frame** eats the ceiling; the clear must finish
  before BG renders into that buffer, so a faster CPU just stalls.
- **Upper bound is small:** `CLR≈3 ms` (~10%) → perfect overlap ≈ 33.3→37 fps,
  realistically +1–2 fps after overhead, for real complexity + correctness risk.

Same wall as ITCM/DTCM (the work is cheap because the buffers are already
cache-resident). Not worth it versus frameskip / an algorithmic BG change.

## Forward-looking ideas (untested, lower-priority)

Per-pixel/per-scanline *work reductions* (not relocations) — the only category
that has moved FPS on this compute-bound renderer. Measure each against the
BGPROF baseline before committing. (Realistically these are single-digit-% at
best; the BG compositing cost is largely irreducible — see header.)

### 6. Hoist `hflip` out of EMIT_PIXELS — ✅ ALREADY DONE BY -O3 (verified)

Checked the `.lst` (objdump -dS of `ppu_render.o`): `-O3` already unswitches the
loop-invariant `hflip` branch into forward / descending (`ldrb …,[rX,#-1]!`)
variants. Nothing to do.

### 7. Specialize `emit_tile_run` on bpp — ❌ NOT WORTH IT (verified)

`bpp` is a *runtime* arg to `render_bg_scanline`, so it can't const-fold through
inlining. In the `.lst`, the hot path already branches to a bpp-specialized
block; only the cold (windowed) path keeps a per-pixel `cmp #8`. Hand-
specializing `render_bg_scanline` on bpp would only help the cold path —
not worth the duplication.

### 8. Improve tile-row cache hash

`ppu_render.c:203`: `((tile_addr >> 1) ^ pixel_y) & TILE_CACHE_MASK`.
Direct-mapped 256-entry; collisions on neighboring rows are common. A
multiplicative hash (`(tile_addr * 0x9E3779B1u + pixel_y) >> 24`) typically
distributes better. Net win depends on the current hit rate (BGPROF reports
~66% on the overworld) — measure first.

### 9. Verify sub-screen elimination when color math is off — ✅ CONFIRMED

The `EMIT_PIXELS(HAS_SUB=0)` path is selected when `ctx->sub_gp == NULL` and the
`if (HAS_SUB …)` block is compiled out (structural). Confirmed in the `.lst`: the
`HAS_SUB=0` variants emit no sub-buffer (`sub_gp`/`sub_color`) accesses. Nothing
to do.

### 10. Verify window-mask cache hit rate

`precompute_window_masks` already has a memcmp-based cache key (`win_cache_key`,
`ppu_render.c:1117-1126`). If every scanline misses the cache, the WIN phase is
paying full price each line. Quick PMU instrumentation would settle it.

## Notes / instrumentation

- The PPU profile timings come from `platform_timer_ticks()`
  (`Core/Src/porting/earthbound/gw_timer.c`). Verify it isn't itself a
  non-trivial fraction of measured time — called many times per scanline.
- The Cortex-M7 PMU can count cache misses directly (`DWT_CTRL`/`DWT_CYCCNT` +
  event counters). If a specific theory needs verification (e.g. "is d-cache
  thrashing the per-line buffers?"), wire one event into the PPU_PROFILE struct.
  Given the ITCM/DTCM results above, **wire the PMU before trying any further
  memory-placement idea** — blind placement tuning has a perfect failure record
  here.
- Useful one-off measurements:
  - `arm-none-eabi-size build/earthbound/ppu_render.o` — code size of the hot
    translation unit; informs i-cache pressure (it is ~16 KB = the full I-cache).
  - `__bss_end__` of `.overlay_earthbound_bss` — working set, informs d-cache
    pressure.
