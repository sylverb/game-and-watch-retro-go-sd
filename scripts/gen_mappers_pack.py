#!/usr/bin/env python3
"""
Pack every FCEUmm per-mapper blob (build/nes_mappers/mapper_<stem>.bin) into a
single file (default sd_content/cores/mappers/mappers.pak), replacing the old
one-file-per-mapper layout plus mappers_table.bin.

Format (little-endian):
  Header (16 bytes):
    0  : magic 'MPAK'
    4  : uint32 version
    8  : uint32 num_entries      (== max mapper number + 1)
    12 : uint32 reserved (0)
  Index (num_entries * 8 bytes), indexed by iNES mapper number:
    +0 : uint32 offset (absolute from file start; 0 => absent)
    +4 : uint32 size   (0 => absent)
  Blobs: concatenated, 4-byte aligned.

A mapper number is "absent" (offset == size == 0) when its board code is shared
and lives in the main core (mmc3, latch, ...), when no ROM needs it (pruned
build), or when the number maps to no board. The runtime treats a 0-size entry
exactly like the old "mapper_<x>.bin not found" case.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

PACK_MAGIC = b"MPAK"
PACK_VERSION = 1
HEADER_SIZE = 16
INDEX_ENTRY_SIZE = 8


def load_mapper_dict(repo_root: Path) -> dict[int, str]:
    """Import mapper_dict from external/fceumm-go/gen_mappers_table.py without
    running its CLI tail (which sys.exit()s when called with no arguments)."""
    p = repo_root / "external/fceumm-go/gen_mappers_table.py"
    text = p.read_text(encoding="utf-8")
    marker = "\nn = len(sys.argv)"
    i = text.find(marker)
    if i == -1:
        raise RuntimeError(f"{p}: expected '\\nn = len(sys.argv)' tail marker")
    ns: dict = {}
    exec(compile(text[:i], str(p), "exec"), ns)  # noqa: S102
    return ns["mapper_dict"]


def build_pack(
    bins_dir: Path,
    mapper_dict: dict[int, str],
    output_path: Path,
    allowed_stems: frozenset[str] | None = None,
) -> tuple[int, int]:
    """Build a mappers pack. Returns (num_blobs, num_mapper_numbers_populated).

    ``allowed_stems`` is None to include every available blob, or a set of stems
    to keep (pruned build). A stem with no mapper_<stem>.bin on disk is silently
    treated as absent (shared/main-core code such as mmc3)."""
    bins_dir = Path(bins_dir)
    output_path = Path(output_path)
    max_mappers = max(mapper_dict) + 1

    # Collect the unique stems referenced by the mapper table, keeping only those
    # that are allowed and have a blob on disk (dedup: many numbers -> one stem).
    stem_bytes: dict[str, bytes] = {}
    for stem in sorted(set(mapper_dict.values())):
        if not stem:
            continue
        if allowed_stems is not None and stem not in allowed_stems:
            continue
        blob = bins_dir / f"mapper_{stem}.bin"
        if not blob.is_file():
            continue  # shared code compiled into the main core (mmc3, latch, ...)
        stem_bytes[stem] = blob.read_bytes()

    # Assign each blob an offset in the pack (4-byte aligned). The index is
    # always full-size (num_entries == max_mappers) so a pruned pack keeps the
    # same runtime lookup arithmetic.
    index_size = max_mappers * INDEX_ENTRY_SIZE
    cursor = HEADER_SIZE + index_size
    stem_loc: dict[str, tuple[int, int]] = {}
    blob_region = bytearray()
    for stem in sorted(stem_bytes):
        data = stem_bytes[stem]
        pad = (-cursor) % 4
        if pad:
            blob_region.extend(b"\x00" * pad)
            cursor += pad
        stem_loc[stem] = (cursor, len(data))
        blob_region.extend(data)
        cursor += len(data)

    index = bytearray(index_size)
    populated = 0
    for num in range(max_mappers):
        stem = mapper_dict.get(num)
        if not stem:
            continue
        loc = stem_loc.get(stem)
        if loc is None:
            continue
        offset, size = loc
        struct.pack_into("<II", index, num * INDEX_ENTRY_SIZE, offset, size)
        populated += 1

    header = PACK_MAGIC + struct.pack("<III", PACK_VERSION, max_mappers, 0)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as f:
        f.write(header)
        f.write(index)
        f.write(blob_region)

    return len(stem_bytes), populated


def main() -> int:
    p = argparse.ArgumentParser(description="Pack FCEUmm mappers into a single file.")
    p.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--bins-dir", type=Path, required=True,
                   help="Directory holding the mapper_<stem>.bin blobs.")
    p.add_argument("--output", type=Path, required=True, help="Output .pak file.")
    p.add_argument("--allow-stem", action="append", default=None,
                   help="Restrict the pack to these stems (repeatable). Omit for all.")
    args = p.parse_args()

    repo = args.repo.resolve()
    mapper_dict = load_mapper_dict(repo)
    allowed = frozenset(args.allow_stem) if args.allow_stem is not None else None
    nblobs, npop = build_pack(args.bins_dir, mapper_dict, args.output, allowed_stems=allowed)
    print(f'file "{args.output}" generated ({nblobs} blob(s), {npop} mapper number(s))')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
