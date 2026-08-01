#!/usr/bin/env python3
"""
Select which files under sd_content/cores/ should be packed from ROM layout.

Dirnames are lowercased subfolders of roms/ (see Core/Src/retro-go/rg_emulators.c emulators_init).
"""
from __future__ import annotations

import pathlib

# UI logos: built to sd_content/bios/logo.bin (FrogFS /bios when SD_CARD=0, SD path /bios when SD_CARD=1).
# pico8_stub.bin is omitted (placeholder; use real pico8.bin on FrogFS/SD or skip).
ALWAYS_PACK_REL = frozenset()

# Never copy these into LittleFS /cores (still produced under sd_content/cores for SD workflows).
LITTLEFS_EXCLUDE_CORE_RELPATHS = frozenset({"pico8_stub.bin"})

# roms/<dirname>/ → core blob(s) under sd_content/cores/
_SYSTEM_CORE_RELFILES: dict[str, frozenset[str]] = {
    "gb": frozenset({"tgb.bin"}),
    "gbc": frozenset({"tgb.bin"}),
    "nes": frozenset(),  # nes_fceu.bin + mappers/ (fceumm)
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
    "tama": frozenset({"tama.bin"}),
    "mini": frozenset({"pkmini.bin"}),
    "videopac": frozenset({"videopac.bin"}),
    "homebrew": frozenset(),
    "pico8": frozenset(),
}


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
) -> bool:
    """Whether rel_posix (relative to cores/, posix) should be copied to the image.

    NES mappers now ship as a single ``mappers/mappers.pak`` (+ ines_correct.bin);
    ROM-based pruning happens when that pack is (re)built, not here. The
    ``nes_mapper_allowlist`` argument is accepted for backward compatibility and
    ignored.
    """
    del nes_mapper_allowlist  # single-file pack: pruning handled at pack build time
    if rel_posix in LITTLEFS_EXCLUDE_CORE_RELPATHS:
        return False
    if rel_posix in ALWAYS_PACK_REL:
        return True

    if "pico8" in active_systems and rel_posix == "pico8.bin":
        return True

    if "nes" in active_systems:
        if rel_posix == "nes_fceu.bin":
            return True
        if rel_posix.startswith("mappers/"):
            return rel_posix in ("mappers/mappers.pak", "mappers/ines_correct.bin")

    for dirname, relfiles in _SYSTEM_CORE_RELFILES.items():
        if dirname in active_systems:
            if rel_posix in relfiles:
                return True

    return False
