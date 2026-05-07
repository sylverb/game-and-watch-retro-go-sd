#!/usr/bin/env python3
"""Firmware flash orchestration."""

from __future__ import annotations

import shlex
from pathlib import Path

from scripts.helper.config import BuildConfig
from scripts.helper.utils import console, abort, find_backup_file, run_command

# Known-good SHA-256 checksums for stock firmware blobs
INTERNAL_SHA256 = {
    "zelda": "ab37ba03bc33682c091b4e7caffd7d3102b83e675377e06d6d3046b9bf483bb6",
    "mario": "b1f10bde11490dcf922524fcba1592ffbc47d2b9a95cda58358ab8934c747e5b",
}
EXTERNAL_SHA256 = {
    "zelda": "cad7e32b0783250c29b4684fda0c8cd5b1a11e6c000ca8dd5633fd32ebffcaed",
    "mario": "2cb99ef457b6495a99514f296a0ef07316e2b376c7c6c38309d4fff9e176b387",
}


def flash_commands(config: BuildConfig) -> list[list[str]]:
    """Build the gnwmanager command(s) needed to flash the compiled firmware."""
    build_dir = ""
    if config.flash_locally:
        build_dir = 'build/'
    if config.sd_card:
        return [[
            "gnwmanager", "flash", config.bank, f"{build_dir}gw_retro_go_intflash.bin",
            "--", "start", config.bank,
            "--", "disable-debug",
        ]]
    return [[
        "gnwmanager", "flash", config.bank, f"{build_dir}gw_retro_go_intflash.bin",
        "--", "flash", "ext", f"{build_dir}frogfs.bin",   f"--offset={config.extflash_offset}",
        "--", "flash", "ext", f"{build_dir}littlefs.bin", f"--offset={config.filesystem_flash_offset}",
        "--", "start", config.bank,
        "--", "disable-debug",
    ]]


def firmware_backups_exist(backup_dir: Path, target: str, offset_bytes: int) -> bool:
    """Return True if both required stock firmware backup files are present."""
    internal = find_backup_file(backup_dir, f"internal_flash_backup_{target}.bin", 131072)
    external = find_backup_file(backup_dir, f"flash_backup_{target}.bin",          offset_bytes)
    return bool(internal and external)


def format_command(cmd: list[str]) -> str:
    """Format a gnwmanager command with subcommands on new lines with backslashes."""
    if "--" not in cmd:
        return shlex.join(cmd)

    parts = []
    current = []
    for arg in cmd:
        if arg == "--" and current:
            parts.append(shlex.join(current))
            current = ["--"]
        else:
            current.append(arg)
    if current:
        parts.append(shlex.join(current))

    return " \\\n    ".join(parts)


def flash_firmware(config: BuildConfig, dry_run: bool = False, pretty: bool = False) -> None:
    """Flash compiled firmware to the device, or print the commands if not flashing locally."""
    if config.flash_locally:
        backup_dir = Path(config.backup_dir).expanduser()
    else:
        backup_dir = ''
    commands: list[list[str]] = []

    if config.dual_boot:
        if config.flash_locally:
            internal = find_backup_file(backup_dir, f"internal_flash_backup_{config.target}.bin", 131072)
            external = find_backup_file(backup_dir, f"flash_backup_{config.target}.bin", config.offset_bytes)
            if not internal or not external:
                abort("Cannot flash dual-boot: stock firmware backups are missing.")
            if config.flash_bootloader:
                commands.append([
                    "gnwmanager", "flash-patch", config.target,
                    str(internal), str(external), "--bootloader",
                ])
        else:
            if config.flash_bootloader:
                commands.append([
                    "gnwmanager", "flash-patch", config.target,
                    f"internal_flash_backup_{config.target}.bin",
                    f"flash_backup_{config.target}.bin",
                    "--bootloader",
                ])

    commands += flash_commands(config)

    if config.flash_locally:
        for cmd in commands:
            run_command(cmd, dry_run)
    else:
        console.print("\n[bold green]# Run these commands to flash:[/bold green]")
        for cmd in commands:
            if pretty:
                console.print(format_command(cmd))
            else:
                console.print(shlex.join(cmd))


if __name__ == "__main__":
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse
from scripts.helper.config import load_config, show_summary, register_args
from scripts.helper.makefile import make_arguments
from scripts.helper.utils import prompt_bool


def build_parser(parent=None) -> argparse.ArgumentParser:
    kwargs = dict(description="Flash Retro-Go firmware.", help="Flash the firmware.")
    p = parent.add_parser("flash", **kwargs) if parent else argparse.ArgumentParser(**kwargs)
    p.add_argument("-y", "--yes",  action="store_true", help="Skip confirmation prompts.")
    p.add_argument("--dry-run",    action="store_true", help="Print commands without executing.")
    p.add_argument("--pretty",     action="store_true", help="Format printed commands for readability.")
    p.add_argument("--force",      action="store_true", help="Proceed even if ROM size estimate exceeds flash capacity.")
    register_args(p, "core")
    return p


if __name__ == "__main__":
    args      = build_parser().parse_args()
    skip_keys = {"yes", "dry_run", "pretty", "force"}
    config    = load_config({k: v for k, v in vars(args).items() if v is not None and k not in skip_keys})

    if config.dual_boot and not firmware_backups_exist(Path(config.backup_dir), config.target, config.offset_bytes):
        console.print("[yellow]Stock firmware backup missing — disabling dual-boot.[/yellow]")
        config.dual_boot = False

    if not args.yes:
        show_summary(config, make_arguments(config))
        if not prompt_bool("Proceed with flash?"):
            raise SystemExit(0)

    flash_firmware(config, dry_run=args.dry_run, pretty=args.pretty)
