#!/usr/bin/env python3
"""Firmware build orchestration."""

from __future__ import annotations

from scripts.helper.config import BuildConfig
from scripts.helper.makefile import make_arguments, run_make_with_progress
from scripts.helper.utils import run_command


def cleanup(dry_run: bool = False) -> None:
    """Run make clean"""
    make_cmd = "make clean".split(' ')
    run_command(make_cmd, dry_run)

def build_firmware(config: BuildConfig, dry_run: bool = False) -> None:
    """Compile the firmware, including filesystem images when not using an SD card."""
    make_cmd       = ["make"] + make_arguments(config)
    extra_targets  = [] if config.sd_card else ["frogfs_image", "littlefs_image"]
    run_make_with_progress(make_cmd + extra_targets, "Compiling firmware…", dry_run)


if __name__ == "__main__":
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import argparse
from scripts.helper.config import load_config, show_summary, register_args
from scripts.helper.makefile import make_arguments
from scripts.helper.utils import prompt_bool


def build_parser(parent=None) -> argparse.ArgumentParser:
    kwargs = dict(description="Build Retro-Go firmware.", help="Build the firmware.")
    p = parent.add_parser("build", **kwargs) if parent else argparse.ArgumentParser(**kwargs)
    p.add_argument("-y", "--yes",      action="store_true", help="Skip confirmation prompts.")
    p.add_argument("--no-clean",       action="store_true", help="Do not trigger make clean.")
    p.add_argument("--dry-run",        action="store_true", help="Print commands without executing.")
    register_args(p, "core")
    return p


if __name__ == "__main__":
    args      = build_parser().parse_args()
    skip_keys = {"yes", "dry_run", "no_clean"}
    config    = load_config({k: v for k, v in vars(args).items() if v is not None and k not in skip_keys})

    if not args.yes:
        show_summary(config, make_arguments(config))
        if not prompt_bool("Proceed with build?"):
            raise SystemExit(0)

    if not args.no_clean:
        cleanup(dry_run=args.dry_run)
    build_firmware(config, dry_run=args.dry_run)
