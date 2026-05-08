#!/usr/bin/env python3
"""Makefile argument generation and build progress tracking."""

from __future__ import annotations

import os
import re
import shlex
import subprocess
from pathlib import Path

from rich.progress import Progress, SpinnerColumn, TextColumn, BarColumn, TimeElapsedColumn

from scripts.helper.config import BuildConfig, MiB
from scripts.helper.utils import console, abort

SUPPORT_DISCORD  = "https://discord.gg/vVcwrrHTNJ"
BUILD_ERROR_LOG  = Path("build-error.log")
# Lines shown in the terminal on build failure (tail of diagnostic output)
ERROR_TAIL_LINES = 40

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

    _BOOL_FLAGS: list[tuple[bool, str]] = [
        (config.coverflow,      "COVERFLOW"),
        (config.single_font,    "SINGLE_FONT"),
        (config.cheat_codes,    "CHEAT_CODES"),
        (config.msx_use_bank_2, "MSX_USE_BANK_2"),
        (config.ko_kr,          "KO_KR"),
        (config.ja_jp,          "JA_JP"),
        (config.zh_cn,          "ZH_CN"),
        (config.zh_tw,          "ZH_TW"),
        (config.big_bank,       "BIG_BANK"),
    ]
    args += [f"{var}=1" for active, var in _BOOL_FLAGS if active]

    if config.coverflow: args += [f"JPG_QUALITY={config.jpg_quality}"]
    if config.compress:  args += [f"COMPRESS={config.compress}"]

    if not config.sd_card:
        if config.fs_size_mb:
            args += [
                f"FILESYSTEM_SIZE={config.fs_size_mb * MiB}",
                f"FILESYSTEM_OFFSET={config.fs_offset}",
            ]
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

    total        = count_make_steps(cmd)
    output_lines: list[str] = []

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
            output_lines.append(line)
            match = _BUILD_OUTPUT_RE.match(line.strip())
            if match:
                label = f"[{match.group(1).strip()}] {match.group(2).strip()}"
                progress.update(task, advance=1, description=f"[cyan]{label[:60].ljust(60)}")

        process.wait()

    if process.returncode != 0:
        _handle_build_failure(cmd, output_lines, process.returncode)


def _handle_build_failure(cmd: list[str], output_lines: list[str], returncode: int) -> None:
    """Write the full build log, surface the relevant diagnostics, and point to support."""
    BUILD_ERROR_LOG.write_text("".join(output_lines))

    # Strip the [TAG] step-marker lines — what remains is compiler/linker diagnostics
    diag_lines = [
        l.rstrip() for l in output_lines
        if l.strip() and not _BUILD_OUTPUT_RE.match(l.strip())
    ]

    console.print("\n[bold red]Build failed![/bold red]")

    if diag_lines:
        tail = diag_lines[-ERROR_TAIL_LINES:]
        console.print("[red]" + "\n".join(tail) + "[/red]")

    console.print(
        f"\n[yellow]Full build log → {BUILD_ERROR_LOG.resolve()}[/yellow]\n"
        f"[cyan]Need help? Join us on Discord and upload the log in our #support channel: {SUPPORT_DISCORD}[/cyan]"
    )
    raise SystemExit(1)


if __name__ == "__main__":
    import argparse
    from scripts.helper.config import load_config, register_args

    parser = argparse.ArgumentParser(description="Show Make arguments for a given configuration.")
    register_args(parser, "core")
    register_args(parser, "makefile")
    args = parser.parse_args()

    config = load_config({k: v for k, v in vars(args).items() if v is not None})
    print(" ".join(make_arguments(config)))

