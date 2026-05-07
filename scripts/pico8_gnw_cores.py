#!/usr/bin/env python3
"""
Fetch Macs75/pico8_gnw_distro release ZIP and merge its cores/ tree into a mounted LittleFS.
"""
from __future__ import annotations

import io
import pathlib
import sys
import urllib.request
import zipfile

# Release bundle: cores/ contains pico8.bin, pico8.ro, etc.
PICO8_GNW_CORES_ZIP_URL = (
    "https://github.com/Macs75/pico8_gnw_distro/releases/download/"
    "v.0.1.4b/pico8_cores-2026-05-07.zip"
)

PICO8_CACHE_FILENAME = "pico8_gnw_cores-2026-05-07.zip"

# Same as sd_cores_pack: never place the placeholder stub on LittleFS.
_SKIP_BASENAMES = frozenset({"pico8_stub.bin"})


def _rel_paths_under_cores_in_zip(zf: zipfile.ZipFile) -> list[tuple[str, zipfile.ZipInfo]]:
    out: list[tuple[str, zipfile.ZipInfo]] = []
    for info in zf.infolist():
        if info.is_dir():
            continue
        name = info.filename.replace("\\", "/")
        parts = name.split("/")
        try:
            idx = next(i for i, p in enumerate(parts) if p.lower() == "cores")
        except StopIteration:
            continue
        rel = "/".join(parts[idx + 1 :]).strip("/")
        if not rel:
            continue
        if pathlib.Path(rel).name in _SKIP_BASENAMES:
            continue
        out.append((rel, info))
    return out


def fetch_zip_bytes(cache_path: pathlib.Path, url: str, *, force_refresh: bool) -> bytes:
    if cache_path.is_file() and not force_refresh:
        return cache_path.read_bytes()

    req = urllib.request.Request(
        url,
        headers={"User-Agent": "game-and-watch-retro-go-sd littlefs build"},
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            data = resp.read()
    except OSError as e:
        print(f"pico8_gnw_cores: failed to download {url}: {e}", file=sys.stderr)
        raise

    cache_path.parent.mkdir(parents=True, exist_ok=True)
    cache_path.write_bytes(data)
    return data


def merge_pico8_zip_into_littlefs_cores(
    fs,
    zip_bytes: bytes,
    *,
    omit_basenames: frozenset[str] | None = None,
) -> int:
    """Write every file from */cores/* in the ZIP to /cores/ on fs. Returns file count."""
    omit_basenames = omit_basenames or frozenset()
    n = 0
    with zipfile.ZipFile(io.BytesIO(zip_bytes), "r") as zf:
        entries = _rel_paths_under_cores_in_zip(zf)
        if not entries:
            print("pico8_gnw_cores: no files found under a 'cores/' path in ZIP", file=sys.stderr)
            return 0
        for rel, info in sorted(entries, key=lambda x: x[0]):
            if pathlib.Path(rel).name in omit_basenames:
                print(f"pico8_gnw_cores: skipping /cores/{rel} (bundled elsewhere)")
                continue
            data = zf.read(info.filename)
            dst = "/cores/" + rel
            parent = dst.rsplit("/", 1)[0]
            if parent:
                fs.makedirs(parent, exist_ok=True)
            with fs.open(dst, "wb") as out_f:
                out_f.write(data)
            n += 1
            print(f"pico8_gnw_cores: installed /cores/{rel} ({len(data)} bytes)")
    return n


def extract_pico8_ro_from_zip_bytes(zip_bytes: bytes, dest_file: pathlib.Path) -> bool:
    """Extract single pico8.ro from distro ZIP to dest_file. Returns False if missing."""
    with zipfile.ZipFile(io.BytesIO(zip_bytes), "r") as zf:
        for rel, info in _rel_paths_under_cores_in_zip(zf):
            if pathlib.Path(rel).name != "pico8.ro":
                continue
            dest_file.parent.mkdir(parents=True, exist_ok=True)
            dest_file.write_bytes(zf.read(info.filename))
            return True
    return False


def fetch_and_merge_into_littlefs(
    fs,
    build_dir: pathlib.Path,
    url: str,
    *,
    force_refresh: bool,
    omit_basenames: frozenset[str] | None = None,
) -> int:
    cache_path = build_dir / PICO8_CACHE_FILENAME
    zip_bytes = fetch_zip_bytes(cache_path, url, force_refresh=force_refresh)
    return merge_pico8_zip_into_littlefs_cores(fs, zip_bytes, omit_basenames=omit_basenames)
