#!/usr/bin/env python3
"""Makefile argument generation and build progress tracking."""

from __future__ import annotations

import os
import re
import shlex
import subprocess

from rich.progress import Progress, SpinnerColumn, TextColumn, BarColumn, TimeElapsedColumn

from scripts.helper.config import BuildConfig, MiB
from scripts.helper.utils import console, abort

_DRY_RUN_STEP_RE = re.compile(r"echo\s+\[\s*([^\]]+?)\s*\]\s+(.+)")
_BUILD_OUTPUT_RE = re.compile(r"^\[\s*([^\]]+?)\s*\]\s+(.+)")


def make_arguments(config: BuildConfig) -> list[str]:
    """Translate a BuildConfig into a flat list of Make variable assignments."""
    args = [
        f"-j{os.cpu_count() or 1}",
        "CHECK_DIRTY_SUBMODULE=0",
        f"GNW_TARGET={config.target}",
        f"INTFLASH_BANK={config.intflash_bank}",
        f"EXTFLASH_OFFSET={config.extflash_offset}",
        f"EXTFLASH_SIZE_MB={config.extflash_size_mb}",
        f"SD_CARD={1 if config.sd_card else 0}",
        f"SHARED_HIBERNATE_SAVESTATE={1 if config.shared_hibernate_savestate else 0}",
        f"DISABLE_SPLASH_SCREEN={1 if config.disable_splash_screen else 0}",
        f"ENABLE_SCREENSHOT={1 if config.enable_screenshot else 0}",
    ]

    if not config.sd_card and config.fs_size_mb:
        args += [
            f"FILESYSTEM_SIZE={config.fs_size_mb * MiB}",
            f"FILESYSTEM_OFFSET={config.fs_offset}",
        ]

    if config.coverflow:      args += ["COVERFLOW=1", f"JPG_QUALITY={config.jpg_quality}"]
    if config.single_font:    args += ["SINGLE_FONT=1"]
    if config.cheat_codes:    args += ["CHEAT_CODES=1"]
    if config.msx_use_bank_2: args += ["MSX_USE_BANK_2=1"]
    if config.ko_kr:          args += ["KO_KR=1"]
    if config.ja_jp:          args += ["JA_JP=1"]
    if config.zh_cn:          args += ["ZH_CN=1"]
    if config.zh_tw:          args += ["ZH_TW=1"]
    if config.big_bank:       args += ["BIG_BANK=1"]
    if config.compress:       args += [f"COMPRESS={config.compress}"]

    if not config.sd_card:
        args += ["frogfs_image", "littlefs_image"]

    return args


def count_make_steps(make_cmd: list[str]) -> int:
    """Dry-run make to count bracketed output lines, used to size the progress bar."""
    dry_cmd = ["make", "-n", "--no-print-directory"] + make_cmd[1:]
    try:
        result = subprocess.run(dry_cmd, capture_output=True, text=True)
        count = sum(1 for line in result.stdout.splitlines() if _DRY_RUN_STEP_RE.search(line))
        return max(count, 1)
    except OSError:
        return 1


def run_make_with_progress(cmd: list[str], description: str, dry_run: bool) -> None:
    """Run a make command and display a live progress bar driven by bracketed output lines."""
    if dry_run:
        console.print(f"[yellow][DRY-RUN] {shlex.join(cmd)}[/yellow]")
        return

    total = count_make_steps(cmd)

    with Progress(
        SpinnerColumn(),
        TextColumn("[progress.description]{task.description}"),
        BarColumn(),
        TextColumn("[progress.percentage]{task.percentage:>3.0f}%"),
        TimeElapsedColumn(),
        console=console,
        transient=True,
    ) as progress:
        task = progress.add_task(f"[cyan]{description:<60}", total=total)

        process = subprocess.Popen(
            cmd + ["--no-print-directory"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            env={**os.environ, "PYTHONUNBUFFERED": "1", "PYTHONUTF8": "1"},
        )

        for line in process.stdout:
            match = _BUILD_OUTPUT_RE.match(line.strip())
            if match:
                label = f"[{match.group(1).strip()}] {match.group(2).strip()}"
                progress.update(task, advance=1, description=f"[cyan]{label[:60].ljust(60)}")

        process.wait()

    if process.returncode != 0:
        abort(f"Build failed (exit code {process.returncode})")


if __name__ == "__main__":
    import argparse
    from scripts.config import load_config, register_args

    parser = argparse.ArgumentParser(description="Show Make arguments for a given configuration.")
    register_args(parser, "core")
    register_args(parser, "makefile")
    args = parser.parse_args()

    config = load_config({k: v for k, v in vars(args).items() if v is not None})
    print(" ".join(make_arguments(config)))


