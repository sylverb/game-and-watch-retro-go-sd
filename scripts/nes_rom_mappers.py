#!/usr/bin/env python3
"""
Discover which FCEUmm mapper blobs under sd_content/cores/mappers/ are needed
for ROMs under roms/nes (and sd_content/roms/nes), using the same heuristics as
external/fceumm-go/nesmapper.py (iNES / FDS / NSF) plus NSF expansion chips
from external/fceumm-go/src/nsf.c (GNW rg_storage_copy_file_to_ram paths).
"""
from __future__ import annotations

import io
import lzma
from collections.abc import Callable
from pathlib import Path

NES_LZMA_FILTERS = (
    {
        "id": lzma.FILTER_LZMA1,
        "preset": 6,
        "dict_size": 16 * 1024,
    },
)

# NSF_HEADER.SoundChip (see external/fceumm-go/src/nsf.h); byte index 126.
_NSF_SOUNDCHIP_OFF = 126


def load_mapper_dict(repo_root: Path) -> dict[int, str]:
    p = repo_root / "external/fceumm-go/gen_mappers_table.py"
    text = p.read_text(encoding="utf-8")
    marker = "\nn = len(sys.argv)"
    i = text.find(marker)
    if i == -1:
        raise RuntimeError(f"{p}: expected '\\nn = len(sys.argv)' tail marker")
    ns: dict = {}
    exec(compile(text[:i], str(p), "exec"), ns)  # noqa: S102
    return ns["mapper_dict"]


def load_analyze_rom(repo_root: Path) -> Callable:
    """Load nesmapper.analyzeRom without running nesmapper.py CLI tail (sys.exit)."""
    p = repo_root / "external/fceumm-go/nesmapper.py"
    text = p.read_text(encoding="utf-8")
    marker = "\nn = len(sys.argv)"
    i = text.find(marker)
    if i == -1:
        raise RuntimeError(f"{p}: expected '\\nn = len(sys.argv)' before CLI block")
    ns: dict = {}
    exec(compile(text[:i], str(p), "exec"), ns)  # noqa: S102
    return ns["analyzeRom"]


def decompress_gnw_nes_lzma_body(payload: bytes) -> bytes:
    return lzma.decompress(payload, format=lzma.FORMAT_RAW, filters=list(NES_LZMA_FILTERS))


def nsf_expansion_mapper_stems(soundchip: int) -> set[str]:
    stems: set[str] = set()
    if soundchip & 0x01:
        stems.add("vrc6")
    if soundchip & 0x02:
        stems.add("vrc7")
    if soundchip & 0x08:
        stems.add("mmc5")
    if soundchip & 0x10:
        stems.add("n106")
    if soundchip & 0x20:
        stems.add("69")
    # bit 4: FDS expansion in NSF_init calls FDSSoundReset() only (no mapper_*.bin load).
    return stems


def _rom_payload_for_analysis(path: Path) -> tuple[bytes | None, str | None]:
    """
    Return (payload, error). payload None means skip file; error is stderr message.
    """
    suf = path.suffix.lower()
    try:
        raw = path.read_bytes()
    except OSError as e:
        return None, f"nes_rom_mappers: skip {path}: {e}"

    if suf == ".lzma":
        if raw.startswith(b"SMS+"):
            return None, f"nes_rom_mappers: skip non-NES LZMA layout {path}"
        try:
            return decompress_gnw_nes_lzma_body(raw), None
        except lzma.LZMAError as e:
            return None, f"nes_rom_mappers: LZMA error {path}: {e}"

    return raw, None


def _mapper_stems_for_nsf_payload(data: bytes, path: Path) -> tuple[set[str] | None, str | None]:
    if len(data) < 8 or data[0:4] != b"NESM" or data[4] != 0x1A:
        return None, f"nes_rom_mappers: not a valid NSF header {path}"
    if len(data) <= _NSF_SOUNDCHIP_OFF:
        return None, f"nes_rom_mappers: NSF too short {_NSF_SOUNDCHIP_OFF + 1}B {path}"
    return nsf_expansion_mapper_stems(data[_NSF_SOUNDCHIP_OFF]), None


def littlefs_nes_mapper_stems(
    repo_root: Path,
    project_roms: Path | None,
    sd_roms: Path | None,
    *,
    mapper_dict: dict[int, str] | None = None,
    analyze_rom: Callable | None = None,
) -> tuple[frozenset[str] | None, list[str]]:
    """
    Return (needed board stems) or None to keep every mapper (ambiguous scan).

    Stems match the mapper_<stem>.bin blob names; they are packed into a pruned
    mappers.pak. An empty frozenset means "no NES ROMs -> no mapper blobs".
    """
    warnings: list[str] = []
    nes_dirs: list[Path] = []
    for base in (project_roms, sd_roms):
        if base is None or not base.is_dir():
            continue
        nd = base / "nes"
        if nd.is_dir():
            nes_dirs.append(nd)

    if not nes_dirs:
        return frozenset(), warnings

    if mapper_dict is None:
        mapper_dict = load_mapper_dict(repo_root)
    if analyze_rom is None:
        analyze_rom = load_analyze_rom(repo_root)

    stems: set[str] = set()
    ambiguous = False

    exts = {".nes", ".fds", ".nsf", ".unf", ".unif", ".lzma"}

    for nd in nes_dirs:
        for path in sorted(nd.rglob("*")):
            if not path.is_file() or path.name == ".DS_Store":
                continue
            if path.suffix.lower() not in exts:
                continue

            data, err = _rom_payload_for_analysis(path)
            if err:
                warnings.append(err)
            if not data:
                ambiguous = True
                continue

            low = path.name.lower()
            if low.endswith(".nsf"):
                nsf_stems, nsf_err = _mapper_stems_for_nsf_payload(data, path)
                if nsf_err:
                    warnings.append(nsf_err)
                    ambiguous = True
                    continue
                stems |= nsf_stems
                continue

            bio = io.BytesIO(data)
            mapper = analyze_rom(bio, path.name)[0]

            if mapper == -3:
                nsf_stems, nsf_err = _mapper_stems_for_nsf_payload(data, path)
                if nsf_err:
                    warnings.append(nsf_err)
                    ambiguous = True
                else:
                    stems |= nsf_stems
                continue

            if mapper == -2:
                # FDS disk games: no per-game mapper blob load on GNW (see fds.c).
                continue

            if mapper == -1:
                warnings.append(f"nes_rom_mappers: unknown format / mapper {path}")
                ambiguous = True
                continue

            name = mapper_dict.get(mapper)
            if not name:
                warnings.append(f"nes_rom_mappers: mapper {mapper} not in gen_mappers_table {path}")
                ambiguous = True
                continue

            stems.add(name)

    if ambiguous:
        return None, warnings

    return frozenset(stems), warnings


def main() -> int:
    import argparse
    import sys

    p = argparse.ArgumentParser(description="List needed mapper stems for roms/nes trees.")
    p.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--project-roms", type=Path, default=None)
    p.add_argument("--sd-roms", type=Path, default=None)
    args = p.parse_args()
    repo = args.repo.resolve()
    stems, w = littlefs_nes_mapper_stems(repo, args.project_roms, args.sd_roms)
    for line in w:
        print(line, file=sys.stderr)
    if stems is None:
        print("(ambiguous — pack all mappers)")
        return 2
    for s in sorted(stems):
        print(s)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
