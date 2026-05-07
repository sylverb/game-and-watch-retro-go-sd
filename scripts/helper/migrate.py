#!/usr/bin/env python3
"""Migration helper to port assets from a legacy game-and-watch-retro-go repository."""

from __future__ import annotations

import shutil
from pathlib import Path

from scripts.helper.utils import console, prompt_text


def migrate_assets(
    do_roms:     bool,
    do_covers:   bool,
    do_backups:  bool,
    repo_path:   Path | None,
    yes:        bool = False,
) -> None:
    """
    Copy ROMs, covers, and/or firmware backups from a legacy repository.
    If no asset flags are set, all three are migrated.
    """
    if not any([do_roms, do_covers, do_backups]):
        do_roms = do_covers = do_backups = True

    repo_path = _resolve_repo_path(repo_path, yes)
    if not repo_path:
        console.print("[yellow]Migration skipped: no valid legacy repository found.[/yellow]")
        return

    console.print(f"[cyan]Migrating from {repo_path}...[/cyan]")

    if do_roms:    _migrate_roms(repo_path)
    if do_covers:  _migrate_covers(repo_path)
    if do_backups: _migrate_backups(repo_path)


def _resolve_repo_path(repo_path: Path | None, yes: bool) -> Path | None:
    if repo_path and repo_path.exists():
        return repo_path

    candidates = [d for d in Path("..").iterdir() if d.is_dir() and d.name == "game-and-watch-retro-go"]
    if candidates:
        console.print(f"Found legacy repository at {candidates[0]}")
        return candidates[0]

    if not yes:
        raw = prompt_text("Old repo not found. Enter path or leave blank to skip", "")
        if raw:
            path = Path(raw)
            if path.exists():
                return path

    return None


def _migrate_roms(repo_path: Path) -> None:
    found = False
    source = repo_path / "roms"
    if not source.exists():
        return

    Path("roms").mkdir(exist_ok=True)
    iterdirs = source.iterdir()
    if iterdirs:
        found = True
    for item in iterdirs:
        destination = Path("roms") / item.name
        shutil.copytree(item, destination, dirs_exist_ok=True) if item.is_dir() else shutil.copy2(item, destination)

    # Relocate homebrew ROMs into their own subdirectory
    Path("roms/homebrew").mkdir(exist_ok=True)
    for rom in [Path("roms/smw/smw.sfc"), Path("roms/zelda3/zelda3.sfc")]:
        if rom.exists():
            rom.rename(Path("roms/homebrew") / rom.name)
            shutil.rmtree(rom.parent)
            console.print(f"  ✓ Migrated {rom.name} to roms/homebrew/")

    if found:
        console.print("  ✓ Migrated ROMs")
    else:
        console.print("  ✗ No ROMs found")


def _migrate_covers(repo_path: Path) -> None:
    source = repo_path / "covers"
    if source.exists():
        Path("covers").mkdir(exist_ok=True)
        for cover in source.glob("*.img"):
            shutil.copy2(cover, Path("covers") / cover.name)
        console.print("  ✓ Migrated covers")
    else:
        console.print("  ✗ No covers found")


def _migrate_backups(repo_path: Path) -> None:
    found = False
    backup_files = repo_path.glob("*backup*.bin")
    if backup_files:
        found = True
    for backup in backup_files:
        shutil.copy2(backup, Path("./") / backup.name)
    if found:
        console.print("  ✓ Migrated firmware backups")
    else:
        console.print("  ✓ No firmware backups found")


if __name__ == "__main__":
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse


def build_parser(parent=None) -> argparse.ArgumentParser:
    kwargs = dict(description="Migrate assets from a legacy game-and-watch-retro-go repository.", help="Migrate assets from a legacy repository.")
    p = parent.add_parser("migrate", **kwargs) if parent else argparse.ArgumentParser(**kwargs)
    p.add_argument("--roms",             action="store_true", help="Migrate ROMs.")
    p.add_argument("--covers",           action="store_true", help="Migrate covers.")
    p.add_argument("--firmware-backups", action="store_true", help="Migrate firmware backups.")
    p.add_argument("--path",             type=str,            help="Path to the old repository.")
    return p


if __name__ == "__main__":
    args = build_parser().parse_args()
    migrate_assets(
        do_roms=args.roms,
        do_covers=args.covers,
        do_backups=args.firmware_backups,
        repo_path=Path(args.path) if args.path else None,
        yes=False,
    )
