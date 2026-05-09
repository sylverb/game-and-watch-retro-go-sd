#!/usr/bin/env python3
"""Build configuration: dataclass, persistence, interactive setup, and summary display."""

from __future__ import annotations

import argparse
import dataclasses
import os
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Any, get_type_hints

import yaml
from rich.table import Table

from scripts.helper.utils import console, abort, prompt_text, prompt_bool, calculate_rom_size

CHOICES_FILE = Path("choices.yaml")
MiB          = 1024 * 1024
OFFSET_MB    = {"mario": 1, "zelda": 4}
OFFSET_BYTES = {k: v * MiB for k, v in OFFSET_MB.items()}
FS_SIZE_MIN_MB = 4 # set to 4MB because 2MB or less fails to build currently


@dataclass
class BuildConfig:
    # ── Core settings ──────────────────────────────────────────────────────────
    target: str = field(
        default="mario",
        metadata={"group": "core", "prompt": "Target device (mario/zelda)"}
    )
    sd_card: bool = field(
        default=False,
        metadata={"group": "core", "prompt": "SD card installation?"}
    )
    flash_mb: int = field(
        default=0,
        metadata={"group": "core", "prompt": "Flash chip size in MB (defaults to stock)"}
    )
    dual_boot: bool = field(
        default=True,
        metadata={"group": "core", "prompt": "Enable dual-boot (stock + retro-go)?"}
    )
    flash_locally: bool = field(
        default=False,
        metadata={"group": "core", "prompt": "Flash the device on this machine?"}
    )
    backup_dir: str = field(
        default="./",
        metadata={"group": "core", "prompt": "Path to backup directory"}
    )
    fs_size_mb: int = field(
        default=0,
        metadata={"group": "core", "prompt": "LittleFS partition size in MB (automatic)"}
    )

    # ── Makefile options ───────────────────────────────────────────────────────
    flash_bootloader: bool = field(
        default=False,
        metadata={"group": "makefile", "prompt": "Flash the bootloader?"}
    )
    single_font: bool = field(
        default=True,
        metadata={"group": "makefile", "prompt": "Add additional fonts?"}
    )
    ko_kr: bool = field(
        default=False,
        metadata={"group": "makefile", "prompt": "Add Korean fonts?"}
    )
    ja_jp: bool = field(
        default=False,
        metadata={"group": "makefile", "prompt": "Add Japanese fonts?"})
    zh_cn: bool = field(
        default=False,
        metadata={"group": "makefile", "prompt": "Add Chinese (Simplified) fonts?"}
    )
    zh_tw: bool = field(
        default=False,
        metadata={"group": "makefile", "prompt": "Add Chinese (Traditional) fonts?"}
    )
    coverflow: bool = field(
        default=False,
        metadata={"group": "makefile"}
    )
    jpg_quality: int  = field(
        default=85,
        metadata={"group": "makefile"}
    )
    cheat_codes: bool = field(
        default=False,
        metadata={"group": "makefile", "prompt": "Enable cheat codes?"}
    )
    msx_use_bank_2: bool = field(
        default=False,
        metadata={"group": "makefile"}
    )
    enable_screenshot: bool = field(
        default=True,
        metadata={"group": "makefile", "prompt": "Enable screenshots?"}
    )
    shared_hibernate_savestate: bool = field(
        default=True,
        metadata={"group": "makefile"}
    )
    disable_splash_screen: bool = field(
        default=True,
        metadata={"group": "makefile"}
    )
    compress: str  = field(
        default="lzma",
        metadata={"group": "makefile"}
    )
    big_bank: bool = field(
        default=True,
        metadata={"group": "makefile"}
    )

    # ── Derived properties ─────────────────────────────────────────────────────

    def __post_init__(self):
        """Handle logic for non-interactive initialization (CLI/Env/YAML)."""
        if self.fs_size_mb == 0:
            self.fs_size_mb = max(FS_SIZE_MIN_MB, int(self.flash_mb * 0.1))

    @property
    def intflash_bank(self) -> int:
        return 2 if self.dual_boot else 1

    @property
    def extflash_offset(self) -> int:
        return OFFSET_BYTES[self.target] if self.dual_boot else 0

    @property
    def extflash_size_mb(self) -> int:
        return (self.flash_mb - OFFSET_MB[self.target]) if self.dual_boot else self.flash_mb

    @property
    def bank(self) -> str:
        return f"bank{self.intflash_bank}"

    @property
    def offset_bytes(self) -> int:
        return OFFSET_BYTES[self.target]

    @property
    def fs_offset(self) -> int:
        return self.extflash_size_mb * MiB - self.fs_size_mb * MiB

    @property
    def filesystem_flash_offset(self) -> int:
        return self.extflash_offset + self.fs_offset

    # ── Persistence ────────────────────────────────────────────────────────────

    def save(self) -> None:
        with open(CHOICES_FILE, "w") as f:
            yaml.dump(asdict(self), f, default_flow_style=False, sort_keys=False)
        console.print(f"[green]Configuration saved to {CHOICES_FILE}[/green]")

    @classmethod
    def from_yaml(cls) -> BuildConfig | None:
        if not CHOICES_FILE.exists():
            return None
        try:
            with open(CHOICES_FILE) as f:
                data = yaml.safe_load(f)
            if not data:
                return None
            known_fields = {f.name for f in dataclasses.fields(cls)}
            return cls(**{k: v for k, v in data.items() if k in known_fields})
        except (yaml.YAMLError, TypeError) as e:
            console.print(f"[yellow]Could not load {CHOICES_FILE}: {e}[/yellow]")
            return None

    def validate_flash_limit(self, strict: bool = True) -> int:
        """Calculates total footprint and warns or exits if it exceeds flash_mb.

        ROM sizes are estimated post-compression using a conservative ratio for
        8-bit systems (0.65). If strict=True (default), exits on overrun.
        If strict=False, only warns — allowing the caller's confirm prompt to
        serve as acknowledgment. Either way, exits immediately if the overrun
        exceeds what maximum lzma compression could theoretically recover.
        """
        LZMA_ESTIMATE    = 0.65
        LZMA_THEORETICAL = 0.50

        roms_raw     = calculate_rom_size(Path("roms"))
        covers_bytes = sum(f.stat().st_size for f in Path("covers").glob("*.img")) if Path("covers").exists() else 0

        ratio      = LZMA_ESTIMATE if self.compress == "lzma" else 1.0
        roms_bytes = int(roms_raw * ratio)

        stock_offset = self.extflash_offset
        retro_go_pkg = 2 * MiB
        lfs_size     = self.fs_size_mb * MiB
        overhead     = stock_offset + retro_go_pkg + covers_bytes + lfs_size
        total_needed = overhead + (0 if self.sd_card else roms_bytes)
        flash_limit  = self.flash_mb * MiB

        if total_needed > flash_limit:
            impossible = not self.sd_card and (overhead + int(roms_raw * LZMA_THEORETICAL)) > flash_limit

            console.print(f"\n[bold red]{'ERROR' if impossible else 'WARNING'}: Configuration exceeds Flash capacity![/bold red]")

            error_table = Table(box=None, show_header=False)
            error_table.add_row("Stock Firmware Offset:", f"{stock_offset / MiB:>6.2f} MB")
            error_table.add_row("Retro-Go System:",       f"{retro_go_pkg / MiB:>6.2f} MB")
            if not self.sd_card:
                error_table.add_row("ROMs (estimated):",  f"{roms_bytes / MiB:>6.2f} MB  [dim](raw: {roms_raw / MiB:.2f} MB)[/dim]")
                error_table.add_row("Covers:",            f"{covers_bytes / MiB:>6.2f} MB")
                error_table.add_row("LittleFS Partition:",f"{self.fs_size_mb:>6.2f} MB")
            error_table.add_row("-" * 30, "")
            error_table.add_row("[bold]Total Estimated:[/bold]", f"[red]{total_needed / MiB:.2f} MB[/red]")
            error_table.add_row("[bold]Available Flash:[/bold]", f"{self.flash_mb:.2f} MB")
            console.print(error_table)
            console.print(f"[yellow]Overage:[/yellow] [bold]{(total_needed - flash_limit) / MiB:.2f} MB[/bold]")

            if impossible:
                console.print("[red]Overage cannot be recovered by compression — remove some ROMs.[/red]\n")
                raise SystemExit(1)

            if strict:
                console.print("[yellow]ROM sizes are estimated post-compression and may differ.[/yellow]\n")
                raise SystemExit(1)

            console.print("[yellow]ROM sizes are estimated — actual result may fit. Proceed at your own risk.[/yellow]\n")

        return total_needed


def load_config(cli_args: dict[str, Any]) -> BuildConfig:
    """
    Merge configuration from three sources, in ascending order of precedence:
      1. choices.yaml
      2. Environment variables  (e.g. TARGET=zelda)
      3. CLI arguments
    """
    merged: dict[str, Any] = {}

    yaml_config = BuildConfig.from_yaml()
    if yaml_config:
        merged.update(asdict(yaml_config))

    type_hints = get_type_hints(BuildConfig)
    for f in dataclasses.fields(BuildConfig):
        env_value = os.environ.get(f.name.upper())
        if env_value is not None:
            merged[f.name] = _cast_env_value(env_value, type_hints[f.name], f.name.upper())

    known_fields = {f.name for f in dataclasses.fields(BuildConfig)}
    for key, value in cli_args.items():
        if value is not None and key in known_fields:
            merged[key] = value

    return BuildConfig(**merged) if merged else BuildConfig()


def _cast_env_value(raw: str, target_type: type, var_name: str) -> Any:
    if target_type is bool:
        if raw.lower() in ("true", "1", "yes", "y"):  return True
        if raw.lower() in ("false", "0", "no",  "n"): return False
        abort(f"{var_name} must be boolean (true/false/1/0). Got: {raw!r}")
    try:
        return target_type(raw)
    except ValueError:
        abort(f"{var_name} must be {target_type.__name__}. Got: {raw!r}")


def configure_interactively(config: BuildConfig) -> BuildConfig:
    type_hints = get_type_hints(BuildConfig)

    for f in dataclasses.fields(config):
        prompt_str = f.metadata.get("prompt")
        if not prompt_str:
            continue

        # SD card mod means no LittleFS partition is needed
        if f.name == "fs_size_mb" and config.sd_card:
            continue

        current_val = getattr(config, f.name)

        if f.name == "fs_size_mb" and current_val == 0:
            current_val = max(FS_SIZE_MIN_MB, int(config.flash_mb * 0.1))

        field_type = type_hints[f.name]
        if field_type is bool:
            val = prompt_bool(prompt_str, current_val)
        else:
            val = prompt_text(prompt_str, str(current_val))
            if field_type is int:
                try:
                    val = int(val)
                except ValueError:
                    abort(f"Value for {f.name} must be an integer.")

        if f.name == "target":
            val = {"m": "mario", "mario": "mario", "z": "zelda", "zelda": "zelda"}.get(val.lower())
            if not val:
                abort("Target must be mario or zelda.")
            if config.flash_mb == 0:
                config.flash_mb = OFFSET_MB[val]

        setattr(config, f.name, val)

    print()
    show_summary(config=config, make_args=None)
    config.validate_flash_limit()
    return config


def show_summary(config: BuildConfig, make_args: list[str] | None = None) -> None:
    """Print a human-readable table of the current build configuration."""
    size_total   = config.validate_flash_limit(strict=False)
    roms_bytes   = calculate_rom_size(Path("roms"))
    covers_bytes = sum(f.stat().st_size for f in Path("covers").glob("*.img")) if Path("covers").exists() else 0

    table = Table(title="Current Build Configuration", show_header=False, box=None)
    table.add_column("", style="cyan",  justify="right")
    table.add_column("", style="white")

    table.add_row("Target:",       config.target.capitalize())
    table.add_row("Flash:",        f"{config.flash_mb} MB ({'SD card' if config.sd_card else 'Flash only'})")
    table.add_row("Dual-boot:",    "Yes" if config.dual_boot else "No")
    table.add_row("Flash bank:",   config.bank)
    if not config.sd_card:
        table.add_row("LittleFS:", f"{config.fs_size_mb} MB")
    table.add_row("ROMs:",         f"{roms_bytes // MiB} MB  [dim](est. compressed: {int(roms_bytes * (0.65 if config.compress == 'lzma' else 1.0)) // MiB} MB)[/dim]")
    if covers_bytes:
        table.add_row("Covers:",   f"{covers_bytes // MiB} MB")
    table.add_row("Total size:", f"{size_total // MiB} MB")
    table.add_row("Flash method:", "Local (gnwmanager)" if config.flash_locally else "Print commands")
    if make_args:
        table.add_row("Make args:", "")

    console.print(table)
    if make_args:
        print("                "+" ".join(make_args))
    console.print()


def register_args(parser: argparse.ArgumentParser, group: str) -> None:
    """Register BuildConfig fields for a given group as CLI arguments on a parser."""
    type_hints = get_type_hints(BuildConfig)
    for f in dataclasses.fields(BuildConfig):
        if f.metadata.get("group") != group:
            continue
        arg_name    = f"--{f.name.replace('_', '-')}"
        actual_type = type_hints[f.name]
        if actual_type is bool:
            parser.add_argument(arg_name, action=argparse.BooleanOptionalAction, help=f"Set {f.name}.")
        else:
            parser.add_argument(arg_name, type=actual_type, help=f"Set {f.name}.")
