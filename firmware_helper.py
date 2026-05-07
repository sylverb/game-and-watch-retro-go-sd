#!/usr/bin/env python3
"""Retro-Go firmware helper — orchestrates config, build, flash, and migration."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from scripts.helper.config import (
    BuildConfig, CHOICES_FILE,
    load_config, configure_interactively, show_summary, register_args,
)
from scripts.helper.makefile import make_arguments
from scripts.helper.build   import build_firmware, build_parser   as build_subparser
from scripts.helper.flash   import flash_firmware, firmware_backups_exist, build_parser as flash_subparser
from scripts.helper.migrate import migrate_assets, build_parser   as migrate_subparser
from scripts.helper.utils   import console, prompt_bool


SKIP_KEYS = {"command", "config_cmd", "yes", "roms", "covers",
             "firmware_backups", "path", "pretty", "no_clean"}


def _split_argv(argv: list[str], sep: str = "--") -> list[list[str]]:
    """Split argv on sep into independent command chunks."""
    chunks: list[list[str]] = []
    current: list[str] = []
    for arg in argv:
        if arg == sep:
            if current:
                chunks.append(current)
            current = []
        else:
            current.append(arg)
    if current:
        chunks.append(current)
    return chunks or [[]]


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Retro-Go firmware helper.")
    subparsers = parser.add_subparsers(dest="command")

    config_parser = subparsers.add_parser("config", help="Configure build options.")
    register_args(config_parser, "core")
    config_subparsers = config_parser.add_subparsers(dest="config_cmd")
    register_args(config_subparsers.add_parser("makefile", help="Advanced Makefile settings."), "makefile")

    migrate_subparser(subparsers)
    build_subparser(subparsers)
    flash_subparser(subparsers)
    return parser


def _dispatch(args: argparse.Namespace, config: BuildConfig, dry_run: bool) -> None:
    yes_mode    = getattr(args, "yes", False)
    pretty_mode = getattr(args, "pretty", False)

    if yes_mode and config.dual_boot:
        if not firmware_backups_exist(Path(config.backup_dir), config.target, config.offset_bytes):
            console.print("[yellow]Stock firmware backup missing in yes mode — disabling dual-boot.[/yellow]")
            config.dual_boot = False

    if args.command == "config":
        cli_args = {k: v for k, v in vars(args).items() if v is not None and k not in SKIP_KEYS}
        if not cli_args and not args.config_cmd:
            config = configure_interactively(config)
        config.save()

    elif args.command == "migrate":
        migrate_assets(
            do_roms=args.roms,
            do_covers=args.covers,
            do_backups=args.firmware_backups,
            repo_path=Path(args.path) if args.path else None,
            yes=yes_mode,
        )

    elif args.command == "build":
        if not yes_mode:
            show_summary(config, make_arguments(config))
            if not prompt_bool("Proceed with build?"):
                return
        build_firmware(config, dry_run)

    elif args.command == "flash":
        if not yes_mode:
            show_summary(config, make_arguments(config))
            if not prompt_bool("Proceed with flash?"):
                return
        flash_firmware(config, dry_run, pretty=pretty_mode)


def main() -> None:
    # Extract global flags before splitting on '--'
    global_parser = argparse.ArgumentParser(add_help=False)
    global_parser.add_argument("--dry-run", action="store_true")
    global_args, remaining = global_parser.parse_known_args(sys.argv[1:])
    dry_run = global_args.dry_run or os.environ.get("DRY_RUN", "0") == "1"

    chunks = _split_argv(remaining)

    # Default workflow: no subcommand given
    if chunks == [[]]:
        parser = _build_parser()
        config = load_config({})
        if not CHOICES_FILE.exists():
            console.print("[cyan]First run — starting configuration.[/cyan]")
            config = configure_interactively(config)
            config.save()
            legacy_repo = Path("..").joinpath("game-and-watch-retro-go")
            if prompt_bool("Migrate assets from an old game-and-watch-retro-go repo?", legacy_repo.exists()):
                migrate_assets(do_roms=True, do_covers=True, do_backups=True, repo_path=None, yes=False)
            show_summary(config, make_arguments(config))
        else:
            parser.print_usage()
            print()
            show_summary(config, make_arguments(config))
        return

    parser = _build_parser()
    for chunk in chunks:
        args     = parser.parse_args(chunk)
        cli_args = {k: v for k, v in vars(args).items() if v is not None and k not in SKIP_KEYS}
        config   = load_config(cli_args)
        _dispatch(args, config, dry_run)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        console.print("\n[yellow]Execution interrupted...[/yellow]")
        raise SystemExit(130)
