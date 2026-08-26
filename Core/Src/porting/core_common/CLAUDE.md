# core_common — standalone "core" binary SDK

Guidance for porting a classic emulator ("core") to the dynamic, out-of-tree
model instead of linking it into the main firmware ELF. Loaded by Cursor via
`.cursor/rules/core_common.mdc` when editing files under `porting/core_common/`
or `cores/`. See also `docs/PICO8_EXTERNAL_MODULE.md` — this SDK generalizes
the exact trampoline/redefine-syms/entry-point pattern PICO-8 already used,
so read that doc first if something here is under-explained.

## Model

Every classic core (Watara Supervision, and any future migration — gb/gbc,
nes, sms/gg/sg/col, msx, pce, md, a2600, a7800, amstrad, tama, pkmini,
gw, videopac, ...) used to be linked straight into `gw_retro_go.elf` via a
`.overlay_<system>` section with a compile-time dispatch table
(`emu_dispatch_t` / `run_internal_emu`, removed — see
`Core/Src/retro-go/rg_emulators.c` header comment above `emulator_start()`).
That coupling is gone: a core is now

1. built as its own freestanding ELF, linked at the fixed `RAM_EMU` VMA
   (`ld/gnw_ram_emu.ld`, shared with the firmware's own linker script so the
   two can never drift),
2. talking to the firmware **only** through `gw_firmware_abi_t`
   (`Core/Inc/retro-go/gw_firmware_abi.h`) — no direct symbol references in
   either direction,
3. packaged by `tools/pack_core.py` into a `CORE`-header `.bin` carrying its
   own metadata (`gnw_core_meta_t` — system name, ROM dirname/extensions,
   optional cheat file extension, ABI requirement, code/BSS size, semantic
   version `X.Y.Z`, inline pad/header logo images),
4. discovered at boot by `emulators_scan_cores()` scanning `/cores/*.bin`
   (`Core/Src/retro-go/rg_emulators.c`) — no compile-time list of systems on
   the firmware side at all.

The packed `--version` (default `1.0.0`) and `--core-name` (default: output
stem) are shown in the in-game pause menu under **Info** (name, version,
path, file date).

```mermaid
flowchart LR
  src["cores/<name>/*.c + external/<engine>"] -->|objcopy --redefine-syms| obj["renamed .o"]
  bridge["core_common/gw_core_bridge.c"] --> elf
  obj --> elf["<name>_core.elf @ RAM_EMU_START"]
  elf -->|tools/pack_core.py| bin["cores/<name>.bin"]
  bin -->|copied by Makefile.common| sd["sd_content/cores/<name>.bin"]
  sd -->|emulators_scan_cores() at boot| tab["dynamic tab in launcher"]
  tab -->|run_dynamic_core()| run["core executes, calls firmware via ABI"]
```

## Load regions

Packed segments may target **RAM_EMU** (always segment 0), optionally
**ITCM** (hot code), and optionally **RAM_UC** (LUT8 LCD bonus, 150 KiB
at `__RAM_UC_CORE_START__` — firmware switches the LTDC to LUT8 before
loading). AHB/DTCM are not `gnw_core_region_t` values — they are
firmware dynamic pools (`malloc` / `dtc_*`); cores allocate from them at
runtime via the ABI (`ahb_malloc`, `dtc_malloc`, `mem_ctl`). Leftover
RAM_UC after the loaded code+bss is `lcd_get_bonus_pool()`.

## Porting a new core: checklist

1. `cp -r cores/_template cores/<name>` is **not** how it works — the
   template is included, not copied. Instead create `cores/<name>/Makefile`
   modeled on `cores/wsv/Makefile`: set `CORE_NAME`, `CORE_ENTRY` (must be a
   real function symbol in your sources, the trampoline in
   `gw_core_entry.S` branches to it), `CORE_C_SOURCES` (repo-root-relative;
   every basename must be unique — objects land in one flat `build/` dir),
   then `include ../_template/Makefile`.
2. Write/adapt `Core/Src/porting/<system>/main_<system>.c`:
   - `#include "gw_core_bridge.h"` **after** the normal firmware headers
     (`common.h`, `rom_manager.h`, `gw_malloc.h`, ...) — see the comment at
     the top of `gw_core_bridge.h` for why the include order matters
     (macro-substitution of later *uses*, not the `extern` declarations).
   - For per-core option labels/values, use `gw_i18n()` tables
     (`gw_core_i18n.h`) + ABI `i18n_lang_code()` — do **not** reach into
     firmware `curr_lang` / `lang_t`. Keep tables in a dedicated
     `<system>_i18n.c` (see MSX: `msx_i18n.c`). English row required;
     other languages optional.
   - Replace any `odroid_settings_<Something>_set/get` wrapper with the
     generic `odroid_settings_app_int32_get/set("Name", ...)` already in the
     ABI.
   - Seed `ram_start` (if the core uses `ram_malloc`) to `&__CORE_BSS_END__`
     (linker symbol, see below), not a firmware-side overlay symbol.
3. `cd cores/<name> && make` — fix undefined-reference errors by adding
   entries to **both**:
   - `Core/Src/porting/core_common/gw_core_bridge_redefine_syms.txt`
     (`OLD_NAME NEW_NAME`, **no blank lines** — objcopy's parser rejects
     them with "missing new symbol name"), and
   - a matching `core_<name>` trampoline in `gw_core_bridge.c`.

   If the missing symbol is already in `gw_firmware_abi_t`, this is a five
   minute addition. If it isn't yet, extend the ABI first (Phase 1 below).
4. Run `make pack` (or plain `make`, which depends on it) — produces
   `cores/<name>.bin`. Sanity-check the packed file matches
   `gnw_core_probe()`'s expectations with the one-off Python snippet in the
   commit history of this SDK (parse `CORE` + `gnw_core_meta_t`, assert
   `payload_offset == header_length + 8` and `file_size - payload_offset ==
   code_size`) if you change the packer or the struct layout.
5. Wire into the top-level build (`Makefile.common`): add a
   `cores_<name>` phony target + `$(CORES_DIR)/<name>.bin` rule (copy from
   `cores/<name>.bin`) mirroring the `wsv` entries, list it as a
   `$(SD_CONTENT_STAMP)` prerequisite, add one `sdpush` line in `flash_sd`,
   and a `$(MAKE) -C cores/<name> clean` line in the top-level `clean`.

Footer logos: put dark-on-light PNG/BMP under `cores/<name>/assets/` and
pass `--pad-logo` / `--header-logo` (or `pad_logo=` / `header_logo=` inside
`--system`). `pack_core.py` converts them the same way as
`tools/png_to_logo.py` (Pillow required). Prefer this over `--pad-logo-c`
extracts from `rg_logos.c` (core logos no longer live there).

Cheat files: set `cheat_ext=` on `--system` (or `--cheat-ext` for the
legacy single-system sugar) to the on-disk suffix under `/cheats/` —
`ggcodes`, `pceplus`, or `mcf`. Leave empty if the core has no cheat
support; the launcher then skips probing entirely.

## Per-core settings (`.cfg`)

Emulator display/region options are **not** stored in the global `/CONFIG`
blob (that only holds launcher-wide settings). On launch the firmware binds:

| Binary | Settings file |
|--------|----------------|
| `/cores/<stem>.bin` | `/data/<stem>.cfg` |
| Multi-tab core (same `.bin`) | one shared `/data/<stem>.cfg` |
| `/homebrews/<stem>.bin` | `/data/homebrew/<stem>.cfg` |

Format: magic `RGCF`, version 1, `app_config_t` (palette/scaling/filter/…
plus up to 16 arbitrary `app_int32` user keys ≤11 chars), crc32. Cores keep
using `odroid_settings_*` / `app_int32_*` — the bind in `emulator_start()`
routes them into the active `.cfg`.

Homebrew payloads and Zelda3/SMW assets live under **`/homebrews/`** (not
`/roms/homebrew/`). Covers remain `/covers/homebrew/<stem>.img`. Project
build trees still use `roms/homebrew/` for restool US ROM inputs.

## Extending the ABI (Phase 1-equivalent)

Follow the checklist already documented in `docs/PICO8_EXTERNAL_MODULE.md`
("Maintenance Checklist"): append-only, never reorder/resize/remove a field.

1. `Core/Inc/retro-go/gw_firmware_abi.h` — add the function pointer at the
   **end** of `gw_firmware_abi_t` (inside `reserved[]`'s shrinking space if
   present, otherwise just append before it).
2. `Core/Src/retro-go/gw_firmware_abi.c` — initialize the new field in
   `g_firmware_abi`.
3. `Core/Src/porting/core_common/gw_core_bridge_redefine_syms.txt` — add
   `real_name core_real_name`.
4. `Core/Src/porting/core_common/gw_core_bridge.c` — add the
   `core_real_name` trampoline forwarding through `gw_firmware_abi()`.

Bump `GW_FIRMWARE_ABI_VERSION` only for an incompatible layout change once
an ABI is *released* (never for a pure append) — see the comment above that
define. While external cores are still in active development (packaged
cores are gitignored; nothing is distributed as a prebuilt blob yet),
fields may be removed/reordered without bumping — same policy as the
`odroid_system_emu_init` cheat_update_cb signature change. Within a
released version, `gnw_core_probe()` / `gwhb_header_t` checks compare
`required_abi_min_size <= sizeof(g_firmware_abi)`, not equality. RTC read
slots (`GW_GetCurrent*`, `GW_GetUnixTM`, `mktime`) were dropped in favor of
`time()`+`localtime()`.

## Shared globals: macros, not snapshots

Unlike PICO-8's bridge (`p8_firmware_bridge.cpp`), which snapshots ABI
pointers once at init because its overlay's BSS is guaranteed to land at a
fixed address across builds, `core_common` exposes `common_emu_state`,
`ACTIVE_FILE`, and `ram_start` as **macros** in `gw_core_bridge.h` that
dereference the ABI pointer on every access:

```c
#define common_emu_state (*(common_emu_state_t *)(gw_firmware_abi()->common_emu_state_ptr))
#define ACTIVE_FILE       (*(retro_emulator_file_t **)(gw_firmware_abi()->ACTIVE_FILE_ptr))
#define ram_start         (*(gw_firmware_abi()->ram_start_ptr))
```

This is a few extra cycles per access but stays correct regardless of a
given core's own BSS layout — safer for a multi-core SDK where each core's
`.o` set (and therefore its BSS addresses) differs. If a core needs another
firmware global read/written this way, add it here rather than inventing a
per-core snapshot mechanism.

## Gotchas hit while porting Watara Supervision (read before re-deriving these)

- **`objcopy --redefine-syms` fails on blank lines** in the mapping file
  ("missing new symbol name") — only `#` comments and `NAME NEW_NAME`
  lines are tolerated.
- **`__aeabi_memset`/`__aeabi_memclr` take `(dest, n, c)`** — `n` and `c`
  **swapped** versus libc `memset(dest, c, n)`. The compiler emits these
  EABI helpers instead of plain `memset` for struct/array init in some
  cases; give them their own trampolines (already done in
  `gw_core_bridge.c`), don't alias them to `core_memset`.
- **`isalnum`/`feof`/etc. are function-like macros in newlib** — writing
  `gw_firmware_abi()->isalnum(c)` as a trampoline body needs `#undef
  isalnum` first (and friends: `isalpha`, `isspace`, `isupper`, `islower`,
  `isxdigit`, `tolower`, `toupper`, `ferror`), otherwise the macro expands
  at the call site into nonsense.
- **`gw_firmware_abi.h` pulls in `ff.h`** (FatFs) transitively — a core's
  Makefile needs `-I.../Core/Src/porting/lib/FatFs` even if the core itself
  never touches FatFs directly.
- **`--gc-sections` silently drops unreferenced `const` globals even with
  `__attribute__((used))`** — that attribute only stops the *compiler* from
  optimizing it away; the *linker*'s section garbage collection needs an
  explicit `KEEP()`. This bit `GW_CORE_BUILT_ABI_VERSION`/`_SIZE` (baked-in
  markers `tools/pack_core.py` reads post-link, nothing in the core itself
  references them) — fixed by giving them their own linker section
  (`.gw_core_bridge_probe`) and `KEEP(*(.gw_core_bridge_probe))` in
  `core_ram_emu.ld`. Any future "value nobody calls, only the packaging
  tool reads via `nm`" needs the same treatment.
- **Watch for premature `*/` inside a `/* ... */` comment** when the prose
  mentions pointer dereference or glob-like text (e.g. `s_wsv_palette_*` or
  `pad_logo_*/header_logo_*`) — the C preprocessor closes the comment at
  the first `*/` regardless of intent, producing confusing downstream
  syntax errors far from the real typo.
- **RAM_EMU address aliasing applies to dynamic cores too** (see the
  top-level `CLAUDE.md`, "Overlay RAM addresses alias"): every core links
  at the same `__RAM_EMU_START__` VMA, so a stopped-at address in
  `0x24xxxxxx` resolves against *whichever* core's `.elf` gdb/addr2line
  happens to have loaded, not necessarily the one that's actually running.
  Point `addr2line` at `cores/<name>/build/<name>_core.elf` explicitly.

## Debugging a dynamic core on hardware

Same techniques as the top-level `CLAUDE.md`'s "Debugging crashes on
hardware" section (BSOD `ABFSR`, `gnwmanager gdbserver` + `hbreak`, ...)
apply unchanged — `run_dynamic_core()` jumps into `RAM_EMU` exactly like
the old compile-time dispatch did, so a fault inside a dynamic core looks
identical to a fault inside a legacy overlay core from the CPU's point of
view. The only difference is which `.elf` has the matching symbols
(`cores/<name>/build/<name>_core.elf`, not `build/gw_retro_go.elf`).
