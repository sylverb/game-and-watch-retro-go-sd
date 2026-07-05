# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

A multi-emulator launcher for the Nintendo Game & Watch (STM32H7B0VB MCU). This is the SD-card variant of [sylverb/game-and-watch-retro-go](https://github.com/sylverb/game-and-watch-retro-go-sd) — emulator cores and ROMs live on a microSD card rather than being baked into the external flash. Each cartridge format (NES, GB, MSX, Genesis, etc.) has its own emulator core ported to STM32 with very tight memory and CPU constraints.

## Build / flash workflow

The build system is plain GNU Make. `Makefile` lists source files; `Makefile.common` contains the rules, toolchain setup, and configuration variables.

**Toolchain.** Requires `arm-none-eabi-gcc` v10+ (CI/Docker uses 15.2.rel1). Either put the toolchain on `PATH` or set `GCC_PATH=/path/to/bin` on the make command line. `gnwmanager` (Python, from `requirements.txt`) is required for any flashing target — install via `python3 -m pip install -r requirements.txt`.

**Common targets** (run from repo root, all support `-j$(nproc)`):

- `make GNW_TARGET=mario` / `make GNW_TARGET=zelda` — build internal+external flash binaries into `build/`. Mario is the default.
- `make flash` — flash internal flash via JTAG (SD-card build: external flash is unused on-device, contents live on SD).
- `make flash_sd` — flash internal + push all emulator core `.bin`s and BIOS files onto the SD card via `gnwmanager sdpush`. Primary developer workflow once hardware is set up.
- `make release` — produce `release/retro-go_update.bin`, the update artifact end users place on their SD card.
- `make release_sdpush` — `make release` + push the update file to the SD card.
- `make docker` — runs the full release build inside the prebuilt `sylverb/retro-go-sd-builder` image (mounts CWD); equivalent to the CI build.
- `make monitor`, `make gdb`, `make dump_screenshot` — runtime debug helpers via gnwmanager.
- `make flash_saves_backup` / `make flash_saves_restore` — pull/push save states between device and `backup/`.
- `make help` — full list of build flags with current values.

There are no automated tests. Verification is manual: build, flash, run on hardware.

**Configuration knobs that change layout (not just behavior)** — pass on the make command line:

- `GNW_TARGET={mario,zelda}` — different button mappings and default extflash size.
- `INTFLASH_BANK={1,2}` (or `INTFLASH_ADDRESS=0x08...`) — selects which 128k/256k internal-flash bank the code is linked into. Bank 2 is used with dual-boot OFW patches.
- `SD_CARD=1` (default for this repo) — enables FatFS, omits ROM compression, and uses `STM32H7B0VBTx_SDCARD.ld`. Setting `SD_CARD=0` switches to the all-in-flash variant (different link script, different feature set).
- `EXTFLASH_SIZE_MB`, `EXTFLASH_OFFSET`, `LARGE_FLASH` — external flash sizing (deprecated in favor of `EXTFLASH_SIZE_MB`).
- `CHEAT_CODES=1`, `COVERFLOW=1`, `SHARED_HIBERNATE_SAVESTATE=1`, `DISABLE_SPLASH_SCREEN=1`, `MSX_USE_BANK_2=1`, `FORCE_NOFRENDO=1` — feature toggles. The Docker release build enables `COVERFLOW=1 SHARED_HIBERNATE_SAVESTATE=1 DISABLE_SPLASH_SCREEN=1 INTFLASH_BANK=2 CHEAT_CODES=1`.
- `CODEPAGE`, `UICODEPAGE`, individual locale flags (`FR_FR`, `RU_RU`, …) — controls font/i18n inclusion.

**Submodule hygiene.** `external/` holds each emulator core as a git submodule. The build refuses to run if submodules are dirty or out of sync — fix with `git submodule update --init --recursive`, or pass `CHECK_DIRTY_SUBMODULE=0` to bypass (the Docker target does this).

## Architecture

### Three storage tiers, one ELF

A single ELF (`build/gw_retro_go.elf`) is partitioned by linker sections into three physical destinations:

1. **Internal flash** (`*_intflash.bin`, from sections `.isr_vector .text .rodata .data .init_array …`) — launcher UI, drivers, FatFS, the always-resident retro-go shell. Linker script: `STM32H7B0VBTx_SDCARD.ld` (or `_FLASH.ld` for non-SD builds).
2. **External flash** (`*_extflash.bin`, sections `._extflash ._itcram_hot ._ram_exec` + every `.overlay_*`) — on SD-card builds this is mostly used during build/test; at runtime cores are streamed from SD instead.
3. **SD card** — `make create_sd_data` extracts each `.overlay_<system>` section into a standalone `cores/<system>.bin` file with `objcopy --only-section`. The launcher loads exactly one core at a time into a fixed RAM region (`._ram_exec`) before running it. Each emulator therefore has the entire "free" RAM (~1 MB) to itself but cannot coexist with another core in memory.

Adding a new emulator means: add its sources to `Makefile`, give it a `.overlay_<name>` section in the linker script, and add a `--only-section=.overlay_<name>` extraction line to `create_sd_data` plus an `sdpush` line in `flash_sd`.

### Source tree layout

- `Core/Src/main.c`, `Core/Src/gw_*.c` — STM32 HAL bring-up, LCD, audio (SAI), buttons, SD driver, RTC, battery (BQ24072), flash chip access, low-level memory allocator. Headers in `Core/Inc/`.
- `Core/Src/porting/<system>/main_<system>.c` — the per-emulator porting layer. This is where most emulator-specific Game & Watch work happens: input mapping, video scaling, audio bridging, savestate hooks, ROM loading, options menus.
- `Core/Src/porting/lib/` — shared helpers used by porting code: FatFs vendor copy, LZ4/LZMA decompressors, HW JPEG decoder, HW SHA1, softspi.
- `Core/Src/porting/odroid_*.c` — the retro-go shell's portability glue (input, display, audio, overlay, sdcard, system). Names come from the original Odroid-GO Retro-Go.
- `retro-go-stm32/` — **git submodule** (same don't-edit-in-place rules as `external/`) holding upstream retro-go: launcher UI, settings, common emulator-side helpers in `components/odroid/`, plus three emulator cores `gnuboy-go`, `nofrendo-go`, `pce-go`, `smsplusgx-go`. Note: most of its `.c` files are NOT compiled — the compiled `odroid_*` implementations live in `Core/Src/porting/`; only the submodule's headers are used. New prototypes for those implementations go on the firmware side (e.g. `Core/Inc/porting/common.h`), not in the submodule's headers.
- `external/` — git submodules for every other emulator core (`fceumm-go`, `blueMSX-go`, `caprice32-go`, `gwenesis`, `LCD-Game-Emulator`, `stella2014-go`, `prosystem-go`, `PokeMini-go`, `potator`, `tamalib`, `tgbdual-go`, `ccleste-go`, `zelda3`, `smw`, `o2em-go`, `firmware_update`). Each is a third-party emulator/port with its own license; we patch them via `genpatch.py`-managed `.patch` files where present.
- `tools/` — Python utilities. The user-facing ones (per README):
  - `gencovers.py` — generate `.img` cover thumbnails for ROMs (uses `requirements.txt`).
  - `fonttool/`, `png_to_logo.py`, `img2bin.py`, `pllgen.py`, `gen_fceu_palettes_table.py` — asset converters used by the build.
- `scripts/` — shell helpers invoked from the Makefile (size reporting, git tag stamping, release packaging, rom discovery). Run from the repo root.
- `assets/`, `icons/`, `smw_redefines`, `zelda3_redefines` — graphics, system icons, SNES symbol-rename headers for the homebrew SNES ports.

### Adding/modifying a core

Most emulator changes happen in `Core/Src/porting/<system>/`. The submodule under `external/<system>` should rarely be touched directly; when it must be, do it as a patch file applied during build, not by editing the submodule in-place (which trips `CheckDirtySubmodules`).

The `retro-go-stm32/components/odroid/` API (`odroid_system`, `odroid_overlay`, `odroid_display`, `odroid_input`, `odroid_audio`, `odroid_sdcard`, `odroid_netplay`) is the contract between the launcher and an emulator core. New cores implement against it.

## Things that are easy to get wrong

- **Don't edit files under submodules in `external/`.** Either fix upstream or add/update a patch. The build's submodule-dirty check will reject the build.
- **`SD_CARD=1` and `SD_CARD=0` are very different builds.** Different linker scripts, different feature set, different binary layout. Default in this repo is `1`; the upstream non-SD repo defaults to `0`.
- **`INTFLASH_BANK` must match how the bootloader was installed.** Dual-boot installs (`gnwmanager flash-patch ... --bootloader`) place retro-go in bank 2 (`INTFLASH_ADDRESS=0x08100000`); standalone installs use bank 1. The release build assumes bank 2.
- **Each emulator must fit in the per-core RAM budget (~1 MB)**, not just compile. Memory regressions only show up at runtime on hardware.
- **`make help` is authoritative** for build flag names, defaults, and current values — prefer it over reading the Makefile.
- **Don't pass an over-aligned struct-member pointer straight into `memmove`/`memcpy`.** `arm-none-eabi-gcc 15.2` mis-marshalled the arguments of a `memmove` whose dest/src came directly from an `ABI_PTR_ALIGN` (`aligned(8)`) member inside a hot/cold–split (`.part.0`) function — it shifted the args one register over so a *pointer* landed in the size slot, producing a multi-hundred-MB copy that ran off into peripheral space and took an imprecise bus fault (EarthBound `scroll_window_up`). Materialize the pointer/size into plain locals (`uint16_t *dst = w->content_tilemap; ... memmove(dst, src, nbytes);`) and, if you suspect a codegen bug, **verify the arg registers in the disassembly**.

## Debugging crashes on hardware (BSOD / faults)

- **Faults self-label.** `main()` sets `SCB->SHCSR` BUSFAULTENA/USGFAULTENA/MEMFAULTENA, so the BSOD title reads "Busfault" / "Usagefault" / "Memfault" instead of a generic "Hardfault". The BSOD also prints `CFSR/HFSR/BFAR/MMFAR/ABFSR`.
- **Imprecise BusFault (`CFSR` bit10 IMPRECISERR, `CFSR=0x…400`, BFAR invalid) = a buffered store to a no-slave address; the reported PC is drain-time noise, not the culprit.** Read **`ABFSR` (`0xE000EFA8`, on the BSOD)** — it names the bus interface the wild access used: bit2 **AHBP** = peripheral space `0x40000000–0x5FFFFFFF`, bit3 **AXIM** = all RAM/flash, bit0/1 = ITCM/DTCM. This instantly tells you RAM-corruption vs a wild pointer into peripherals.
- **GDB over an ST-Link (or pico-probe).** `gnwmanager gdbserver` spawns OpenOCD (auto-detects the probe via `interface/*.cfg`) with a gdbserver on `:3333`; then `make gdb` (or `arm-none-eabi-gdb build/gw_retro_go.elf -ex 'target extended-remote :3333'`). Use **`hbreak`, not `break`, for flash addresses** (`0x08xxxxxx`) — a software breakpoint silently fails to write flash. RAM/overlay addresses (`0x24xxxxxx`) take either. This gdb build has **no Python**; use native `-ex printf`/`x`. To catch a fault with full context, `hbreak common_fault_handler_c` and read `*frame` (the stacked r0–r3/lr/PC) plus live r4–r11.
- **Overlay RAM addresses alias.** Every core's overlay links at the same RAM_EMU VMA, so `gdb`/`addr2line` resolve a `0x24xxxxxx` address to *whichever* overlay's symbol it finds first (often zelda3/SMW, not the running core). Resolve EB addresses via `build/gw_retro_go.map` filtered to `build/earthbound/*.o`, or disassemble the specific `build/<core>/<file>.o`.
