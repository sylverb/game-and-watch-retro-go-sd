#!/usr/bin/env python3
"""
Pre-compress ROM blobs for FrogFS (SD_CARD=0), matching legacy parse_roms.py --compress lzma.

Produces sidecar names like game.lzma, SMS+/GB bank containers, and MSX/Amstrad .dsk → .cdk
via tools/dsk2lzma.py and tools/amdsk2lzma.py.
"""
from __future__ import annotations

import lzma
import subprocess
import sys
from pathlib import Path
from struct import pack

# Mirrors parse_roms.py
MAX_COMPRESSED_NES_SIZE = 0x00060010
MAX_COMPRESSED_PCE_SIZE = 0x00049000
MAX_COMPRESSED_WSV_SIZE = 0x00080000
MAX_COMPRESSED_SG_COL_SIZE = 60 * 1024
MAX_COMPRESSED_A2600_SIZE = 131072
MAX_COMPRESSED_A7800_SIZE = 131200
MAX_COMPRESSED_MSX_SIZE = 136 * 1024

DONT_COMPRESS = object()


def compress_lzma_raw(data: bytes, level=None) -> bytes:
    if level is DONT_COMPRESS:
        return data
    compressed = lzma.compress(
        data,
        format=lzma.FORMAT_ALONE,
        filters=[
            {
                "id": lzma.FILTER_LZMA1,
                "preset": 6,
                "dict_size": 16 * 1024,
            }
        ],
    )
    return compressed[13:]


def _byteswap_md16(data: bytes) -> bytes:
    if len(data) < 2:
        return data
    b = bytearray(data)
    for i in range(0, len(b) - 1, 2):
        b[i], b[i + 1] = b[i + 1], b[i]
    return bytes(b)


def compress_payload_lzma(mode: str, raw: bytes, *, compress_gb_speed: bool = False) -> bytes | None:
    """Return .lzma file body, or None to keep uncompressed."""
    if mode == "gb":
        bank_size = 16384
        banks = [raw[i : i + bank_size] for i in range(0, len(raw), bank_size)]
        compressed_banks = [compress_lzma_raw(bank) for bank in banks]
        compress_its = [True] * len(banks)
        compress_its[0] = False

        if compress_gb_speed:
            compression_credit = 26
            compress_size = [len(b) for b in compressed_banks[1:]]
            compress_size = [i for i in compress_size if i > 98]
            ordered = sorted(compress_size)
            if compression_credit > len(ordered):
                compression_credit = len(ordered) - 1
            compress_threshold = ordered[int(compression_credit)] if ordered else 0
            for i, bank in enumerate(compressed_banks):
                if len(bank) >= compress_threshold:
                    compress_its[i] = False

        out_banks = []
        for bank, compressed_bank, compress_it in zip(banks, compressed_banks, compress_its):
            if compress_it:
                out_banks.append(compressed_bank)
            else:
                out_banks.append(compress_lzma_raw(bank, level=DONT_COMPRESS))
        return b"".join(out_banks)

    if mode == "sms_md":
        bank_size = 128 * 1024
        banks = [raw[i : i + bank_size] for i in range(0, len(raw), bank_size)]
        compressed_banks = [compress_lzma_raw(bank) for bank in banks]
        parts: list[bytes] = [b"SMS+", pack("<l", len(compressed_banks))]
        for b in compressed_banks:
            parts.append(pack("<l", len(b)))
        parts.extend(compressed_banks)
        return b"".join(parts)

    if mode == "nes":
        if len(raw) > MAX_COMPRESSED_NES_SIZE:
            return None
    elif mode == "pce":
        if len(raw) > MAX_COMPRESSED_PCE_SIZE:
            return None
    elif mode == "msx_rom":
        if len(raw) > MAX_COMPRESSED_MSX_SIZE:
            return None
    elif mode == "wsv":
        if len(raw) > MAX_COMPRESSED_WSV_SIZE:
            return None
    elif mode == "a2600":
        if len(raw) > MAX_COMPRESSED_A2600_SIZE:
            return None
    elif mode == "a7800":
        if len(raw) > MAX_COMPRESSED_A7800_SIZE:
            return None
    elif mode in ("col", "sg"):
        if len(raw) > MAX_COMPRESSED_SG_COL_SIZE:
            return None

    if mode in ("nes", "pce", "msx_rom", "wsv", "a2600", "a7800", "col", "sg"):
        return compress_lzma_raw(raw)

    return None


def _compression_mode_for_path(rel: Path) -> str | None:
    """Relative path under roms/ (e.g. Path('nes/foo.nes'))."""
    if not rel.parts:
        return None
    top = rel.parts[0].lower()
    suf = rel.suffix.lower()
    name_l = rel.name.lower()

    if top == "nes" and suf in (".nes", ".fds", ".nsf"):
        return "nes"
    if top == "pce" and suf == ".pce":
        return "pce"
    if top in ("gb", "gbc") and suf in (".gb", ".gbc"):
        return "gb"
    if top == "sms" and suf == ".sms":
        return "sms_md"
    if top == "gg" and suf == ".gg":
        return "sms_md"
    if top == "md" and suf in (".md", ".gen", ".bin"):
        return "sms_md"
    if top == "col" and suf == ".col":
        return "col"
    if top == "sg" and suf == ".sg":
        return "sg"
    if top == "wsv" and suf in (".bin", ".sv"):
        return "wsv"
    if top == "a2600" and suf in (".a26", ".bin"):
        return "a2600"
    if top == "a7800" and suf in (".a78", ".bin"):
        return "a7800"
    if top == "msx" and suf in (".rom", ".mx1", ".mx2"):
        return "msx_rom"
    # Skip: dsk (cdk conversion), homebrew, pico8, pkmini, gw, etc.
    if name_l.endswith(".lzma") or name_l.endswith(".cdk"):
        return None
    return None


def _dedupe_uncompressed_vs_lzma(roms_root: Path) -> None:
    for p in sorted(roms_root.rglob("*")):
        if not p.is_file():
            continue
        sibling = p.parent / (p.stem + ".lzma")
        if sibling.is_file():
            if not p.name.lower().endswith(".lzma"):
                try:
                    p.unlink()
                except OSError:
                    pass


def _convert_dsk_if_needed(repo: Path, tool: str, path: Path) -> bool:
    """Run tools/dsk2lzma.py or amdsk2lzma.py; return True if .cdk created."""
    script = repo / "tools" / tool
    if not script.is_file():
        print(f"rom_frogfs_lzma: missing {script}", file=sys.stderr)
        return False
    r = subprocess.run(
        [sys.executable, str(script), str(path), "lzma"],
        cwd=str(path.parent),
        capture_output=True,
        text=True,
    )
    if r.returncode != 0:
        print(
            f"rom_frogfs_lzma: {tool} failed for {path} (exit {r.returncode})\n{r.stderr}",
            file=sys.stderr,
        )
        return False
    # Tools may write legacy jeu.dsk.cdk; normalize to jeu.cdk for cover basename matching.
    final_cdk = path.parent / (path.stem + ".cdk")
    legacy_cdk = path.parent / (path.name + ".cdk")
    if legacy_cdk.is_file() and legacy_cdk != final_cdk:
        try:
            if final_cdk.is_file():
                final_cdk.unlink()
            legacy_cdk.rename(final_cdk)
        except OSError:
            return False
    return final_cdk.is_file()


def pack_staged_roms(
    repo_root: Path,
    roms_staging_root: Path,
    *,
    compress_gb_speed: bool = False,
) -> tuple[int, int]:
    """
    Mutate roms_staging_root in place. Returns (compressed_count, skipped_count).
    """
    if not roms_staging_root.is_dir():
        return 0, 0

    _dedupe_uncompressed_vs_lzma(roms_staging_root)

    compressed = 0
    skipped = 0

    # MSX / Amstrad: jeu.dsk → jeu.cdk (ext cdk; MSX core treats cdk like dsk)
    for p in list(sorted(roms_staging_root.rglob("*.dsk"))):
        if not p.is_file():
            continue
        try:
            rel = p.relative_to(roms_staging_root)
        except ValueError:
            continue
        top = rel.parts[0].lower() if rel.parts else ""
        if top == "msx":
            if _convert_dsk_if_needed(repo_root, "dsk2lzma.py", p):
                try:
                    p.unlink()
                except OSError:
                    pass
                compressed += 1
        elif top == "amstrad":
            if _convert_dsk_if_needed(repo_root, "amdsk2lzma.py", p):
                try:
                    p.unlink()
                except OSError:
                    pass
                compressed += 1

    for p in sorted(roms_staging_root.rglob("*")):
        if not p.is_file():
            continue
        name_l = p.name.lower()
        if name_l.endswith(".lzma") or name_l.endswith(".cdk"):
            continue
        try:
            rel = p.relative_to(roms_staging_root)
        except ValueError:
            continue

        mode = _compression_mode_for_path(rel)
        if mode is None:
            continue

        raw = p.read_bytes()
        if top_folder := (rel.parts[0].lower() if rel.parts else ""):
            if top_folder == "md" and mode == "sms_md":
                raw = _byteswap_md16(raw)

        try:
            out = compress_payload_lzma(
                mode, raw, compress_gb_speed=compress_gb_speed
            )
        except Exception as ex:  # noqa: BLE001
            print(f"rom_frogfs_lzma: compress error {p}: {ex}", file=sys.stderr)
            skipped += 1
            continue

        if out is None or len(out) >= len(raw):
            skipped += 1
            continue

        out_path = p.parent / (p.stem + ".lzma")
        out_path.write_bytes(out)
        try:
            p.unlink()
        except OSError as ex:
            print(f"rom_frogfs_lzma: could not remove {p}: {ex}", file=sys.stderr)
            try:
                out_path.unlink()
            except OSError:
                pass
            skipped += 1
            continue
        compressed += 1

    if compressed:
        print(
            f"rom_frogfs_lzma: wrote {compressed} compressed ROM payload(s)"
            + (f", {skipped} unchanged" if skipped else "")
        )
    return compressed, skipped
