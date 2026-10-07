#!/usr/bin/env python3
"""Merge a GWHB install zip's homebrews/ tree into one or more sd_content dirs.

The installer project's non-debug release asset looks like:

    homebrews/installer.bin

(The *-debug.zip sibling only ships ELF/map for addr2line — not usable here.)

Optional helper to merge installer-retro-go-sd's homebrews/ into an sd_content
tree. Release CI no longer embeds installer.bin in retro-go_update.bin —
cores/homebrews are installed separately — but this script remains useful for
local one-off packages.
"""

from __future__ import annotations

import argparse
import io
import os
import sys
import urllib.request
import zipfile


GWHB_MAGIC = b"GWHB"


def fetch_zip(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "retro-go-sd-release"})
    with urllib.request.urlopen(req) as resp:
        return resp.read()


def merge_homebrews(zip_bytes: bytes, content_dirs: list[str]) -> list[str]:
    placed: list[str] = []
    with zipfile.ZipFile(io.BytesIO(zip_bytes)) as zf:
        names = [n for n in zf.namelist() if not n.endswith("/")]
        hb = [n for n in names if n.replace("\\", "/").startswith("homebrews/")]
        if not hb:
            raise SystemExit(
                "install zip has no homebrews/ members "
                f"(found: {names[:8]}{'…' if len(names) > 8 else ''})"
            )
        for content_dir in content_dirs:
            for name in hb:
                rel = name.replace("\\", "/")
                data = zf.read(name)
                if rel.endswith(".bin") and not data.startswith(GWHB_MAGIC):
                    raise SystemExit(f"{rel}: missing GWHB magic")
                dest = os.path.join(content_dir, *rel.split("/"))
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with open(dest, "wb") as out:
                    out.write(data)
                placed.append(dest)
    return placed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--url",
        required=True,
        help="Install zip URL (e.g. …/installer-v1.0.0.zip, not *-debug.zip)",
    )
    ap.add_argument(
        "--content",
        action="append",
        required=True,
        metavar="DIR",
        help="sd_content tree to merge into (repeatable)",
    )
    args = ap.parse_args()

    for d in args.content:
        if not os.path.isdir(d):
            raise SystemExit(f"content dir not found: {d}")

    print(f"Fetching {args.url}")
    blob = fetch_zip(args.url)
    placed = merge_homebrews(blob, args.content)
    for p in placed:
        print(f"  wrote {p} ({os.path.getsize(p)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
