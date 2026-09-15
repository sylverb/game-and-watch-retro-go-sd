# GWEMU — running retro-go under emulation

`gwemu` is a QEMU fork that emulates the STM32H7B0-based Game & Watch. It lets you boot the
firmware you just built, read its log output, and catch faults — without a device, a debug
probe, or a screwdriver. It also supports deterministic input playback and headless video
capture for reproducible tests.

The guiding rule of this integration: **the emulator must run the same firmware, laid out
the same way, as the hardware.** Everything below exists to keep that true. When gwemu and
a real device disagree, that disagreement only means something if they were running the
same bytes.

## Dependencies

Firmware build tools plus a few utilities for the wrapper scripts.

### Ubuntu / Debian
```bash
sudo apt-get install -y build-essential gcc-arm-none-eabi gdb-multiarch \
    python3 python3-pip curl wget unzip mtools
```

### macOS

```bash
brew install mtools wget python3
```

**Do not `brew install arm-none-eabi-gcc`.** The homebrew-core formula is built
without newlib, so it has no libc at all and the very first compile dies with
`fatal error: stdint.h: No such file or directory`. Use Arm's own toolchain
instead — either the cask (needs an admin password for its `.pkg`):

```bash
brew install --cask gcc-arm-embedded
```

or, preferably, let the repo fetch the right one for your host — it reads the
pin straight out of `Dockerfile`, picks the correct architecture, verifies the
download, and prints the `PATH` line to use:

```bash
./scripts/get_toolchain.sh          # installs into ~/opt
```

It works on Linux too, and needs no admin rights. Equivalent by hand:

```bash
VER=15.2.rel1                    # must match ARM_COMPILER_VERSION in Dockerfile
ARCH=darwin-arm64                # see the Intel note below
mkdir -p ~/opt && cd ~/opt
curl -fSLO "https://developer.arm.com/-/media/Files/downloads/gnu/$VER/binrel/arm-gnu-toolchain-$VER-$ARCH-arm-none-eabi.tar.xz"
tar xf "arm-gnu-toolchain-$VER-$ARCH-arm-none-eabi.tar.xz"
export PATH="$HOME/opt/arm-gnu-toolchain-$VER-$ARCH-arm-none-eabi/bin:$PATH"
```

> If you verify a download by hand, note that **Arm's checksum files are
> misnamed**: `.sha256asc` holds the real SHA-256, while `.sha256` and `.asc`
> both hold an MD5. `get_toolchain.sh` picks the algorithm by digest length
> rather than trusting the extension.

**Match `ARM_COMPILER_VERSION` in `Dockerfile` (15.2.rel1), not merely "v10+".**
Codegen differences between toolchain versions are not academic here: `CLAUDE.md`
documents a 15.2 argument-marshalling bug in `memmove` that reproduces only on
that compiler. A local build on a different version is not the firmware CI
produces, which undercuts the whole point of comparing emulator against hardware.

> **Intel Macs cannot match CI.** Arm's last `darwin-x86_64` build is
> **14.2.rel1**; 14.3.rel1 and 15.x are Apple Silicon only (verified — the
> `darwin-x86_64` URLs 404). On an Intel Mac you therefore have a choice:
> 14.2.rel1 locally and accept that your codegen differs from CI, or **`make
> docker`**, which pulls the pinned 15.2.rel1 toolchain regardless of host
> architecture. Use Docker for anything where codegen fidelity matters —
> chasing a fault, or comparing against hardware. 14.2 is fine for
> "does it build and boot".

(Alternatively, leave it off `PATH` and pass `GCC_PATH=<...>/bin` on the make
command line.) The bundled `arm-none-eabi-gdb` works as-is, so `gdb-multiarch`
is not needed. The `gcc-arm-embedded` cask tracks whatever Arm ships as current
— check `arm-none-eabi-gcc --version` and prefer the tarball if it does not
match the pin.

`parted` is deliberately absent from both lists: it is Linux-only and has no
Homebrew formula, so the SD image is built by `scripts/make_sdcard_image.py`
instead. Nothing else in this workflow needs it.

## Quick start

```bash
# SD card build
make -j$(nproc) <your usual params> release gwemu_release
./scripts/run_gwemu.sh

# Flash-only (FrogFS) build
make -j$(nproc) <your usual params> frogfs_image littlefs_image gwemu_release
./scripts/run_gwemu.sh
```

> `nproc` is GNU coreutils and does not exist on macOS — use
> `-j$(sysctl -n hw.ncpu)` there, or just a literal `-j8`. (Inside the Makefile
> this is already handled by `$(NPROC)`; it is only the command line you type
> that needs adjusting.)

That is the whole workflow. The emulator boots, retro-go's log appears on your terminal,
and if the firmware faults you get a full exception report instead of a frozen screen.

> **Do not use `make all`.** It builds `gw_retro_go_extflash.bin`, which neither the release
> path nor the flash path uses, and which is not a meaningful artifact for either variant.

## The two build variants

`SD_CARD` is not a feature toggle — it selects a different firmware layout, a different
linker script, and a different place for the filesystem to live. Almost everything else in
this document branches on it, so establish which one you are building first.

| | `SD_CARD=1` (SD card) | `SD_CARD=0` (flash-only, FrogFS) |
|---|---|---|
| Linker script | `STM32H7B0VBTx_SDCARD.ld` | `STM32H7B0VBTx_FLASH.ld` |
| Filesystem lives in | the SD card | external flash (FrogFS read-only + LittleFS read/write) |
| Emulator cores | streamed from `/cores/` on the card | packed into the LittleFS partition |
| External flash at runtime | unused | holds everything |
| Distributable release | yes (`make release`) | no — needs proprietary blobs, `release` errors |
| Build for gwemu | `release gwemu_release` | `frogfs_image littlefs_image gwemu_release` |
| Flash to hardware | `make flash_sd` (internal flash + `sdpush` of each core) | `make flash` (internal flash + `frogfs.bin` and `littlefs.bin` at their offsets) |

To compare emulator against hardware, use the matching pair from the last two rows — built
from one invocation with one set of variables. Anything else is comparing two different
firmwares.

### Variables that only matter for `SD_CARD=0`

FrogFS builds place the filesystem in external flash, so they need to know the chip layout:

- **`EXTFLASH_OFFSET`** — byte offset where retro-go's partition begins. Non-zero when
  something else (typically original firmware) occupies the start of the chip.
- **`EXTFLASH_SIZE_MB`** — size of retro-go's partition **after** the offset. Not the chip
  size. This trips people up: on a 64 MB chip with a 24 MB offset you want `40`, not `64`.
- `EXTFLASH_OFFSET + EXTFLASH_SIZE` **must land on a power-of-two boundary**, which the
  build enforces with a hard error. It is how the layout is expected to end at the chip
  boundary.
- **`FILESYSTEM_SIZE`** defaults to 10% of the partition, rounded down to 4096, and the
  LittleFS partition sits at the end of it. `FILESYSTEM_FLASH_OFFSET` is derived; you do not
  set it, but it is recorded in the build info and is where `littlefs.bin` gets written.

A worked example, for a 64 MB chip with 24 MB reserved at the start:

```bash
make -j$(nproc) SD_CARD=0 INTFLASH_BANK=1 \
     EXTFLASH_OFFSET=25165824 EXTFLASH_SIZE_MB=40 \
     frogfs_image littlefs_image gwemu_release
```

### `INTFLASH_BANK` and what gwemu needs

`INTFLASH_BANK=1` links retro-go for `0x08000000`; `INTFLASH_BANK=2` links it for
`0x08100000`, which is the dual-boot arrangement where patched original firmware occupies
bank 1.

For the emulator this matters: a bank 2 build needs *something* in bank 1 or the machine has
nothing to boot. `gwemu_release` looks for `backup/internal_flash_backup_mario.bin` or
`backup/internal_flash_backup_zelda.bin` and uses it for `qemu_bank1.bin`. Without one it
warns and leaves bank 1 empty, which will not boot the way hardware does. If you are
testing a bank 2 build, put your OFW backup there first.

### Changing layout variables

`SD_CARD`, `INTFLASH_BANK`, `EXTFLASH_*` and `GNW_TARGET` change the binary layout, not just
behaviour. **Run `make clean` when you change any of them.** Stale objects from a previous
layout link into a firmware that looks fine and behaves strangely — and the resulting
debugging session will not be about the thing you changed.

## How the pieces fit together

### `make gwemu_release` — building the media

Produces the images the emulator boots: `qemu_bank1.bin`, `qemu_bank2.bin`, `extflash.bin`,
and for SD builds `sdcard.img`.

**It is driven by a real release.** For `SD_CARD=1` this target *depends on* `release`, so
the images come out of exactly the steps that produce a distributable build. This is
deliberate and it is the point: gwemu and hardware must come from **one invocation with one
set of variables**. Building `release` with one set of flags and `gwemu_release` with
another would have the emulator quietly running different firmware than your device, and
any conclusion drawn from comparing them would be worthless.

`SD_CARD=0` cannot produce a distributable release (it needs proprietary blobs), so it
depends on `frogfs_image` / `littlefs_image` instead — which is precisely what `make flash`
writes to the device.

**External flash mirrors the device layout**, because the two variants use it differently:

| variant | external flash contents |
|---|---|
| `SD_CARD=1` | Unused at runtime — cores stream from the SD card. A blank 64 MB blob. |
| `SD_CARD=0` | The filesystem lives here. Sized to the whole chip (`EXTFLASH_OFFSET + EXTFLASH_SIZE`), with `frogfs.bin` at `EXTFLASH_OFFSET` and `littlefs.bin` at `FILESYSTEM_FLASH_OFFSET` — the same layout gnwmanager writes. |

Handing a FrogFS build a blank external flash would boot it against a filesystem that isn't
there. That configuration does not exist on hardware, so it is not worth testing. Both
offsets must be 4096-aligned; the target checks and fails loudly rather than writing a
subtly misplaced image.

**Every run records what it built** in `build/gwemu_build_info.txt` — the build variables
and the md5 of each image, including `intflash.bin`:

```
GNW_TARGET                = mario
SD_CARD                   = 1
INTFLASH_BANK             = 1
EXTFLASH_OFFSET           = 0
COVERFLOW                 = 1
...
intflash.bin md5          = 48a0efbea0bc289b8c272067e3a2c8a5
qemu_bank1.bin md5 = a014363987ee8e18a6712e82deb3065b
```

When the emulator and the device behave differently, the first question is always "was it
even the same firmware?" Compare that md5 with what gnwmanager flashed and you have the
answer immediately instead of inferring it.

### The emulated storage is writable and persists

`extflash.bin` and `sdcard.img` are the device's storage, and the firmware writes to them —
config, the LittleFS `/cores` partition, save data. Those writes survive between runs, just
as they do on real hardware. **Runs are therefore not idempotent**, and accumulated state
can change behaviour: a tree whose emulator list was full can come up with fewer entries
because a previous boot rewrote the filesystem.

If a run looks wrong, check `build/gwemu_build_info.txt` first. If the firmware md5 still
matches but `extflash.bin` or `sdcard.img` no longer do, the guest mutated its storage and
you are looking at accumulated state rather than a code change.

`./scripts/run_gwemu.sh --reset` wipes the media and rebuilds it, reusing the variables
recorded in `build/gwemu_build_info.txt` so the configuration cannot drift. Use it after
changing anything that lands on the SD card or any flash-layout variable, and whenever you
want a clean slate.

`gwemu_release` otherwise skips regenerating media that already exists.

### `run_gwemu.sh` — running it

Launches the emulator, attaches GDB, and gives you logs and exceptions.

| flag | effect |
|---|---|
| `--docker` | Run headless in a container. Reproducible, CI-friendly. |
| `--gdb` | Interactive GDB session instead of the batch forwarder. |
| `--gdb-script <f>` | Run your own GDB script instead of the default forwarder. One-off diagnostics only. |
| `--log-file <path>` | Session log destination (default `./gwemu.log`). |
| `--timeline <f.tl>` | Play back a recorded input timeline. |
| `--record <f.tl>` | Record an input timeline. Local only — needs the SDL window. |
| `--video <f.mp4>` | Export an MP4. Requires `--docker` and `--timeline`. |
| `--qmp <port>` | Expose QEMU's monitor protocol. |
| `--reset` | Wipe and rebuild the emulator media. |
| `--update` | Fetch the newest gwemu release before running. |

### Logging and exception capture

**Always on, in every mode**, including `--docker`. Not optional and not configurable — a
run that silently swallows a fault is worse than no run at all.

- **Log forwarding** — GDB breaks on `_write()` in `Core/Src/syscalls.c`, where retro-go's
  `stdout`/`stderr` funnel through, prints the bytes, and resumes. Every write is observed
  once, in order, with no dependence on timing.
- **Exception trapping** — breaks on `common_fault_handler_c()` in
  `Core/Src/stm32h7xx_it.c`, the choke point every hard/bus/usage/mem fault passes through
  before `BSOD()` paints the screen. Dumps the stacked exception frame,
  `CFSR`/`HFSR`/`MMFAR`/`BFAR`/`ABFSR`, the symbolized faulting PC, a backtrace and all
  registers. `__assert_func` is trapped the same way. You never read a BSOD off the
  emulated LCD.
- **Exit status** — a trapped fault or failed assertion exits non-zero, so CI can detect a
  crashed run without parsing text.
- **Session log** — everything also goes to `./gwemu.log`, overwritten each run. It sits at
  the repo root, not under `build/`, so `make clean` cannot delete the log you are reading.
  `*.log` is gitignored.

QEMU always starts suspended (`-S`) with GDB attached before the firmware runs. Retro-go
prints most of its boot output in the first second or two; attaching afterwards misses all
of it, which looks exactly like "logging is broken".

**If you write your own GDB script**, two things will bite you:

- GDB's `printf` does not support `%.*s`. To print a counted, non-NUL-terminated buffer,
  loop and emit `%c` per byte.
- Do not poll `log_idx`/`logbuf` in a loop. Two such designs have been tried and both
  failed — a blocking `continue` followed by a `while` loop never reaches the loop, and an
  `interrupt`-every-second variant races the target and drops output. Break on `_write`
  instead.

### The emulator binary and release lookups

`gwemu_bin` is a ~16 MB download and has nothing to do with build output, so it is kept at
the **repo root** and therefore **survives `make clean`** — you do not need to re-fetch it
after a clean build. The installed tag is recorded in `gwemu_bin.version`. Both files are
gitignored, as is `gwemu.log`.

On Linux `gwemu_bin` is the downloaded AppImage itself. On macOS the release is an
`.app` bundle whose executable loads its dylibs from a sibling `Frameworks/` directory,
so the bundle is unpacked to `gwemu.app/` at the repo root and `gwemu_bin` is a symlink
into it. Either way `./gwemu_bin` is the thing you run, and `gwemu.app/` is gitignored
too — just do not move `gwemu_bin` away from the bundle on macOS, or it will not load.

gwemu moves quickly, so the tooling tracks the latest release rather than pinning:

```bash
make gwemu_download                      # fetch if missing (no-op when present)
make gwemu_update                        # check for and install a newer release
./scripts/run_gwemu.sh --update
GWEMU_TAG=v0.0.19 make gwemu_download    # pin a tag; never touches the network
```

**Release lookups are cached for an hour.** GitHub rate-limits unauthenticated API calls to
60/hour/IP and the Docker path needs the tag on every run. Exceeding the limit makes the
API return nothing, which surfaces as a confusing "image not found" rather than an obvious
error. All lookups go through `scripts/gwemu_latest_tag.sh`, which caches in
`.gwemu_release_cache`; if a lookup fails while a stale cache exists it uses the stale tag
and warns on stderr rather than returning nothing.

- `scripts/gwemu_latest_tag.sh --force`, or `make gwemu_update GWEMU_FORCE_CHECK=1` —
  bypass the cache, e.g. right after a release lands.
- `GWEMU_RELEASE_TTL=<minutes>` — change the lifetime; `0` disables caching.

## Timelines and video

Record keypresses sub-frame accurately, then replay them deterministically:

```bash
./scripts/run_gwemu.sh --record scripts/my_test.tl     # local only, needs the SDL window
./scripts/run_gwemu.sh --timeline scripts/my_test.tl
./scripts/run_gwemu.sh --docker --timeline scripts/my_test.tl --video out.mp4
```

This is how a bug that takes twelve button presses to reach becomes reproducible.

### The `.tl` file format is UNDOCUMENTED — and that is a real gap

Only the *usage* above has ever been written down, never the file contents. An agent
trying to automate a boot test concluded it could not author one and fell back to
manual driving, because `--record` needs an interactive SDL window and no example
`.tl` exists anywhere in the tree.

What is known from the binary, without having reverse-engineered the grammar:

- It is a **plain-text, line-oriented** format. The recorder writes a
  `# Automatically recorded timeline` header, so `#` is a comment.
- The parser reports errors as `gnw-timeline: <file>:<line>: parse error: "<text>"`,
  so it is line-by-line with a recognisable token per line.
- It arms **two kinds of event**: `gnw-timeline: <file> armed (%d time + %d frame events)`
  — i.e. events can be scheduled either at a timestamp or at a frame number.
- Actions include at least **`screenshot`** and **`quit`** (`gnw-timeline: screenshot %s`,
  `gnw-timeline: quit`), alongside button events.
- `GNW_TIMELINE` selects a file for playback, `GNW_TIMELINE_RECORD` for capture — the
  wrapper sets both (`scripts/run_gwemu.sh`).

Semantically it is simple — press a button at a timestamp, quit at a timestamp — but
**the exact syntax has not been confirmed and is deliberately not guessed here.** The
reliable way to obtain it is to record one interactively once (`--record`, needs a
local SDL window) and check the result in as a worked example. **Do that; it removes a
standing obstacle to automated testing.**

Until then, QMP is the working alternative for scripted input:

```bash
./scripts/run_gwemu.sh --qmp 4477      # then drive via QMP send-key / screendump
```

`send-key` needs `hold-time: 600`; 200 ms is too short for the launcher to register a
press. The gwemu key map is `gnw-input.ini`, holding QKeyCode indices — A=`x`, B=`z`,
Game=`g`, Time=`t`, Pause=`esc`, Power=`p`, Start=`ret`, Select=`shift_r`. It lives
in gwemu's per-platform settings directory:

| | path |
|---|---|
| Linux | `~/.local/share/gwemu/gwemu/gnw-input.ini` |
| macOS | `~/Library/Application Support/gwemu/gwemu/gnw-input.ini` |

gwemu prints the directory it settled on at startup (`gwemu_settings_get_base_path:`),
so check the log rather than guessing if it is not where you expect.

> `-display gwemu` fails on X11 MIT-SHM in some environments (notably headless or
> containerised sessions). `-display none` works, and QMP `screendump` and `send-key`
> both still function — so a screenshot-driven test does not need a visible window.

## When the emulator and hardware disagree

They are different implementations and can legitimately differ — particularly around
behaviour the ARM architecture leaves `UNPREDICTABLE`. A fault seen only under emulation is
not automatically a firmware bug.

Before drawing conclusions:

1. **Confirm it is the same firmware.** Compare `build/gwemu_build_info.txt` against the
   md5 gnwmanager reported. Cheap, and rules out the most common explanation.
2. **Check whether the difference is configuration.** `SD_CARD=0` and `SD_CARD=1` differ in
   where the filesystem lives, which paths run at boot, and what memory gets touched.
3. **Get a hardware datapoint before concluding anything about hardware.**
   `gnwmanager monitor` reads the same log buffer this harness forwards, and
   `gnwmanager gdbserver` gives you breakpoints on the device — use `hbreak` for flash
   addresses there. Note that killing the gdbserver leaves the core halted, which looks
   exactly like a frozen device; `gnwmanager start bank1` resumes it.

A worked example — a MemManage that reproduced deterministically under gwemu and never on
silicon, traced to an `UNPREDICTABLE` MPU register write — is preserved in the gwemu
project's `backup/memfault/`, with both images and a script reproducing each outcome.
