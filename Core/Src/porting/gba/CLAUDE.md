# GBA (gpSP) — the debugging playbook

Everything here was learned on hardware the expensive way. The fork is
`jshsakura/gpsp @ gnw-port` (submodule `external/gpsp`); one hook in `cpu.cc`,
one callback in `main.c`, everything else lives out here or in `tools/`.

## Which rig answers which question

| question | rig |
|---|---|
| is my HLE/optimization bit-exact, including guest time | `tools/gba_m4a/prove.sh` (`--blocks` / `--e2e` / `--speed`) |
| is a candidate idle address safe | `prove_main` with `IDLE_PC=` (+`IDLE_COND=ne` for polls, `NO_KEYS=1` always) — NOT mGBA, whose remover disables its own detector when forced |
| what does the sound stream actually contain | `M4A_AUDIO_RAW=` dump → numpy spectrum / click detector |
| why is the game frozen/stuck | `IDLE_TRACE=1` per-frame pc/halt/VCOUNT/IE/IF/IME, then `-DIDLE_SKIP_TRACE` in cpu.cc for per-arrival forensics |
| does the cart load path survive a real address space | `tools/gba_harness/run.sh` (QEMU maps page 0 and lies) |
| is the firmware linking what I tested | `tests/test_gba_m4a_wired.sh`, `tests/test_gba_xip_contract.sh` |
| what is the device really spending a frame on | branch `feat/gba-probe` (guest-PC histogram, DWT) |
| which carts have which mixer / idle loop | `tools/gba_m4a/census.py`; measurements live in `game-and-what` (idlefind + DB) |

All the prove_main knobs are tabled in `tools/gba_m4a/README.md`.

## Idle skips — TWO semantics, and the story that forced the second

- `idle_loop_target_pc` + `idle_loop_cond == ALWAYS`: the classic table
  (`gba_idle_loop.c`, GENERATED in game-and-what — whole-file copy only).
  Right for loops only an interrupt releases.
- `idle_loop_cond == WHEN_NE`: park on a VCOUNT poll's closing branch, burn
  the slice only while Z says it loops. Exists because SRT D paces its intro
  by CALLING `wait-for-scanline-160` a counted number of times — on hardware
  ~120 calls return instantly inside the matching line, and the ALWAYS skip
  turned each into a burned slice: a six-frame delay became seven hundred,
  which read as a freeze. **The poll was innocent; the caller was the loop.**
  Hand-curated entries in `main_gba.c` (`vcount_polls[]`), only for carts with
  no generated entry — the two waits share one target slot. Proof bar:
  1,800-frame no-keys A/B, screens ~99.8% at a small shift, both sets
  mutually contained.

## The sound path, end to end

guest mixer (M4A HLE if hooked — `tools/gba_m4a/`) → gpSP ring (accumulates,
**hard-clips at ±2047**, ×16 out) → `gba_pcm_submit()`: mono fold `(L+R)/2`,
hold-last on shortfall → **rate-following low-pass** (`gba_audio_filter.c`,
cutoff = 0.42 × `sound_fifo_rate_hz()`, ≥38 kHz bypasses bit-exact, rate-0
gaps KEEP the cutoff — toggling was a click) → volume ×(≤1.0) → SAI.

Sound bugs shipped and fixed, so far: resample imaging ("gritty", the filter),
PSG note-ons a fourth flat (65536 Hz constants duplicated in two files — one
formula, one definition now, `_Static_assert`-pinned), filter toggle seams.
Still open: an occasional note-tail crackle. The audio diary that hunted it
(`/gba_audio_diag.txt`) was removed when GBA work wrapped up — per-system
test logs now belong only to the systems in active bring-up. If the crackle
hunt resumes, restore it from git history (`git log -S gba_audio_diag`); the
design rule it encoded still stands: never write SD mid-play, flush only on
mute/quit. Host-side, 90 s of the Ruby opening through the exact device math
contains zero clicks: whatever it is, it is born on the device.

## Memory layout — the contract in one breath

cpu.o runs from ITCM, outside the sentinel scan: **nothing it references may
live in the flash blob.** The overlay glob keeps its callees in RAM; the M4A
transliterations (102 KB of resumable state machine) XIP from the blob and are
reached only through run pointers the load-time pass patches.
`test_gba_xip_contract.sh` counts the sentinels; `test_gba_m4a_wired.sh`
proves each piece is on its side and the hooks are actually wired. libm lives
in RESIDENT flash — a single `tanf()` once overflowed it by 1,412 bytes
(`gba_audio_filter.c` uses a Padé rational instead).

## Traps with scars attached

- `make … | tail` reports tail's exit code. Judge a build by its artifact.
- A multi-file change half-committed builds locally (working tree) and dies in
  CI (tag). `git status` after every commit.
- `gba_idle_loop.c` and `gba_over.h` semantics: 0 can mean FEAT_DISABLE, and
  per-argument scales differ (`load_gamepak` rtc vs serial).
- Korean patches keep the original cart header — and sometimes change the
  region letter (`BPEK`), which is a separate `gba_over.h` entry.
- The census once credited six games with an M4A mixer they never run (dead
  code, copied to IWRAM and all). Adoption now requires burned cycles. Our
  runtime hook is safe either way: a pc never reached never fires.
