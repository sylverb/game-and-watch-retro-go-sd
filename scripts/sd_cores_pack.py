#!/usr/bin/env python3
"""
Select which files under sd_content/cores/ should be packed from ROM layout.

Dirnames are lowercased subfolders of roms/ (see Core/Src/retro-go/rg_emulators.c emulators_init).
"""
from __future__ import annotations

import pathlib

# UI logos: built to sd_content/bios/logo.bin (FrogFS /bios when SD_CARD=0, SD path /bios when SD_CARD=1).
ALWAYS_PACK_REL = frozenset()

# Never copy these into LittleFS /cores (still produced under sd_content/cores for SD workflows).
LITTLEFS_EXCLUDE_CORE_RELPATHS = frozenset()

# roms/<dirname>/ → core blob(s) under sd_content/cores/
_SYSTEM_CORE_RELFILES: dict[str, frozenset[str]] = {
    "gb": frozenset({"tgb.bin"}),
    "gbc": frozenset({"tgb.bin"}),
    "nes": frozenset(),  # nes_fceu.bin + nes_fceumm_mappers/ (fceumm)
    "gw": frozenset({"gw.bin"}),
    "pce": frozenset({"pce.bin"}),
    "gg": frozenset({"sms.bin"}),
    "sms": frozenset({"sms.bin"}),
    "sg": frozenset({"sms.bin"}),
    "col": frozenset({"sms.bin"}),
    "md": frozenset({"md.bin"}),
    "msx": frozenset({"msx.bin"}),
    "wsv": frozenset({"wsv.bin"}),
    "a2600": frozenset({"a2600.bin", "a2600_defprops.bin"}),
    "lynx": frozenset({"lynx.bin"}),
    "a7800": frozenset({"a7800.bin"}),
    "amstrad": frozenset({"amstrad.bin"}),
    "mini": frozenset({"pkmini.bin"}),
    "gba": frozenset({"gba.bin", "gba.xip"}),
    "homebrew": frozenset(),
    "pico8": frozenset(),
}


# --- CORE-header driven selection -------------------------------------------
#
# The table above is a hand-maintained map from system dirname to the exact core
# filename that serves it. That worked when every core was built in this repo
# with a name this file already knew. It does not survive decoupling: a core is
# now published by its own project under whatever name that project chose
# (fceumm.bin for nes, LCD-Game-Emulator.bin for gw), and a name missing from
# the table is dropped from the LittleFS image silently -- the launcher then
# shows no tab and nothing explains why.
#
# A packed core already declares which systems it provides, in its CORE header
# (gnw_core_meta_t, see Core/Inc/retro-go/gnw_core_meta.h). Read that instead of
# guessing from the filename. The table stays as a fallback for files that are
# not CORE containers and predate this (mapper packs, defprops blobs).
CORE_MAGIC = b"CORE"
_GNW_CORE_MAX_SEGMENTS = 4
_SYSTEM_STRUCT_SIZE = 32 + 16 + 32 + 4 + 4 + 4 + 4 + 4 + 8 + 8


def core_declared_dirnames(path: pathlib.Path) -> frozenset[str] | None:
    """Dirnames a packed core declares, or None if not a CORE container."""
    import struct

    try:
        data = path.read_bytes()
    except OSError:
        return None
    if len(data) < 8 + 24 or data[:4] != CORE_MAGIC:
        return None
    try:
        segments_off = 8 + 16
        systems_count_off = segments_off + _GNW_CORE_MAX_SEGMENTS * 12
        (systems_count,) = struct.unpack_from("<I", data, systems_count_off)
        if not 1 <= systems_count <= 4:
            return None
        base = systems_count_off + 4
        out: set[str] = set()
        for i in range(systems_count):
            off = base + i * _SYSTEM_STRUCT_SIZE
            dirname = data[off + 32:off + 48].split(b"\0")[0]
            if dirname:
                out.add(dirname.decode("ascii", "replace").lower())
        return frozenset(out) if out else None
    except (struct.error, IndexError):
        return None


def rom_subdir_has_files(rom_subdir: pathlib.Path) -> bool:
    if not rom_subdir.is_dir():
        return False
    for p in rom_subdir.rglob("*"):
        if p.is_file() and p.name != ".DS_Store":
            return True
    return False


def active_system_dirnames_from_roms_trees(
    project_roms: pathlib.Path | None,
    sd_roms: pathlib.Path | None,
) -> frozenset[str]:
    """Lowercase roms/* folder names that should map to cores (bios/ ignored)."""
    found: set[str] = set()
    for root in (project_roms, sd_roms):
        if root is None or not root.is_dir():
            continue
        for child in root.iterdir():
            if not child.is_dir():
                continue
            name = child.name.lower()
            if name == "bios":
                continue
            if rom_subdir_has_files(child):
                found.add(name)
    return frozenset(found)


def core_relative_path_allowed(
    rel_posix: str,
    active_systems: frozenset[str],
    nes_mapper_allowlist: frozenset[str] | None = None,
    cores_root: pathlib.Path | None = None,
) -> bool:
    """Whether rel_posix (relative to cores/, posix) should be copied to the image.

    NES mappers now ship as a single ``nes_fceumm_mappers/mappers.pak``
    (+ ines_correct.bin); ROM-based pruning happens when that pack is
    (re)built, not here. The ``nes_mapper_allowlist`` argument is accepted
    for backward compatibility and ignored.
    """
    del nes_mapper_allowlist  # single-file pack: pruning handled at pack build time
    if rel_posix in LITTLEFS_EXCLUDE_CORE_RELPATHS:
        return False
    if rel_posix in ALWAYS_PACK_REL:
        return True

    if "pico8" in active_systems and rel_posix == "pico8.bin":
        return True

    # Ask the core itself which systems it serves. A mapped sidecar (gba.xip,
    # pico8.ro) is not a CORE container, so it rides along with the .bin of the
    # same stem -- the two are one install and must not be separated.
    if cores_root is not None:
        here = cores_root / rel_posix
        declared = core_declared_dirnames(here)
        if declared is None and here.suffix not in (".bin",):
            sibling = here.with_suffix(".bin")
            declared = core_declared_dirnames(sibling)
        if declared is not None:
            return bool(declared & active_systems)

    if "nes" in active_systems:
        if rel_posix == "nes_fceu.bin":
            return True
        if rel_posix.startswith("nes_fceumm_mappers/"):
            return rel_posix in (
                "nes_fceumm_mappers/mappers.pak",
                "nes_fceumm_mappers/ines_correct.bin",
            )

    for dirname, relfiles in _SYSTEM_CORE_RELFILES.items():
        if dirname in active_systems:
            if rel_posix in relfiles:
                return True

    return False
