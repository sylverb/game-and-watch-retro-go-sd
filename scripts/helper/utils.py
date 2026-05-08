#!/usr/bin/env python3
"""Shared utilities: console, prompts, file helpers, subprocess wrappers."""

from __future__ import annotations

import hashlib
import shlex
import subprocess
import sys
from pathlib import Path

from rich.console import Console

console = Console()

# ROM file extensions recognized by the build system
ROM_EXTENSIONS: frozenset[str] = frozenset({
    ".gb", ".gbc",
    ".nes", ".fds", ".nsf",
    ".ggcodes", ".gw",
    ".sms", ".gg", ".sg",
    ".md", ".gen",
    ".bin",
    ".col",
    ".pce", ".pceplus",
    ".rom",
    ".dsk",
    ".mx1", ".mx2", ".mcf",
    ".a28", ".a78",
    ".png", ".jpg", ".bmp",
})


def calculate_rom_size(roms_dir: Path) -> int:
    """Return total bytes of all ROM files under *roms_dir* that the build system will pick up."""
    if not roms_dir.exists():
        return 0
    return sum(
        f.stat().st_size
        for f in roms_dir.rglob("*")
        if f.is_file() and f.suffix.lower() in ROM_EXTENSIONS
    )


def abort(message: str) -> None:
    console.print(f"[bold red]{message}[/bold red]")
    sys.exit(1)


def prompt_text(question: str, default: str = "") -> str:
    suffix = f" (default: {default})" if default else ""
    return input(f"{question}{suffix}: ").strip() or default


def prompt_bool(question: str, default: bool = True) -> bool:
    default_str = "y" if default else "n"
    while True:
        raw = input(f"{question} [y/n] (default: {default_str}): ").strip().lower() or default_str
        if raw in ("y", "yes", "1", "true"):  return True
        if raw in ("n", "no",  "0", "false"): return False
        console.print("[yellow]Please enter y or n.[/yellow]")


def verify_sha256(path: Path, expected_hash: str) -> None:
    console.print(f"  Verifying {path.name}… ", end="")
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual == expected_hash:
        console.print("[green]OK[/green]")
    else:
        abort(f"Hash mismatch!\n  Expected: {expected_hash}\n  Got:      {actual}")


def find_backup_file(base: Path, filename: str, expected_size: int) -> Path | None:
    """Search base and its immediate subdirectories for a file matching name and size."""
    try:
        search_dirs = [base] + [d for d in base.iterdir() if d.is_dir()]
    except (PermissionError, FileNotFoundError):
        return None
    for directory in search_dirs:
        candidate = directory / filename
        try:
            if candidate.stat().st_size == expected_size:
                return candidate
        except FileNotFoundError:
            pass
    return None


def run_command(cmd: list[str], dry_run: bool) -> None:
    if dry_run:
        console.print(f"[yellow][DRY-RUN] {shlex.join(cmd)}[/yellow]")
        return
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
