# PCE / PCE CD

Guidance for working on the PC Engine / PC Engine CD emulator in this repo.
Loaded by Cursor via `.cursor/rules/pce.mdc` when editing files under `porting/pce/`,
`linux/pce/`, or `pce-go/`.

PCE CD is a fork extension on top of vendored `retro-go-stm32/pce-go/` (HuExpress-GO).
There is **no automated test suite** — use the Linux/SDL harness for fast iteration,
then verify on hardware.

## Linux harness (`linux/`)

```bash
cd linux
make -f Makefile.pce              # interactive play (main_play.c) → build/retro-go-pce
make -f Makefile.pce debug        # diagnostic harness (main.c): SCSI trace, save/load self-tests
make -f Makefile.pce PCE_ENABLE_ARCADE_CARD=0   # build without Arcade Card
./build/retro-go-pce --syscard syscard3.pce "cd_pce/game.cue"
```

- **`LINUX_EMU`** — host build flag (stdio, weak diag hooks, harness-only code paths). Not the same as Arcade Card enable.
- **`PCE_ENABLE_ARCADE_CARD`** — compile-time Arcade Card (`linux/pce/arcade_card.c` + `pce.c`/`pce.h` I/O routing). Default `1` in `Makefile.pce`. Runtime off: `PCE_ARCADE_CARD=0`.
- **Savestates in play mode:** F2 save / F4 load → `<cue>.state`. Autoload at boot: `GWAUTOLOAD=1`.
- **SCSI/ADPCM trace (debug build):** `PCE_TRACE=1`, output in `linux/pcecd_diag.txt`.

## Where the CD code lives

| Component | Path | Role |
|-----------|------|------|
| Machine core | `retro-go-stm32/pce-go/components/pce-go/` | CPU, VDC, PSG, memory map; `$1800` I/O dispatches to SCSI via `extern pce_scsi_*` |
| SCSI + ADPCM | `Core/Src/porting/pce/pce_scsi.c`, `pce_adpcm.c` | CD-ROM² registers, handshake, DMA ADPCM (Mednafen-derived) |
| Disc I/O | `Core/Src/porting/pce/pce_cd.c` | `.cue` parse, sector read from SD/file — **storage backend**, not hardware |
| Arcade Card | `linux/pce/arcade_card.c` (Linux only for now) | 2 MB RAM, banks `$40–$43`, I/O `$1A00–$1BFF` |
| Device glue | `Core/Src/porting/pce/main_pce.c` | ROM/CD mount, audio mix (CD-DA + ADPCM), savestates |
| Host glue | `linux/pce/main_play.c`, `main.c` | Same savestate layout as device; `main.c` has cold-resume self-tests |

**CD RAM banks:** `$68–$87` (256 KB Super CD) must point at real backing storage — see `pce_cd.h`. Without this, banks alias `NULLRAM` and games self-corrupt.

**Super System Card signature:** `$18C1=$AA`, `$18C2=$55` in `pce.c` — BIOS checks this after `EX_MEMOPEN`; missing it aborts Super CD boot.

## Savestate layout (device + Linux play mode)

Padded format (current device / `main_play.c`):

1. Header 8 B (`SAVESTATE_HEADER`)
2. `0` + ROM CRC 4 B
3. SVARs (`SaveStateVars[]`)
4. Pad to **76 KB** (`SAVE_STATE_BUFFER_SIZE`)
5. CD RAM 256 KB (banks `$68–$87`)
6. `CDDA` block + `ADPC` block (64 KB RAM + engine state) + `SCSX` (SCSI in-flight)
7. Optional `ARCD` (Arcade Card regs + 2 MB RAM if `ram_used`)

## ADPCM / SCSI pitfalls

## ADPCM / SCSI pitfalls

- **ADPCM engine is Mednafen-style** (`pce_adpcm_run` / `pce_adpcm_sync`): `$180A` read/write use `ReadPending`/`WritePending` (57 / 33 cycles). `EndReached` when `LengthCount` hits 0 during playback fetch or read-complete — **not** on manual `$180D` stop. Audio decode runs in `pce_adpcm_fill` at the programmed rate (not the CPU `bigdiv` burst path) to avoid FIFO underrun stutter on short SFX/voice.
- **Call `pce_adpcm_sync()` only on status/DMA polls** (`$1800`, `$1803`, `$180A`, `$180B`, `$180C`) plus once per frame before `Cycles` reset — not on every `$180x` read (IPL `$1808` path). Calling it on all register reads corrupts IPL/boot (`load error`).
- **ADPCM RAM is 64 KB double-buffered** — voice dropouts after ~4–5 s often mean DMA completion handshake (`HALF`/`END`, `effective_port3()`) or drain timing, not sector I/O.
- **`$180D` read** must return last command byte (`s_last_cmd`) — returning `0` breaks games that poll it.
- **CD-DA vs SCSI data** use separate file handles in `pce_cd.c` — sharing one handle thrashes audio when data and CD-DA tracks live in different `.bin` files.
- **Device diag logging** (`pcecd_diag.txt`): capped on device (FatFs); unbounded on Linux. Per-command `fopen` in the System Card poll loop can still be expensive — keep caps tight.

## CD volume / fader (`$180F`) debugging

Mednafen fades **out** CD-DA or ADPCM over 2.5 s / 6 s; **cancel** = write `$180F` without bit 3 (`0x00`–`0x07`). There is no hardware fade-in — games must cancel to restore full volume.

`pce_scsi.c` logs fader events to `pcecd_diag.txt` (Linux: `linux/pcecd_diag.txt`):

```bash
cd linux && rm -f pcecd_diag.txt
./build/retro-go-pce --syscard syscard3.pce "cd_pce/Dracula X.cue"
grep FADER pcecd_diag.txt
```

Look for:

- `FADE_START` / `FADE_CONT` — fade-out begun (check `target=CDDA` vs `ADPCM`, `cmd` byte)
- `CANCEL` — volume restored to 65536 (game un-muted)
- `CDDA_VOL_0` / `WARN_LOW_CDDAVOL` while `play=1` — **stuck silent**: game faded out and never cancelled before BGM
- `CDDA SAPSP` / `SAPEP` lines include `cdda_vol=` at play start

If Dracula X shows fade to 0 without a later `CANCEL`, the quiet audio is fader state — not a bad rip. Compare with Ys (may never touch `$180F` or cancels promptly).

## CD-DA level diagnostic

On the first `cdda_fill` with a half-full FIFO, `pcecd_diag.txt` logs:

```bash
grep CDDA_LEVEL pcecd_diag.txt
```

Two one-shot lines:

- `CDDA_LEVEL pregap` — first fill landed in INDEX 01 silence (`raw_peak` &lt; 512). Normal for track 3 of Dracula X (~21 sectors of digital zero after INDEX 01).
- `CDDA_LEVEL sound` — first fill with real audio (`raw_peak` ≥ 1024). **Use this line** to judge stream strength.

| `CDDA_LEVEL sound` | Meaning |
|--------------------|---------|
| `raw_peak` 2000–8000 | Normal CD-DA rip; if still quiet at the speaker, loss is downstream (mix/SDL) |
| `raw_peak` &lt; ~1000 | Weak rip or wrong offset |
| Only `pregap`, never `sound` | Playback ended before music sector, or FIFO/read failure |

## CD peak meters (`$1805` / `$1806`)

Games latch the current CD-DA level by **writing** `$1805` or `$1806` (value ignored), then **read** low/high bytes per channel. `$1803` read toggles bit 1 (L/R select). Mednafen stores `abs(sample) * CDDAFadeVolume >> 16`.

Linux trace:

```bash
grep PEAK pcecd_diag.txt
```

`PEAK_W` / `PEAK_R` are **not** capped by the 130-line `Ir`/`Iw` boot trace — they log up to 64 peak-register touches anytime in the session. `Ir5`/`Iw5` in the general trace only cover System Card boot polling.

Non-zero `L=` / `R=` on `PEAK_W` while `play=1` confirms the meter path. **No `PEAK_*` lines at all** means the game never touched `$1805`/`$1806` (Dracula X may not use them for in-game BGM).

## Arcade Card

- Hardware: 2 MB RAM, CPU window banks **`$40–$43`** via sentinel `PCE_ACAREA_MARKER`, I/O **`$1A00–$1BFF`** (`A & 0x1E00 == 0x1A00`).
- **Physical reads must use full 32-bit address** `(MMR[page] << 13) | (addr & 0x1FFF)` in `pce_read8`/`pce_write8` macros — passing only the low 13 bits corrupts streamed video (e.g. Sapphire intro).
- Enabled automatically on `.cue` load when compiled in; needs appropriate System Card BIOS (e.g. Arcade Card Pro) for games that detect it.
- Savestate block `ARCD` is optional/trailing — older saves without it still load (probe + `fseek` back).

## Reference

Mednafen PCE CD / Arcade Card under `external/mednafen/src/` (`pce_fast/pcecd*.c`, `hw_misc/arcade_card/`) is the semantic reference when behaviour is unclear. That tree may need to be added locally if not present.
