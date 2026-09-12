# Firmware distribution

How a Retro-Go SD firmware release is published, and what an installer has to
do with it.

This is the contract. [RELEASE_2_0.md](RELEASE_2_0.md) is the argument behind
it — why fields exist, and which ones were deliberately left out. Read this one
to implement an installer; read that one to understand a decision.

Cores and homebrew are **not** part of a firmware release. They are separate
projects publishing under the
[GWRG distribution spec](https://github.com/slash-proc/gwrg-dist-spec), bound
to this firmware only by the firmware ABI. A firmware release carries the
intflash image and the static content the launcher itself needs: fonts,
language blobs, and the boot logo.

That is why this format is a sibling of the GWRG spec rather than a `kind`
inside it. The spec describes installing files into a directory. Firmware is
flashed to an address in internal flash and *defines* the filesystem everything
else lands in — a different verb, and `artifacts[]`/`uses[]`/`requiresAbi` have
nothing to say about it.

Real values throughout are from the release in
[`examples/`](examples/), which is the output of a genuine four-build packer
run. Nothing here is invented.

## The pieces

| File | What it is |
|---|---|
| `dist/versions.json` | The index. One fetch, every version |
| `dist/<tag>/manifest.json` | One release: its builds, firmware identity, install paths |
| `dist/<tag>/projects.json` | The curated core/homebrew list |
| `dist/<tag>/*.zip` | Eight per release: an install bundle and a debug bundle per build |

## Discovery

Releases are the archival record; **GitHub Pages is the mirror a browser can
actually read.** A release asset redirects to
`release-assets.githubusercontent.com`, which sends no CORS header — `curl`
gets the bytes, a page does not. The same problem, and the same answer, as
[gwrg-dist-spec spec/01](https://github.com/slash-proc/gwrg-dist-spec/blob/main/spec/01-distribution.md).
There is no CORS proxy in this design.

```
https://slash-proc.github.io/game-and-watch-retro-go-sd/dist/versions.json
https://slash-proc.github.io/game-and-watch-retro-go-sd/dist/v2.0.0/manifest.json
https://slash-proc.github.io/game-and-watch-retro-go-sd/dist/v2.0.0/projects.json
https://slash-proc.github.io/game-and-watch-retro-go-sd/dist/v2.0.0/retro-go-sd-v2.0.0-sd-bank2.zip
```

`dist/versions.json` is the only URL a tool hard-codes. Everything else is
reached by resolving a relative URL against the file that named it:

1. `GET dist/versions.json`.
2. Pick a version. The newest is `versions[0]`; there is no `latest`.
3. `GET` that entry's `manifest`, resolved against the `versions.json` URL.
4. Resolve every `url` in the manifest against the manifest's URL.

Each deploy regenerates the whole site from the releases list, keeping the
newest `retained` versions (5).

## `versions.json`

```json
{
  "schemaVersion": 1,
  "project": "retro-go-sd",
  "title": "Retro-Go SD",
  "repo": "slash-proc/game-and-watch-retro-go-sd",
  "releasesUrl": "https://github.com/slash-proc/game-and-watch-retro-go-sd/releases",
  "retained": 5,
  "versions": [
    {
      "tag": "v2.0.0",
      "manifest": "v2.0.0/manifest.json",
      "publishedAt": "2026-09-08T19:33:37Z",
      "prerelease": false,
      "gitTag": "Retro-Go SD v1.4.1-124-g1dfd6f95b+",
      "providesAbi": { "version": 2, "size": 844 },
      "coreMetaVersion": 3
    }
  ]
}
```

| Field | Required | |
|---|---|---|
| `schemaVersion` | yes | Integer. Refuse a version you do not implement |
| `project` | yes | Always `retro-go-sd` |
| `title` | yes | Display name |
| `repo` | yes | `owner/name` |
| `releasesUrl` | yes | Where a user finds versions not listed here |
| `retained` | yes | How many versions this file keeps |
| `versions` | yes | Newest first |

Version entries:

| Field | Required | |
|---|---|---|
| `tag` | yes | Release tag |
| `manifest` | yes | URL, relative to this file |
| `publishedAt` | yes | RFC 3339 |
| `prerelease` | yes | Boolean |
| `gitTag` | yes | The firmware's baked version string |
| `providesAbi` | yes | `{version, size}` — the ABI this firmware offers |
| `coreMetaVersion` | yes | Core container format this firmware can load |

The last three are duplicated from the manifest, and only those three. A field
earns a place here when it lets a picker **filter or warn before fetching a
manifest** — which is exactly what `providesAbi` and `coreMetaVersion` do:
together they say whether a user's installed cores will still load after
upgrading to a version whose manifest has not been downloaded. They must agree
with the manifest; the manifest wins.

`paths`, `languages` and `capabilities` are deliberately *not* here. They
matter at install time, by which point the manifest has been fetched anyway,
and duplicating them across five retained entries is drift with no reader.

## `manifest.json`

### Top level

| Field | Required | |
|---|---|---|
| `schemaVersion` | yes | Integer |
| `project` | yes | Matches `versions.json` |
| `title` | yes | Display name |
| `docs` | no | Absolute `https://` URL for a human. Never fetched by an installer |
| `source` | yes | `repo`, `commit`, `ref` — what built this |
| `firmware` | yes | Identity and compatibility. See below |
| `paths` | yes | Where things are installed on the device |
| `languages` | yes | Every UI language this release offers |
| `builds` | yes | One per storage × bank. Four today |
| `builtAt` | yes | RFC 3339, when the release was packed |
| `projects` | no | The curated list, published beside this manifest |

### `firmware`

Everything here is read out of the built binary, not copied from a header. The
point is to describe what actually shipped.

```json
"firmware": {
  "gitTag": "Retro-Go SD v1.4.1-124-g1dfd6f95b+",
  "providesAbi": { "version": 2, "size": 844 },
  "abiOffset": "0x400",
  "coreMetaVersion": 3,
  "superblock": { "magic": "GWLB", "version": 2, "structSize": 36 },
  "installFile": { "path": "data/INSTALL", "magic": "RGIN", "version": 1 }
}
```

| Field | |
|---|---|
| `gitTag` | The version string baked into the image, verbatim |
| `providesAbi` | `{version, size}` from the ABI struct |
| `abiOffset` | Where that struct sits in the image, as a hex string |
| `coreMetaVersion` | `GNW_CORE_META_VERSION` — the core container format |
| `superblock` | `{magic, version, structSize}` of the layout superblock |
| `installFile` | Where the device writes its install marker, and what identifies it |

`coreMetaVersion` is the one value that is *not* recoverable from the binary —
it exists only as a compile-time comparison — so it is read from
`Core/Inc/retro-go/gnw_core_meta.h` at pack time.

### `paths`

```json
"paths": {
  "cores": "/cores",
  "homebrew": "/homebrews",
  "bios": "/bios",
  "roms": "/roms",
  "covers": "/covers",
  "cheats": "/cheats",
  "data": "/data"
}
```

The firmware declares where things live, so installers stop hardcoding it. A
rename is then a one-value change here rather than a change in every tool.

Keys are role names; values are absolute paths from the storage root. Ignore a
role you do not use, and treat the object as open — a later firmware may name a
role this one does not.

### `languages`

Every UI language the release offers, as a sorted array:

```json
["de_de","en_us","es_es","fr_fr","it_it","ja_jp","ko_kr","no_nb","pt_pt","ru_ru","zh_cn","zh_tw"]
```

`en_us` is baked into the firmware's rodata and has **no blob**, so it appears
here but never in a build's `content[]`. Every other entry has one blob per
build.

### `builds`

```json
{
  "id": "sd-bank2",
  "storage": "sd",
  "bank": 2,
  "capabilities": ["coverflow", "cheatCodes", "screenshot", "sharedHibernateSavestate"],
  "littlefsBlockSize": 4096,
  "buildFlags": "SD_CARD=1 … INTFLASH_BANK=2",

  "bundle": { "bytes": 490467, "sha256": "af09decc…", "url": "retro-go-sd-v2.0.0-sd-bank2.zip" },
  "debug":  { "bytes": 1509147, "sha256": "b9390080…", "url": "retro-go-sd-v2.0.0-sd-bank2-debug.zip" },

  "image":    { "bytes": 239364, "sha256": "95bc847a…", "path": "gw_retro_go_intflash.bin" },
  "sdUpdate": { "bytes": 239364, "sha256": "95bc847a…", "path": "gw_retro_go_intflash.bin",
                "filename": "update_bank2.bin" },

  "content": [
    { "path": "lang/fr_fr.bin", "install": "lang/fr_fr.bin", "language": "fr_fr",
      "bytes": 1947, "sha256": "…" }
  ]
}
```

| Field | Required | |
|---|---|---|
| `id` | yes | `<storage>-bank<n>`. Stable, and how a tool names a build |
| `storage` | yes | `sd` or `flash` |
| `bank` | yes | `1` or `2` — which internal-flash bank this is linked for |
| `capabilities` | yes | Features compiled in. For UI only |
| `littlefsBlockSize` | yes | Compile-time; the host must match it when building a filesystem image |
| `buildFlags` | yes | The literal make command line. Provenance — do not parse it |
| `bundle` | yes | The install zip |
| `debug` | yes | The ELF zip. Never installed |
| `image` | yes | The intflash image, inside `bundle` |
| `sdUpdate` | SD only | The image under the name the on-device updater looks for |
| `content` | yes | Everything else that lands on the device |

`bank` is the intflash link address — `1` is `0x08000000`, `2` is `0x08100000`.
It is not a runtime patch: a bank-2 image cannot be flashed to bank 1. Bank 2 is
the dual-boot layout, which keeps the original firmware in bank 1.

`capabilities` is derived from the build flags rather than hand-written, so it
cannot drift. Nothing about installation depends on it — it is what a UI shows.
`sharedHibernateSavestate` is the one worth surfacing to a user, because it
changes savestate semantics and so is a save-compatibility concern across an
upgrade.

There is deliberately no extflash or filesystem geometry here. The installer
detects the chip and patches the superblock, so a build's baked values describe
nothing about the release. See [Installing](#installing).

### `path` vs `install` vs `url`

Three different things, and mixing them up is the easiest mistake to make:

| Key | Means |
|---|---|
| `url` | A standalone file published **beside the manifest**. Resolve it against the manifest URL and fetch it |
| `path` | An entry **inside that build's `bundle` zip**. Read it out of the archive |
| `install` | Where the file goes **on the device**, relative to the storage root |

Only `bundle`, `debug` and the top-level `projects` use `url`. Everything else
lives inside a bundle and uses `path`. Only `content[]` has `install`.

`install` is currently always equal to `path`, but they are separate fields
because they answer separate questions, and an installer must use `install` to
decide where a file lands.

### `sdUpdate`, and the one load-bearing filename

`sdUpdate` appears only on SD builds. A flash install has no card to write it
to, and the schema rejects it there.

`filename` is the exact name the on-device updater looks for —
`update_bank1.bin` or `update_bank2.bin`, matched literally in
`external/firmware_update/Core/Src/firmware_update.c:20,24`. This is the only
place in the format where a filename is load-bearing rather than cosmetic;
everywhere else the device finds files by directory and extension.

`sdUpdate.path` may point at the **same zip entry as `image`**, and normally
does: the build copies the intflash image to `update_bank<n>.bin`, so the two
are the same bytes and the zip stores them once. When `path` matches
`image.path`, extract that entry and write it under `sdUpdate.filename`. If the
two ever diverge the zip carries both and `path` differs; handle it by reading
`path`, not by assuming either case.

## Choosing a build

An installer picks one of the four. The two axes:

- **`storage`** — `sd` for a device with an SD card mod, `flash` for one
  without. These are genuinely different builds: different linker script,
  different filesystem, not a runtime option.
- **`bank`** — `2` is the dual-boot layout, which leaves the original firmware
  in bank 1 and is the normal choice. `1` replaces the stock firmware.

Refuse rather than guess when:

- The device's detected storage does not match any published build.
- The user asks for `bank: 1` on a device that already has the dual-boot
  bootloader installed. `boot_bank2()` spins forever when bank 2 holds no valid
  reset vector (`external/firmware_update/Core/Src/firmware_update.c:97-114`),
  so a bank-1-only install under that bootloader can leave a device that does
  not boot.
- The manifest's `providesAbi` disagrees with what you read out of the image
  itself. That means the release is misdescribed; see
  [Compatibility](#compatibility).

## Installing

In order:

1. **Fetch and verify.** Check `bundle.sha256` before opening the archive, then
   every `image`, `sdUpdate` and `content[]` entry's `sha256` after extracting.
   Hashes are lowercase hex, over the raw file bytes.

2. **Patch the layout superblock** in the intflash image with host-resolved
   geometry, then flash it. The build ships linker defaults with `crc32 = 0`, so
   an unpatched image falls back to those defaults and never bricks. See
   [The layout superblock](#the-layout-superblock).

3. **Write `content[]`** to the directories `paths` declares, using each entry's
   `install` path. A user installing a subset of languages writes only the
   `lang/` entries whose `language` they want, plus everything without a
   `language` key.

4. **Write `/data/INSTALL`** if you can. See
   [The install marker](#the-install-marker).

You do **not** need to delete anything. `saves/flashcachedata.bin` is stale
after a reflash — it is keyed to the device UID and the flash write base — but
the *firmware* clears it, on the next boot, when the install marker disagrees
with the running build. That is deliberate: a browser without the File System
Access API (Firefox) cannot delete a file on the card, and a device flashed by
`gnwmanager` or `make flash` was never touched by an installer at all. Neither
case can be made to work by asking installers to tidy up.

### Language blobs are per-build

`lang/*.bin` is a table of string offsets in `lang_t` field order, and `lang_t`
gains fields under `CHEAT_CODES=1` and three more under `INTFLASH_BANK=2`. A
blob built for one configuration therefore has a different field *order* than
another, and `rg_i18n.c` truncates a mismatched blob rather than rejecting it —
the failure mode is silently wrong menu text, not an error.

Measured, in this release: `fr_fr` is 1880 bytes on bank 1 and 1947 on bank 2.
Fonts and `bios/logo.bin` are byte-identical across all four builds.

**Only ever install a build's own `content[]`.** Do not share language blobs
between builds, and do not carry them across an upgrade that changes bank.

### The layout superblock

`GnwLayoutSuperblock` (`Core/Inc/retro-go/gw_layout_superblock.h`) makes one
binary serve any external-flash size. It is compiled into **all four builds**.
Locate it by its magic; the linker places it wherever the section lands.

| Offset | Size | Field | |
|---|---|---|---|
| `0x00` | 4 | `magic` | `0x424C5747`, LE bytes `47 57 4C 42` — `"GWLB"` |
| `0x04` | 2 | `version` | `2` |
| `0x06` | 2 | `struct_size` | `36` |
| `0x08` | 4 | `frogfs_offset` | FrogFS base is `0x90000000 + frogfs_offset` |
| `0x0C` | 4 | `frogfs_length` | Packed image size; `0` = unknown |
| `0x10` | 4 | `extflash_size` | Total external flash bytes |
| `0x14` | 4 | `reserved_offset` | Bytes reserved at the bottom |
| `0x18` | 4 | `littlefs_length` | LittleFS partition bytes |
| `0x1C` | 4 | `flags` | Bit *n* marks field *n* authoritative |
| `0x20` | 4 | `crc32` | Over the first `0x20` bytes |

`flags` exists so a real `0` is distinguishable from "unset, use the runtime
default": bit 0 `frogfs_offset`, bit 1 `extflash_size`, bit 2
`reserved_offset`, bit 3 `littlefs_length`. Set the bit for every field you
write, then recompute `crc32` over bytes `0x00`–`0x1F`.

Assert `version` and `structSize` against the manifest's
`firmware.superblock` before patching. A future `GNW_LAYOUT_VERSION` bump then
fails loudly instead of silently writing the wrong field layout.

The magic also appears in the instruction stream — the code that validates the
superblock loads it as an immediate — so a search finds more than one candidate.
Filter on `version` and `struct_size` being plausible, and treat an ambiguous
result as an error rather than picking the first hit.

### The install marker

`/data/INSTALL` is how a tool that can only see the storage learns what firmware
is on the device: the ABI struct and the version string live in internal flash,
which is not on the card.

The **firmware writes it**, at boot, whenever it disagrees with the running
build. So it is correct however the device was flashed. An installer may write
it to save one boot cycle, but never needs to, and must treat a missing or
mismatched marker as "unknown", not as an error.

76 bytes, little-endian, packed — verified by `_Static_assert` against the real
header, not read off the struct by eye:

| Offset | Size | Field | |
|---|---|---|---|
| `0` | 4 | `magic` | `0x4E494752`, LE bytes `52 47 49 4E` — `"RGIN"` |
| `4` | 1 | `version` | `1` |
| `5` | 1 | `bank` | `1` or `2` |
| `6` | 1 | `storage` | `0` = flash, `1` = sd |
| `7` | 1 | `reserved` | Zero |
| `8` | 4 | `abi_version` | Matches `providesAbi.version` |
| `12` | 4 | `abi_size` | Matches `providesAbi.size` |
| `16` | 2 | `core_meta_version` | Matches `coreMetaVersion` |
| `18` | 2 | `reserved2` | Zero |
| `20` | 48 | `git_tag` | NUL-padded. Matches `firmware.gitTag` byte for byte |
| `68` | 4 | `installed_at` | Unix seconds; `0` when the RTC is not set |
| `72` | 4 | `crc32` | Over all 76 bytes with this field zeroed |

`crc32` is **standard CRC-32** — reflected, polynomial `0xEDB88320`, init
`0xFFFFFFFF`, final XOR `0xFFFFFFFF`. Identical to zlib's `crc32`, so any
off-the-shelf implementation matches; verified against
`Core/Src/porting/crc32.c` (`crc32_le(0, "The quick brown fox", 19)` =
`0xb74574de`, the same as `zlib.crc32`). Compute it over the whole 76-byte
struct with bytes `72`–`75` set to zero, then store the result there.

Reject a marker whose `magic`, `version` or `crc32` does not check out. Do not
compare `installed_at` when deciding whether a marker matches a build — it
records *when* the marker was written, not *what*.

## Compatibility

A core declares `required_abi_version` and `required_abi_min_size`. The firmware
publishes `providesAbi`. A core loads when:

```
required_abi_version == providesAbi.version && required_abi_min_size <= providesAbi.size
```

`size` is `sizeof(gw_firmware_abi_t)` in bytes. The struct is append-only within
a version, so "needs at least N bytes" and "needs at least the first N/4 table
entries" say the same thing. It moves between releases — 844 in this one — which
is why nothing should hardcode it.

**The manifest is advisory. Read the real values out of the image.** They are
two little-endian `uint32` — `version` then `size` — at `firmware.abiOffset`
(`0x400`) from the start of the intflash image, or from the bank base when
scanning a device.

This is not defensive boilerplate. A misplaced `.firmware_abi` section shipped
in both flash builds during development: a preprocessor block landed between the
`__attribute__((section(".firmware_abi")))` and the struct it applied to, so the
attribute attached to the wrong symbol and `g_firmware_abi` ended up in `.text`.
It compiled, it linked, and it produced plausible ~243 KB images. Nothing in the
build complained. It was caught only because the packer reads those bytes instead
of trusting the header — and a tool that trusted the manifest would have read
garbage at `0x400` on a real device.

### `gitTag`

Provenance and version comparison. **Nothing gates on it.**

It is the string baked into the image (`"Retro-Go SD v2.0.0"`), stored verbatim
in `/data/INSTALL` and published verbatim in the manifest, so the two
byte-compare with no normalisation on either side. Use it to tell a user what is
installed, or to notice that a card was written by a different release.

Do not use it as a compatibility check. The core git-tag check that once existed
is gone — `CORE_HEADER_MAGIC_INTERNAL` is defined and unused in
`Core/Src/retro-go/rg_emulators.c` — and compatibility rests entirely on
`providesAbi` versus a core's `required_abi_*`.

## `projects.json`

The curated list of cores and homebrew known to work with this firmware,
generated in CI from [CURATED_PROJECTS.md](CURATED_PROJECTS.md) and published
beside the manifest.

```json
{
  "schemaVersion": 1,
  "projects": [
    {
      "project": "nes-fceu",
      "title": "FCEUmm",
      "kind": "core",
      "versionsUrl": "https://slash-proc.github.io/fceumm-retro-go-sd/dist/versions.json"
    }
  ]
}
```

| Field | |
|---|---|
| `project` | Slug, stable across versions |
| `title` | Display name |
| `kind` | `core` or `homebrew` |
| `versionsUrl` | That project's `versions.json` |

`kind` is `core`/`homebrew`, matching the projects' own `PROJECT_KIND`, not the
GWRG spec's `emulator`/`homebrew`.

Follow `versionsUrl` using the GWRG spec's rules, not this document's: from
there on it is a spec-conformant project and its `versions.json` and
`manifest.json` are the spec's, with `requiresAbi` to check against this
firmware's `providesAbi`.

The list is a convenience, not a gate. A user may install any conforming project
by pasting its repository URL.

## Validating

```
scripts/flasher/validate_release_json.py docs/examples
```

Checks all three files against `schema/firmware-*.schema.json` (JSON Schema
draft 2020-12), plus the cross-field rules a schema cannot express: build ids
unique, `languages[]` agreeing with what the builds ship, no two files
installing to the same path, `sdUpdate` sharing the image's zip entry only when
it is the same bytes, and `versions.json` agreeing with the manifest.

The schemas set `additionalProperties: false` throughout, so a field that was
deliberately dropped cannot quietly return.
