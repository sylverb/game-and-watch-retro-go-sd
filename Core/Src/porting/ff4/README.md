# FF4 G&W port — integration glue

This directory contains the integration glue between the retro-go-sd
firmware and the native-C FF4 reimplementation that lives in
[`hcross/ff4-gnw`](https://github.com/hcross/ff4-gnw)
(linked here as `external/ff4`).

The branch `feat/ff4-port-scaffold` carries the FF4-specific changes
on top of the upstream retro-go-sd. The base SD-card fork remains
[`sylverb/game-and-watch-retro-go-sd`](https://github.com/sylverb/game-and-watch-retro-go-sd);
this fork tracks it for everything not under `external/ff4/` and this
directory.

## What's in here

| File           | Role                                                          |
|----------------|---------------------------------------------------------------|
| `main_ff4.c`   | FF4 emulator entry (`app_main_ff4`). Caches the ROM via `odroid_overlay_cache_file_in_flash`, initialises LakeSnes, runs the frame loop (`ff4_step` + `ff4_blit_to_lcd`), reads the gamepad, services the watchdog. Behind `#ifdef FF4_AUTOBOOT` it also: skips the retro-go launcher straight into FF4, emits five greppable serial diagnostic markers, and exposes the `Snes *ff4_snes` so the diagnostic harness can read the live PPU / APU / CPU state. |

## FF4_AUTOBOOT diagnostic harness

`main_ff4.c` builds without ceremony in the normal retro-go path
(`-DFF4_AUTOBOOT` undefined). Adding `FF4_AUTOBOOT=1` to the make
command turns the FF4 entry into an autonomous test harness used by
the upstream pipeline at [`hcross/ff4-port`](https://github.com/hcross/ff4-port):

```bash
make flash SD_CARD=0 FF4_AUTOBOOT=1 EXTFLASH_SIZE_MB=4 -j8
gnwmanager monitor   # observe the markers below
```

The harness emits five markers on the serial UART:

| Marker                         | Cadence                | Payload                                                |
|--------------------------------|------------------------|--------------------------------------------------------|
| `FF4_AUTOBOOT_ATTEMPT_*`       | once at boot           | confirms the autoboot path was taken                   |
| `FF4_BOOT_MARKER_*`            | once at `app_main_ff4` | confirms FF4 was reached                               |
| `FF4_DIAG_PPU_*`               | every 50 host frames   | `nmiEn`, `inVbl`, `forceBlank`, `bright`, `vPos`       |
| `FF4_DIAG_APU_*`               | every 50 (offset 25)   | `inPorts[0..3]` and `outPorts[0..3]` (SPC mailbox)     |
| `FF4_DIAG_PCHIST_*`            | every 250 frames       | PC bank histogram + last PC                            |
| `FF4_DIAG_MISS_*`              | every 100 frames       | cumulative dispatch hits / misses + 8-entry ring of unique unmatched PCs |
| `FF4_DIAG_ALIVE_*`             | every 60 frames        | heartbeat (`snes_cyc_hi`, `lo`, `snes_frm`, current PC)|

Plus the existing every-5-frame `FF4 live: host=H snes_frame=F
dispatch=Hits/Total wall_ms=W` block.

These markers let `ff4-port/translator/port_validated.py` and
`ff4-port/translator/volume_iterate.py` measure the dispatch hit
rate, detect crashes, and decide adopt-vs-revert on each chunk of
newly-translated routines — with no human in the loop.

## Building

The FF4 build path uses the SD-card-free FrogFS+LittleFS layout
(`SD_CARD=0`). The user provides their FF4 JP 1.1 ROM (CRC32
`CAA15E97`) at `sd_content/roms/homebrew/ff4.sfc`; the build packs it
into the FrogFS image at make time.

```bash
make flash SD_CARD=0 EXTFLASH_SIZE_MB=4 -j8           # normal path
make flash SD_CARD=0 FF4_AUTOBOOT=1 EXTFLASH_SIZE_MB=4 -j8  # diagnostic
```

## Status

Boots through the Square Enix splash to the title screen on real
hardware (G&W Mario via JTAG, 64 MB extflash mod). 168 routines from
the FF4 dispatch table are now linked in via `external/ff4`. See
`hcross/ff4-gnw/README.md` for the per-module breakdown and the
running hit-rate measurement.
