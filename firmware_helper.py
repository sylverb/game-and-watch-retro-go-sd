#!/usr/bin/env python3
"""Retro-Go firmware helper — orchestrates config, build, flash, and migration."""

from __future__ import annotations

import argparse
import os
from sys import argv
from pathlib import Path

from scripts.helper.config import (
    BuildConfig, CHOICES_FILE,
    load_config, configure_interactively, show_summary, register_args,
)
from scripts.helper.makefile import make_arguments
from scripts.helper.build   import build_firmware
from scripts.helper.flash   import flash_firmware, firmware_backups_exist
from scripts.helper.migrate import migrate_assets
from scripts.helper.utils   import console, prompt_bool


def main() -> None:
    parser = argparse.ArgumentParser(description="Retro-Go firmware helper.")
    parser.add_argument("--dry-run", action="store_true", help="Print commands without executing.")

    subparsers = parser.add_subparsers(dest="command")

    # config
    config_parser = subparsers.add_parser("config", help="Configure build options.")
    register_args(config_parser, "core")
    config_subparsers = config_parser.add_subparsers(dest="config_cmd")
    register_args(config_subparsers.add_parser("makefile", help="Advanced Makefile settings."), "makefile")

    # migrate
    migrate_parser = subparsers.add_parser("migrate", help="Migrate assets from a legacy repository.")
    migrate_parser.add_argument("--roms",             action="store_true", help="Migrate ROMs.")
    migrate_parser.add_argument("--covers",           action="store_true", help="Migrate covers.")
    migrate_parser.add_argument("--firmware-backups", action="store_true", help="Migrate firmware backups.")
    migrate_parser.add_argument("--path",             type=str,            help="Path to old repository.")

    # build
    build_parser = subparsers.add_parser("build", help="Build the firmware.")
    build_parser.add_argument("-y", "--yes", action="store_true", help="Skip confirmation prompts.")

    # flash
    flash_parser = subparsers.add_parser("flash", help="Flash the firmware.")
    flash_parser.add_argument("-y", "--yes", action="store_true", help="Skip confirmation prompts.")
    # Add the --pretty flag here
    flash_parser.add_argument("--pretty", action="store_true", help="Format printed commands for readability.")

    args    = parser.parse_args()
    dry_run = args.dry_run or os.environ.get("DRY_RUN", "0") == "1"
    # Capture pretty mode from args
    pretty_mode = getattr(args, "pretty", False)

    # Added "pretty" to skip_keys so it doesn't pollute the BuildConfig object unless intended
    skip_keys = {"command", "config_cmd", "dry_run", "yes", "roms", "covers", "firmware_backups", "path", "pretty"}
    cli_args  = {k: v for k, v in vars(args).items() if v is not None and k not in skip_keys}
    config    = load_config(cli_args)

    yes_mode = getattr(args, "yes", False)
    if yes_mode and config.dual_boot:
        if not firmware_backups_exist(Path(config.backup_dir), config.target, config.offset_bytes):
            console.print("[yellow]Stock firmware backup missing in yes mode — disabling dual-boot.[/yellow]")
            config.dual_boot = False

    # ── Subcommand dispatch ────────────────────────────────────────────────────

    if args.command == "config":
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
        # Pass the pretty flag here
        flash_firmware(config, dry_run, pretty=pretty_mode)

    else:
        # ── Default workflow (no subcommand) ──────────────────────────────────
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

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        console.print("\n[yellow]Execution interrupted...[/yellow]")
        # Standard exit code for SIGINT is 128 + 2 = 130
        raise SystemExit(130)

