# EarthBound G&W port — progress log

Companion to [`SNES-PORTING.md`](SNES-PORTING.md) (the plan, authoritative). This file tracks **where we are**, decisions deviating from the plan, and the next concrete step. Update as work lands.

## TL;DR plan

Two phases, per SNES-PORTING.md:

- **Phase 1 — Upstream changes** in `external/earthbound/` (this repo's submodule, on branch `gnw-port`). Five items, mostly mechanical. Should benefit desktop/Pico/SNES ports too (or be no-ops for them).
- **Phase 2 — Firmware integration** in this repo (`game-and-watch-retro-go-sd`). Linker rules, redefines, `app_main_earthbound`, launcher branch, build-system wiring. The actual port boots from here.

Pattern B from the doc (split RAM/extflash with `PatchCodeRodataOffset`) is the load model. 3 MB of INCBIN'd assets ride into `.rodata_earthbound` via the trick from Phase 1.1 below.

## Phase 1 progress (in `external/earthbound`, branch `gnw-port`)

| # | Task | Status | Commit |
|---|---|---|---|
| 1 | `embed.py`: tag `embedded_assets[]` with `EBASSET_TABLE_ATTR` | ✅ done | `2e935492` |
| 2 | `port/gw_retro_go/` skeleton (README + main.c + 5 platform stubs) | ✅ done | `b3508465` |
| 3 | Embedded-cleanliness audit (no stdio, no malloc, no setjmp under `EB_EMBEDDED`) | ✅ done | `f2093992` |
| 4 | `.noreloc` audit + `EB_NORELOC` macro | ✅ done | `717864b5` |
| 5 | (Optional) CMake target for cross-compile sanity | ✅ done | `4822e55a` |

## Phase 2 progress (this repo) — 13 commits, all landed

| # | Commit | Change |
|---|--------|--------|
| 1 | `43594876` | Add `APPID_EARTHBOUND` to `appid_t` |
| 2 | `b1b30f03` | Add `earthbound_redefines` symbol-rename file (`main` → `earthbound_main`) |
| 3 | `ade1a1dc` | Reserve linker sections in both `SDCARD.ld` and `FLASH.ld` (`.overlay_earthbound`, `.overlay_earthbound_bss`, `.rodata_earthbound`) |
| 4 | `1dec3cf4` | Add `main_earthbound.{c,h}` entry-point shell |
| 5 | `11aaf346` | Add G&W `platform_*` implementations — six `gw_*.c` files (video, input, audio, timer, debug, save) |
| 6 | `2b29e25d` | Drop duplicate `platform_render_frame` in `gw_video.c` (upstream provides it) |
| 7 | `794e3e08` | Add `EARTHBOUND_C_SOURCES` + `EARTHBOUND_C_INCLUDES` to root `Makefile` |
| 8 | `da33bcf8` | Add `EARTHBOUND_OBJECTS`, per-object rule, `embedded_assets_array.o` override to `Makefile.common` |
| 9 | `c2d949d5` | Wire `ebtools` asset pipeline (extract → pack-all → embed-registry) via `EARTHBOUND_SFC` env var |
| 10 | `708df207` | Extract `.overlay_earthbound` / `.rodata_earthbound` and push `EarthBound.bin` / `earthbound.ro` onto SD |
| 11 | `ed2dd465` | Wire EarthBound into the launcher's Homebrew branch in `rg_emulators.c` + `gw_linker.h` externs |
| 12 | `26eb9da0` | Bump earthbound submodule pointer to `gnw-port` @ `4822e55a` |
| 13 | `d9a93de4` | Filter `embedded_assets_array.c` from generic per-object foreach (kills duplicate-rule warning) |

## Key decisions made during implementation

These deviate from or refine SNES-PORTING.md. Read both docs together.

- **Build flag is `EB_NORELOC_REQUIRED`** (not the doc's tentative `EBASSET_REQUIRE_NORELOC`). Renamed because the macro isn't asset-specific; it's about any pointer-bearing rodata. If you'd rather match the doc verbatim, rename in `src/core/embedded.h` — only one definition.
- **`EB_NORELOC` macro lives in a new `src/core/embedded.h`**, not in `core/log.h`. log.h stays focused on logging; embedded-build attributes get their own header.
- **Generic embedded-build flag is `EB_EMBEDDED`** — gates the no-op `LOG_WARN`/`LOG_TRACE`, the trap-version of `FATAL`, and the `state_dump.c` stub. Separate from `EB_NORELOC_REQUIRED` because the SNES port needs `EB_EMBEDDED` but not `.noreloc` (no PatchCodeRodataOffset on real SNES hardware).
- **Skeleton port has 5 platform stubs**, not 6 — `gw_save.c` was deliberately omitted, matching `port/snes/`. The G&W save implementation is a trivial `fopen`/`fread`/`fwrite` wrapper with nothing port-specific worth a stub. Add later if Phase 1 task 5 (CMake compliance check) needs it.
- **No upstream `port/gw_retro_go/CMakeLists.txt` yet** — Phase 1 task 5 is optional. Firmware builds from this repo's `Makefile`, not upstream's CMake.
- **Audit numbers vs. the doc:** doc said "72 malloc / 138 printf / 70 stderr / 17 file I/O / 1 setjmp." Reality at task 3 time: 5 real malloc (all in `verify.c`, already `#ifdef ENABLE_VERIFY`), 44 fprintf/stderr (all refactored to `LOG_WARN`), 9 file-I/O (all in `state_dump.c`, now `#ifndef EB_EMBEDDED`), 0 setjmp. The doc was conservative; the work surface was smaller.
- **`verbose_level` bumped 0 → 1** in `src/game_main.c` so the refactored `LOG_WARN` calls still print on desktop without `-v`. Preserves prior behavior of the raw `fprintf(stderr, …)` calls. Trivial revert if not wanted.
- **5 `EB_NORELOC` sites** all turned out to be `const char *X[]` arrays of string literals. The codebase's pointer-bearing structs (`BattleAction`, `EnemyData`, `PsiAbility`) store ROM addresses as `uint32_t`, not C pointers, so they don't need tagging.
- **Phase 2 commits land granularly** — each task is its own commit (user preference for review/bisect); 13 commits total, including two small fixups (`2b29e25d` duplicate `platform_render_frame`, `d9a93de4` duplicate-rule warning).
- **`main_earthbound.c` is a bootstrap-only shell** — unlike `main_zelda3.c`, it does NOT drive its own game loop. Upstream's `earthbound_main()` (renamed from `int main(void)` in `port/gw_retro_go/main.c`) owns the loop and calls `platform_*()` each frame. The retro-go shell just caches `.ro`, runs `PatchCodeRodataOffset`, calls `odroid_system_init`/`emu_init`, then jumps to `earthbound_main()`.
- **Firmware-side platform impls win** — `external/earthbound/port/gw_retro_go/platform/*.c` stubs are excluded from the Makefile source list; our six `Core/Src/porting/earthbound/gw_*.c` files are the actual implementations.
- **`EARTHBOUND_SFC` default is `./earthbound.sfc`** (parent repo root), matching upstream's `port/unix` and submodule Makefile convention rather than the smw-style `roms/<name>/<name>.sfc`.
- **Default `platform_render_frame` from upstream** — `src/platform/platform_render.c` provides a single-core render dispatch; our `gw_video.c` uses it as-is (initial commit had a duplicate that was removed in `2b29e25d`).
- **Savestate/SRAM hooks are stubs** — `eb_LoadState` returns false, `eb_SaveState` returns true, `eb_SramSave` is no-op. First-boot goal is title-screen only.

## Open notes

- **Donor ROM now present at `./earthbound.sfc`** (repo root, added 2026-05-21). Full firmware cross-compile is unblocked.
- **macOS host firmware build requires explicit `GCC_PATH`** — arm-none-eabi-gcc must be installed and passed via `GCC_PATH=/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin/` (or equivalent). Without it the build fails at `CheckTools`.
- **macOS host with `-DEB_NORELOC_REQUIRED` fails** — Mach-O's `section` attribute needs `"segment,section"` syntax. Irrelevant since the G&W variant is only built with arm-none-eabi-gcc (ELF). If someone tries to compile-test on macOS with the flag, they'll need to know this.
- **Editor LSP diagnostics throughout the upstream tree are noise** — clangd can't resolve include paths without `compile_commands.json`. Fine to ignore unless they start showing in a real compile.
- **Upstream `CLAUDE.md` port list now mentions `gw_retro_go/`** — added in Phase 1.5 commit `4822e55a`.
- **Submodule pointer no longer dirty** — committed in `26eb9da0` pointing at `gnw-port` @ `4822e55a`.

## Phase 2 completed (except hardware boot test)

Both Phase 1.5 (upstream CMakeLists.txt toolchain) and all 12 Phase 2 firmware integration steps are done. The only remaining work is **Phase 2.12 (first boot test on hardware)**, which requires:

1. **Donor ROM** at `./earthbound.sfc` (default path; override via `EARTHBOUND_SFC=/path/to/earthbound.sfc`).
2. **Physical Game & Watch** device (Mario or Zelda form factor, autodetected via `get_ofw_is_mario()`).

### Resume command (from repo root, donor ROM in place)

```bash
make -j$(nproc) GNW_TARGET=mario GCC_PATH=/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin CHECK_DIRTY_SUBMODULE=0
make flash_sd GNW_TARGET=mario GCC_PATH=/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin
```

### Diagnostic guide (per SNES-PORTING.md step 13)

- **Crash after `PatchCodeRodataOffset` returns** → missing `.noreloc` annotation on a RAM-resident pointer table.
- **Crash during `game_init()`** → likely a `malloc` we missed; the bump allocator returns NULL, callers don't null-check.
- **Black screen** → scanline routing wrong in `gw_video.c`'s `platform_video_send_scanline`.
- **Link error: undefined reference to `foo`** → add `foo eb__foo` (or similar) to `earthbound_redefines`.
