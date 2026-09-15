# 2.0 release & distribution

Design notes for the 2.0 release format consumed by browser installers
(gnw-web-builder). Companion to [CURATED_PROJECTS.md](CURATED_PROJECTS.md).

**Implementing an installer? Read [FIRMWARE_DIST.md](FIRMWARE_DIST.md)** — the
contract: field references, the install algorithm, and the binary layouts. This
file is the argument behind it, and records what was deliberately left out.

Cores and homebrew are decoupled: they are separate projects publishing under
the [GWRG distribution spec](https://github.com/slash-proc/gwrg-dist-spec) and
bound to this firmware only by the firmware ABI. **No cores ship with the
firmware.**

## Settled

- Firmware gets **its own schema**, not a `kind` in the dist spec. The spec
  describes installing files into a directory; firmware is flashed to an
  intflash address and defines the filesystem the rest lands in.
- Conventions are borrowed from the spec: `schemaVersion`, `sha256` on every
  published file, plain-filename `url`s resolved relative to the manifest, a
  `versions.json` index, and a GitHub Pages mirror. No CORS proxy.
- Four builds: storage (`flash` | `sd`) x intflash bank (1 | 2). Each is
  independently sufficient.
- Firmware publishes `providesAbi {version, size}` — the inverse of the spec's
  `requiresAbi`. Advisory: the real check reads the ABI struct at offset
  `0x400` from the bank base.
- `paths{}` in the manifest declares install directories, so installers stop
  hardcoding them and `/homebrews` can be renamed in one place.
- Curated project list is generated in CI from `CURATED_PROJECTS.md` into
  `dist/<tag>/projects.json`. `kind` is `core` or `homebrew`, matching the
  projects' own `PROJECT_KIND`.
- Clean break. No compatibility shim for the old `web-artifacts.zip`.

### Content is not uniformly shared

`fonts/` and `bios/logo.bin` are build-independent. **`lang/*.bin` is not** —
`lang_t` gains 2 fields under `CHEAT_CODES=1` and 3 more under
`INTFLASH_BANK=2`, so the blob's field order is build-dependent, and
`rg_i18n.c` truncates a mismatched blob instead of rejecting it. Language
blobs are therefore per-build.

### Release assets

Eight per release: one install zip and one debug zip per build.

```
retro-go-sd-<tag>-{sd,flash}-bank{1,2}.zip         ~490 KB  image + content
retro-go-sd-<tag>-{sd,flash}-bank{1,2}-debug.zip   ~1.5 MB  ELF
```

Each install zip is self-contained — its intflash image, its `lang/` blobs and a
copy of `fonts/` and `bios/logo.bin` (84 KB duplicated four times, not worth
deduplicating). The image itself *is* deduplicated: `create_sd_data` copies it to
`update_bank<n>.bin`, so the two are the same bytes and the zip stores one entry
that `sdUpdate` points at — a third of the bundle, saved for free.

The ELFs are split out. They are ~1.5 MB now that no core links into the
firmware, but an unstripped ELF was 25.9 MB before decoupling and nothing stops
it growing again; debug symbols nobody downloads should not sit in the install
path or the Pages budget either way.

`manifest.json`, `versions.json` and `projects.json` stay unzipped beside the
bundles so a version picker reads metadata without fetching an archive.

### No geometry in the manifest

`GnwLayoutSuperblock` carries `frogfs_offset`, `extflash_size`,
`reserved_offset` and `littlefs_length`. The installer detects the chip and
patches those in, so the build's baked values are not facts about the release
and are not published. `littlefsBlockSize` is the exception: compile-time
(`LITTLEFS_BLOCK_SIZE`, default 4096), not in the superblock, and the host must
match it when building the filesystem image.

The superblock is compiled into **all four builds** (`Makefile:71` is
unconditional; `cf8f48f6d` extended it to SD-cache relocation). The header's own
docstring still says `SD_CARD=0` only and is stale.

Version 2 (36 bytes) is the first that ships: `5a03c5260` created it at v1
(32 bytes) and `778faf76f` bumped it the same day, both on `web-builder-flash`,
never released.

### No `config{}` block

An earlier draft mirrored the make variables into the manifest. Nearly all of
it was redundant (`bank` / `intflashBank` / `intflashAddr` are one fact) or
unactionable (`compress`, `codepage`, `singleFont`, `msxUseBank2`). Only what
changes installer behaviour is recorded: `storage`, `bank`, `littlefsBlockSize`,
`sharedHibernateSavestate` (save compatibility, carried in `capabilities[]`) and
the other UI capability flags. Provenance is the literal make command line as one
opaque `buildFlags` string that nothing parses.

Likewise dropped: `intflashAddr` (derivable from `bank`), `requiresBootloader`
(implied by `bank: 2`), `label` (rendered from `storage` + `bank`), and
`filename` everywhere except `sdUpdate`, where the on-device updater matches
`update_bank1.bin` / `update_bank2.bin` exactly (`firmware_update.c:20,24`).

## `/data/INSTALL`

A device-written marker so a tool that can only see the SD card knows what is
installed. Same shape as the existing `/data/CONFIG`
(`persistent_config_t`): magic, version, fields, `crc32`.

```c
typedef struct {
    uint32_t magic;             /* 'RGIN' */
    uint8_t  version;           /* 1 */
    uint8_t  bank;              /* 1 | 2 */
    uint8_t  storage;           /* 0 = flash, 1 = sd */
    uint8_t  reserved;
    uint32_t abi_version;
    uint32_t abi_size;
    uint16_t core_meta_version;
    uint16_t reserved2;
    char     git_tag[48];
    uint32_t installed_at;      /* unix */
    uint32_t crc32;
} rg_install_file_t;
```

`git_tag` holds `GIT_TAG` verbatim, display prefix included
(`"Retro-Go SD v2.0.0"`), and the manifest's `firmware.gitTag` is read out of the
same baked string, so the two byte-compare with no normalisation on either side.
It is provenance and version comparison only — nothing gates on it. The core
git-tag check that once existed is gone (`CORE_HEADER_MAGIC_INTERNAL` is defined
and unused in `rg_emulators.c`); compatibility rests entirely on `providesAbi`
versus a core's `required_abi_version` / `required_abi_min_size`.

The firmware compares this against its own build at boot. On mismatch, missing
file, or bad CRC it rewrites the file **and deletes
`saves/flashcachedata.bin`**, which is keyed to the device UID and the flash
write base and is stale after any reflash.

This is why the firmware owns the file rather than the installer: browsers
without the File System Access API (Firefox) cannot delete files on the card,
and a device flashed by gnwmanager or `make flash` would otherwise carry no
marker at all.

Binary rather than JSON so the device needs no parser and a user is not invited
to edit it.

## Parked: `/data/INVENTORY`

Not decided. Recorded so it is not lost.

An installer-owned table of what is installed, so a tool can offer "update"
rather than "install" and explain why a hand-added core is not loading. The
firmware would never write it.

```c
typedef struct {
    char    project[?];    /* project slug */
    char    tag[?];        /* release tag */
    uint8_t kind;          /* 0 = core, 1 = homebrew */
    uint8_t source;        /* repo | bundle | ad-hoc */
    uint8_t sha256[32];    /* of the installed .bin */
} rg_install_entry_t;
```

Wrapped in the same magic/version/crc32 envelope.

Open points:

- **Field widths.** 16 bytes is too small for a project slug; the spec sets no
  maximum. Tag needs room for semver plus a prerelease suffix.
- **Things installed with no upstream project** — source bundles, hand-built
  binaries. Needs a `source` discriminator and sensible behaviour when
  `project`/`tag` are empty.
- Reusing the project manifest's published `artifacts[].sha256` means "which
  repo and version is this" is an exact lookup with no new hashing scheme, and
  doubles as an integrity check on the file.

## Open

- Whether the SD marker should also cover flash-only (`SD_CARD=0`) installs,
  which have no card. The ABI struct plus the baked version string covers most
  of it already.
- Bank-1-only installs on a device that already has the dual-boot bootloader:
  `boot_bank2()` spins forever when bank 2 holds no valid reset vector
  (`firmware_update.c:97-114`). Reached only on the no-update boot path. To
  discuss with Sylver.
- `update_bank2.bin` is written unconditionally regardless of `INTFLASH_BANK`
  (`Makefile.common:1394`), so a bank-1 tree ships a bank-1 image under the
  bank-2 name. The on-device updater does support `update_bank1.bin`
  (`firmware_update.c:336`) — it runs entirely from RAM, so it can rewrite
  either bank.
