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

- `make gwemu_release`: Compiles the retro-go firmware into the expected `qemu_bank1.bin`, `qemu_bank2.bin`, and `extflash.bin` images, and prepares an SD card image (`sdcard.img`).
- `make gwemu_download`: Automatically queries GitHub for the latest `slash-proc/gwemu` release and downloads the appropriate native binary (macOS `.app` or Linux `.AppImage`) into `build/gwemu_bin`.
- `make gwemu_interactive`: Calls `gwemu_release` and `gwemu_download`, then launches the emulator via `./scripts/run_gwemu.sh`.
- `make gwemu_interactive_gdb`: Same as above, but passes `--gdb` to open an interactive debugging session.

## Usage: `run_gwemu.sh`

The `run_gwemu.sh` script is the primary entry point for launching the emulator. It manages the QEMU binary execution, binds SD card and Flash images, and handles the debugging/GDB lifecycle.

### Local Execution (Native)
Run the emulator natively with the custom SDL3 display driver (`-display gwemu`):
`./scripts/run_gwemu.sh`

### GDB Behavior and Logging
By default, `run_gwemu.sh` runs QEMU in the background and attaches GDB in batch mode via `scripts/gwemu_log.gdb`. This acts as a log-forwarder. Specifically, it uses GDB to attach to QEMU, set a breakpoint or watchpoint on retro-go's internal log buffer, and reads out retro-go's log buffer directly to your terminal's stdout by default.

If you need to interactively debug:
`./scripts/run_gwemu.sh --gdb`
This passes `-S` to QEMU (suspending execution on launch) and drops you into a fully interactive GDB session connected to QEMU's gdbstub on port 1234.

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
