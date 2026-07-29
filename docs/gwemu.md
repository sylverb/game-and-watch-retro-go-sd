# GWEMU (Game & Watch Emulator) Toolchain

`gwemu` provides a comprehensive emulation environment for the STM32H7B0 based Game & Watch system using a custom QEMU fork. It includes timeline recording for deterministic input sequences and headless Docker playback for video capture.

## Dependencies

To run `gwemu` locally, you will need standard build tools for the firmware and a few utilities for the wrapper scripts.

### Ubuntu / Debian (apt-get)
```bash
sudo apt-get install -y \
    build-essential \
    gcc-arm-none-eabi \
    gdb-multiarch \
    python3 \
    python3-pip \
    curl \
    wget \
    unzip \
    mtools
```

### macOS (Homebrew)
```bash
brew install \
    arm-none-eabi-gcc \
    arm-none-eabi-gdb \
    python3 \
    curl \
    wget \
    unzip \
    mtools
```

## Build Stages (Makefile Integration)

The `game-and-watch-retro-go-sd` repository integrates with `gwemu` via several targets in `Makefile.common`:

- `make gwemu_release`: Builds the QEMU media images (`qemu_bank1.bin`, `qemu_bank2.bin`, `extflash.bin`, and for SD builds `sdcard.img`) from the compiled firmware. It honours `SD_CARD`:
  - **`SD_CARD=1`** — bank images plus a FAT32 `sdcard.img` populated from `sd_content/`. External flash is unused at runtime (cores stream from the card), so `extflash.bin` is a blank 64 MB blob.
  - **`SD_CARD=0`** (FrogFS) — no SD image. The firmware reads its filesystem out of external flash, so `extflash.bin` is built to the **full chip size** (`EXTFLASH_OFFSET + EXTFLASH_SIZE`, which the Makefile already forces to a power of two) with `frogfs.bin` written at `EXTFLASH_OFFSET` and `littlefs.bin` at `FILESYSTEM_FLASH_OFFSET` — the same layout `make flash` writes to hardware via gnwmanager. A blank blob would boot the firmware against an external flash containing no filesystem, which is not what runs on the device.

  Build the right targets for your variant:
  ```bash
  # SD card build
  make -j$(nproc) <params> release gwemu_release

  # Flash-only (FrogFS) build
  make -j$(nproc) <params> frogfs_image littlefs_image gwemu_release
  ```
  Note `all` is not the right target for either: it builds `gw_retro_go_extflash.bin`, which the release/flash paths do not use.
- `make gwemu_download`: Downloads the appropriate native gwemu binary (macOS `.app` or Linux `.AppImage`) to `./gwemu_bin` in the repo root. It is a no-op when the binary is already present, so it costs nothing on repeat invocations.
- `make gwemu_update`: Forces a check against the latest `slash-proc/gwemu` release and re-downloads if a newer tag is available. Equivalent to `run_gwemu.sh --update`.
- `make gwemu_interactive`: Calls `gwemu_release` and `gwemu_download`, then launches the emulator via `./scripts/run_gwemu.sh`.
- `make gwemu_interactive_gdb`: Same as above, but passes `--gdb` to open an interactive debugging session.

Note that `gwemu_release` skips image preparation entirely if `build/sdcard.img` already exists. After changing anything that lands on the SD card, or any flash-layout variable (`INTFLASH_BANK`, `EXTFLASH_*`, `GNW_TARGET`), pass `--reset` to `run_gwemu.sh` (or delete `build/sdcard.img`) so the images are rebuilt.

### The gwemu binary
`gwemu_bin` lives in the **repo root**, not under `build/`, and is gitignored. It is a ~16 MB download unrelated to build output, and keeping it in `build/` meant every `make clean` threw it away and forced a re-download.

The installed version is recorded in `gwemu_bin.version`. gwemu is still moving quickly, so the tooling tracks the latest release rather than a pin:

- `./scripts/run_gwemu.sh --update` (or `make gwemu_update`) checks for and installs a newer release before running.
- `GWEMU_TAG=v0.0.18 make gwemu_download` pins a specific tag — use this once things stabilise.
- If the network is unreachable but a binary is already present, the download step warns and continues rather than failing the run.

## Usage: `run_gwemu.sh`

The `run_gwemu.sh` script is the primary entry point for launching the emulator. It manages the QEMU binary execution, binds SD card and Flash images, and handles the debugging/GDB lifecycle.

### Local Execution (Native)
Run the emulator natively with the custom SDL3 display driver (`-display gwemu`):
`./scripts/run_gwemu.sh`

### GDB Behavior and Logging
By default, `run_gwemu.sh` runs QEMU in the background and attaches GDB in batch mode via `scripts/gwemu_log.gdb`. This acts as a log-forwarder and an exception trap, both writing to your terminal's stdout.

**QEMU is always started suspended (`-S`) whenever a GDB session will attach** — including the default batch log mode, not just `--gdb`. This matters: without it, GDB attaches a second or two into the boot and silently misses everything retro-go printed on the way up, which looks exactly like "logging is broken".
The one exception is headless Docker, which runs without an attached GDB and therefore must not be suspended.

`scripts/gwemu_log.gdb` does two things:

- **Log forwarding** — breaks on `_write()` in `Core/Src/syscalls.c`, where retro-go's `stdout`/`stderr` funnel through, prints the exact bytes handed to it, and resumes. Every write is observed once, in order, with no dependence on wall-clock timing.
  *Do not replace this with a loop that polls `log_idx`/`logbuf`.* Two such designs have been tried and both failed: a blocking `continue` followed by a `while` loop never reaches the loop at all, and an `interrupt`-every-second async variant races the target and drops output.
- **Fault trapping** — breaks on `common_fault_handler_c()` in `Core/Src/stm32h7xx_it.c`, the single choke point every hard/bus/usage/mem fault funnels through before `BSOD()` paints the screen. It dumps the stacked exception frame, `CFSR`/`HFSR`/`MMFAR`/`BFAR`/`ABFSR`, the faulting PC resolved to a symbol, a backtrace, and all registers, then exits non-zero. `__assert_func` is trapped the same way.
  This puts the whole fault report on stdout, so you never have to read a BSOD off the emulated LCD.

If you need to interactively debug:
`./scripts/run_gwemu.sh --gdb`
This drops you into a fully interactive GDB session connected to QEMU's gdbstub on port 1234.

For a one-off diagnostic (probing a variable, dumping MPU registers, breaking somewhere specific), write a small `.gdb` file and run it through the harness rather than hand-rolling a QEMU invocation:
`./scripts/run_gwemu.sh --gdb-script scripts/my_probe.gdb`

Two gotchas when writing these scripts:
- GDB's `printf` does **not** support `%.*s`. To print a counted, non-NUL-terminated buffer, loop and emit `%c` per byte.
- Always run the emulator with a real display (the native path uses `-display gwemu`). `-display none` is only appropriate for Docker and for tight iteration on a known issue where the fault report alone is sufficient and visual inspection adds nothing.

### Timeline Recording
Record your exact keypresses (sub-frame accurate) into a `.tl` (timeline) file.
**Note**: Recording requires the local SDL3 window and cannot be done via Docker.
`./scripts/run_gwemu.sh --record scripts/my_recording.tl`

### Timeline Playback
Replay a recorded timeline locally:
`./scripts/run_gwemu.sh --timeline scripts/my_recording.tl`

### Headless Docker Playback and Video Export
Docker is used to run the emulator headlessly, guaranteeing a reproducible CI-friendly environment. You can use it to playback timelines and simultaneously export perfect MP4 video captures.
`./scripts/run_gwemu.sh --docker --timeline scripts/my_recording.tl --video my_playback.mp4`
