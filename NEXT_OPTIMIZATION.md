# EarthBound PPU — next optimization handoff

Continuation plan for a fresh session. Read this together with `OPTIMIZATIONS.md`
(the full shipped log + reverted dead-ends). All source line refs are to
`external/earthbound/src/snes/ppu_render.c` unless noted.

## Where we are (2026-05-29)

Overworld per-frame PPU render, BGPROF (0.1 ms units):

```
... emit=3776 blank=8 decode=1272 hit=66% px=30208 elided=2968 |
CLR=30 BG=79 SND=5 TOTAL=173
```

Journey: original `TOTAL=214 / BG=118` → row-plan cache → transparent-tile
elision → **`TOTAL=173 / BG=79`** (BG −33%). Shipped commits (submodule
`external/earthbound`, branch `main`; parent bumps on branch `earthbound`):

- `699b5363` BGPROF instrumentation (under `PPU_PROFILE`)
- `bcb99275` BG tilemap row-plan cache (`#if PPU_BG_ROW_CACHE`) — BG 118→105
- `46b079fe` transparent-tile elision (`tile_fully_blank`) — BG 105→79
- `62efb048` **OBJ/WIN/COMP phase timings added to BGPROF** ← the tool for this pass

Proven lesson (5+ experiments): **eliminating redundant work wins; relocating
work (ITCM/DTCM/AoS) and per-pixel micro-opts are hidden by the M7 cache/pipeline
and do nothing.** Both BG wins removed redundant *per-scanline* recomputation.

## STEP 0 (do this first): measure the remainder

`BG=79` is no longer the biggest slice. `TOTAL 173 − CLR 30 − BG 79 − SND 5 ≈ 59
units (5.9 ms, ~34% of frame)` is **OBJ + WIN + composite**, previously unshown.
Commit `62efb048` now prints them. **Flash and read the new line first:**

```
make flash_sd        # reflashes intflash AND pushes EarthBound.bin + earthbound.ro
```

Overworld BGPROF now reads `... CLR=.. BG=.. OBJ=.. WIN=.. COMP=.. SND=.. TOTAL=..`.
Expectations to confirm: `WIN≈0` (overworld has no windows → `precompute_window_masks`
skipped), so the 59 units should split mostly between **COMP** and **OBJ**. Target
the larger. Both candidates below are pre-analyzed; pick by the measured split.

## Candidate 1 — composite fast-path specialization (likely the bigger one)

The compositing loop (`ppu_render_frame_ex`, ~line 1490–1564, `PROF_SECTION(comp)`)
runs a **per-pixel pass over the full ~320-wide viewport × 240 lines** (~76,800
px/frame) and still touches every pixel even though BG is now ~80% backdrop after
elision. Per pixel it does: OBJ priority check, BG priority compare, color select,
then **two loop-invariant branches** — `if (color_math_active …)` and
`if (brightness < 0x0F …)`.

On the overworld the common case is **no windows, no color math, full brightness
(0x0F), and most scanlines have no sprites**. In that case the loop collapses to
`line_out[x] = bg_gp>0 ? best_bg_color[x] : backdrop`. Idea: hoist the invariants
and emit a specialized tight loop for that case (and a no-OBJ variant when the
scanline has zero sprite pixels — see Candidate 2's per-line flag).

- **Method:** first check the `-O3` `.lst` (`objdump -dS build/earthbound/ppu_render.o`)
  to see if the compiler already unswitched `color_math_active`/`brightness`. If the
  per-pixel branches survive, split the loop. Measure COMP delta vs the baseline.
- **Risk:** moderate. Keep the general path for windowed/color-math/dimmed scenes
  (battle swirls, fades use color math + brightness — verify those visually).
- **Caveat:** this is closer to a per-pixel micro-op, which has a *poor* record here.
  The win must come from *removing* the per-pixel branches (loop unswitching), not
  from reordering — confirm in the `.lst` it's actually removing work.

## Candidate 2 — OBJ per-frame sprite bucketing (clean redundancy, row-cache-like)

`render_obj_scanline` (line 570) **rescans all 128 OAM sprites every scanline**
(`for i=127..0`), recomputing each sprite's size/x/y and doing `row = scanline -
spr_y; if (row<0||row>=h) continue;` — **128 × 240 = 30,720 sprite-tests/frame,**
the vast majority rejected. Classic redundant per-scanline rescan (same shape as
the row cache).

- **Fix:** once per frame, bucket sprite indices by the scanline range they cover
  (e.g. an array of small per-scanline lists, or per-8px-band buckets), then each
  scanline iterates only the sprites that overlap it. Build cost is O(128) once;
  per-scanline cost drops from 128 to (#sprites on that line).
- **Expected:** depends on active sprite count (overworld = player + a few NPCs, so
  most of the 30,720 tests are pure waste). Could be a clean win if OBJ is a
  meaningful share of the 59 units.
- **Risk:** low-moderate. Priority order matters — sprites are scanned 127→0 so
  lower index wins; preserve that ordering within each bucket. Per-frame reset like
  the row cache (`bg_row_cache`). Single render context only (see gotchas).
- **Bonus:** while building buckets, set a per-scanline `has_obj` flag → feeds
  Candidate 1's no-OBJ composite fast path.

## Methodology (what has worked here — follow it)

1. **Stub the ceiling first.** Before building the real thing, write a throwaway
   stub that captures the *upper bound* (e.g. skip the work entirely, even if
   visually wrong) gated on a `#define`, flash, read BGPROF. If the ceiling is
   noise, abandon — cheaply. (This killed the uniform-mask idea and confirmed both
   BG wins.)
2. **Same-scene measurement.** Only compare BGPROF lines with matching
   `layers/line=3.00` + `px/line` — a stuck logger / different screen burned us once.
3. **Verify visually on HW** (overworld + battle) before shipping — by-construction
   correctness arguments still get a glance.
4. **Productionize**, then commit: submodule (`main`) first, then bump the parent
   pointer (branch `earthbound`). Update `OPTIMIZATIONS.md` (directly is fine).

## Gotchas / environment

- **Build one object locally (no flash):** `make GCC_PATH=/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin CHECK_DIRTY_SUBMODULE=0 build/earthbound/ppu_render.o`
  (donor ROM `earthbound.sfc` is present; regenerates the `.lst`). clangd errors in
  the editor are noise — only trust the GCC build.
- **NEVER `git checkout`/`stash` `ppu_render.c` to revert** — the BGPROF/PPU_PROFILE
  bits are now committed (`699b5363`), but reverting experiments by hand (reverse
  Edits) is still the safe habit; a `git checkout` once nuked uncommitted
  instrumentation (recovered from a dangling stash).
- **Single render context only.** `bg_row_cache`/elision and any new per-frame cache
  are global, gated `#if … PPU_NUM_RENDER_CONTEXTS == 1` (the firm G&W default).
  Keep new caches behind the same assumption.
- **Reflash both** intflash *and* the overlay (`make flash_sd`) — overlay-only
  sdpush causes symbol-mismatch crashes.
- **Don't bother** with: ITCM/DTCM placement, AoS pixel packing, per-pixel
  mask/bpp/hflip micro-opts — all measured as noise or losses (see OPTIMIZATIONS.md
  "Reverted"). `-O3` already unswitches hflip/bpp in the BG inner loop.

## Deferred (not now)

- **Frameskip** — the big lever, intentionally LAST: add it once render is
  comfortably <16.6 ms in most scenes so rendering every other frame restores 60 Hz
  game logic. zelda3 ships `ZELDA3_LIMIT_30FPS=1`. Open question: confirm EB logic is
  render-coupled (running slow-motion below 60 Hz today).
- **Lever B** (fold the one non-filling layer's temp+merge into emit): `temp_lyr=240`
  = exactly 1 of 3 overworld layers; small + complex, shelved.
