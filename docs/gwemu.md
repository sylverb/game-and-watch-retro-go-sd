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

**The emulator images are built from a real release.** For `SD_CARD=1`, `gwemu_release` depends on the `release` target itself, so `make gwemu_release` alone produces `firmware_update.bin`, `gw_update.tar` and the release package, and the images gwemu boots come out of exactly those steps. This is deliberate: gwemu and hardware must be built from **one invocation with one set of variables**. Building `release` with one set of flags and `gwemu_release` with another would have the emulator quietly running different firmware than the device, making any comparison between them worthless. (`SD_CARD=0` cannot produce a distributable release — it needs proprietary blobs — so it depends on `frogfs_image` / `littlefs_image`, which is what `make flash` writes to hardware.)

Every run writes `build/gwemu_build_info.txt` recording the parameters used and the md5 of each image:

```
GNW_TARGET                = mario
SD_CARD                   = 1
INTFLASH_BANK             = 1
EXTFLASH_OFFSET           = 0
...
intflash.bin md5          = 48a0efbea0bc289b8c272067e3a2c8a5
qemu_bank1.bin md5 = a014363987ee8e18a6712e82deb3065b
```

When the emulator and the device disagree, the first question is always "was it actually the same firmware?" — compare that md5 with what gnwmanager flashed and the question is settled immediately.
- `make gwemu_download`: Downloads the appropriate native gwemu binary (macOS `.app` or Linux `.AppImage`) to `./gwemu_bin` in the repo root. It is a no-op when the binary is already present, so it costs nothing on repeat invocations.
- `make gwemu_update`: Forces a check against the latest `slash-proc/gwemu` release and re-downloads if a newer tag is available. Equivalent to `run_gwemu.sh --update`.
- `make gwemu_interactive`: Calls `gwemu_release` and `gwemu_download`, then launches the emulator via `./scripts/run_gwemu.sh`.
- `make gwemu_interactive_gdb`: Same as above, but passes `--gdb` to open an interactive debugging session.

Note that `gwemu_release` skips image preparation entirely if `build/sdcard.img` already exists. After changing anything that lands on the SD card, or any flash-layout variable (`INTFLASH_BANK`, `EXTFLASH_*`, `GNW_TARGET`), pass `--reset` to `run_gwemu.sh` (or delete `build/sdcard.img`) so the images are rebuilt.

### The gwemu binary
`gwemu_bin` lives in the **repo root**, not under `build/`, and is gitignored. It is a ~16 MB download unrelated to build output, and keeping it in `build/` meant every `make clean` threw it away and forced a re-download.

The installed version is recorded in `gwemu_bin.version`. gwemu is still moving quickly, so the tooling tracks the latest release rather than a pin:

- `./scripts/run_gwemu.sh --update` (or `make gwemu_update`) checks for and installs a newer release before running.
- `GWEMU_TAG=v0.0.18 make gwemu_download` pins a specific tag — use this once things stabilise. A pin never touches the network.

**Release lookups are cached for an hour.** GitHub rate-limits unauthenticated API calls to 60/hour/IP, and the Docker path used to ask for the latest tag on every single run, so a handful of iterations could exhaust the budget — which surfaces as an empty tag rather than an obvious error. All lookups now go through `scripts/gwemu_latest_tag.sh`, which caches the answer in `.gwemu_release_cache` (gitignored) and reuses it for 60 minutes. If a lookup fails while a stale cache exists, the stale tag is used and a warning goes to stderr rather than silently returning nothing.

- `scripts/gwemu_latest_tag.sh --force` (or `make gwemu_update GWEMU_FORCE_CHECK=1`) bypasses the cache — use it right after a new release lands.
- `GWEMU_RELEASE_TTL=<minutes>` changes the lifetime; `0` disables caching.
- If the network is unreachable but a binary is already present, the download step warns and continues rather than failing the run.

## Usage: `run_gwemu.sh`

The `run_gwemu.sh` script is the primary entry point for launching the emulator. It manages the QEMU binary execution, binds SD card and Flash images, and handles the debugging/GDB lifecycle.

### Local Execution (Native)
Run the emulator natively with the custom SDL3 display driver (`-display gwemu`):
`./scripts/run_gwemu.sh`

### Logging and exception capture (always on)

Every run forwards retro-go's log buffer live to stdout and traps exceptions. This is not
optional and not mode-dependent -- it applies to native runs, `--docker`, and timeline
playback alike. Without it there is little point running the firmware under emulation.

Output also goes to a log file: `./gwemu.log` by default, overwritten each run. Use
`--log-file <path>` to write elsewhere. It sits at the repo root rather than under `build/`
so that `make clean` does not delete the log of the run you are trying to read; `*.log` is
gitignored.

If a fault or failed assertion is trapped, the run **exits non-zero**, so CI (or a caller)
can detect a crashed session without parsing the log.

QEMU is always started suspended (`-S`) and GDB attaches before the firmware runs. Without
this the forwarder attaches a second or two into the boot and silently misses everything
retro-go printed on the way up -- which reads as "logging is broken".

`scripts/gwemu_log.gdb` does two things:

- **Log forwarding** -- breaks on `_write()` in `Core/Src/syscalls.c`, where retro-go's
  `stdout`/`stderr` funnel through, prints the exact bytes, and resumes. Every write is
  observed once, in order, with no dependence on wall-clock timing.
  *Do not replace this with a loop that polls `log_idx`/`logbuf`.* Two such designs have
  been tried and both failed: a blocking `continue` followed by a `while` loop never
  reaches the loop at all, and an `interrupt`-every-second async variant races the target
  and drops output.
- **Exception trapping** -- breaks on `common_fault_handler_c()` in
  `Core/Src/stm32h7xx_it.c`, the choke point every hard/bus/usage/mem fault passes through
  before `BSOD()` paints the screen. It dumps the stacked exception frame,
  `CFSR`/`HFSR`/`MMFAR`/`BFAR`/`ABFSR`, the symbolized faulting PC, a backtrace and all
  registers, then exits non-zero. `__assert_func` is trapped the same way. You never have
  to read a BSOD off the emulated LCD.

Overrides, for the rare cases they are wanted:

- `--gdb` -- interactive GDB session instead of the batch forwarder.
- `--gdb-script <file.gdb>` -- run your own script instead of the default forwarder, for
  one-off diagnostics (probing a variable, dumping MPU registers). Note the default
  already covers logs and exceptions, so this is not needed for ordinary runs.

Two gotchas when writing such scripts:
- GDB's `printf` does **not** support `%.*s`. To print a counted, non-NUL-terminated
  buffer, loop and emit `%c` per byte.
- Use `hbreak`, not `break`, for flash addresses when debugging **real hardware**; under
  emulation either works.

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
