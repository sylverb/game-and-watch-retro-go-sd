#!/usr/bin/env python3
"""
Package a standalone homebrew build into the GWHB-header .bin format
loaded from /homebrews/ (see Core/Inc/retro-go/gwhb.h and
run_gwhb_homebrew() in Core/Src/retro-go/rg_emulators.c).

Same multi-segment payload model as pack_core.py: segment 0 is always
RAM_EMU (entry trampoline at offset 0); optional ITCM / RAM_UC segments
are auto-detected from ELF symbols (or passed via --segment).

File layout (little-endian):

    offset 0   "GWHB" magic
    offset 4   header_version  u16  == GWHB_META_VERSION
    offset 6   header_length   u16  == sizeof(gwhb_meta_t) + cover_size
    offset 8   gwhb_meta_t     (segments[] like gnw_core_meta_t)
    ...        optional cover JPEG
    8+header_length  payload: segments[0].code_size, then [1], ...

Usage:

    tools/pack_homebrew.py \\
        --elf build/celeste_core.elf --bin build/celeste_core.bin \\
        --name "Celeste" --version 1.0.0 \\
        --cover path/to/cover.jpg \\
        --out Celeste.bin
"""
from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

GWHB_MAGIC = b"GWHB"
GWHB_HEADER_MIN_SIZE = 8
GWHB_META_VERSION = 1
COVER_SIZE_MAX = 10 * 1024  # must match COVER_SIZE in gui.c
COVER_MAX_WIDTH = 186
COVER_MAX_HEIGHT = 100

GNW_CORE_MAX_SEGMENTS = 4
REGION_NAME_TO_ID = {"ram_emu": 0, "itcm": 1, "ram_uc": 2}
REGION_ID_TO_NAME = {v: k for k, v in REGION_NAME_TO_ID.items()}

SEGMENT_STRUCT_FORMAT = "<III"
SEGMENT_STRUCT_SIZE = struct.calcsize(SEGMENT_STRUCT_FORMAT)
assert SEGMENT_STRUCT_SIZE == 12, SEGMENT_STRUCT_SIZE

# Mirror gwhb_meta_t (Core/Inc/retro-go/gwhb.h):
# 4x u32 + segments[4] + cover_offset/size + name[32] + version(4) + reserved[16]
META_STRUCT_FORMAT = (
    "<IIII"
    + (SEGMENT_STRUCT_FORMAT[1:] * GNW_CORE_MAX_SEGMENTS)
    + "II32sBBBB16s"
)
META_STRUCT_SIZE = struct.calcsize(META_STRUCT_FORMAT)
assert META_STRUCT_SIZE == 124, META_STRUCT_SIZE

AUTO_EXTRA_SEGMENTS = (
    {
        "region": "itcm",
        "start": "__ITCM_CORE_START__",
        "code_end": "__CORE_ITCM_CODE_END__",
        "bss_end": "__CORE_ITCM_BSS_END__",
        "section": ".core_itcm",
    },
    {
        "region": "ram_uc",
        "start": "__RAM_UC_CORE_START__",
        "code_end": "__CORE_RAM_UC_CODE_END__",
        "bss_end": "__CORE_RAM_UC_BSS_END__",
        "section": ".core_ram_uc",
    },
)


def objcopy_tool_from_nm(nm_tool: str) -> str:
    nm_tool = str(nm_tool)
    if nm_tool.endswith("nm"):
        return nm_tool[:-2] + "objcopy"
    return "arm-none-eabi-objcopy"


def extract_section_bytes(objcopy: str, elf_path: Path, section: str, expected_size: int) -> bytes:
    if expected_size == 0:
        return b""
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
        tmp_path = Path(tmp.name)
    try:
        subprocess.run(
            [objcopy, "-O", "binary", f"--only-section={section}", str(elf_path), str(tmp_path)],
            check=True,
            capture_output=True,
            text=True,
        )
        data = tmp_path.read_bytes()
    except subprocess.CalledProcessError as e:
        sys.exit(
            f"error: objcopy failed extracting {section} from {elf_path}: "
            f"{e.stderr or e.stdout or e}"
        )
    finally:
        tmp_path.unlink(missing_ok=True)
    if len(data) != expected_size:
        sys.exit(
            f"error: section {section} extracted as {len(data)} bytes, "
            f"expected code_size={expected_size}"
        )
    return data


def discover_auto_segments(symbols: dict[str, int], elf_path: Path, objcopy: str):
    found = []
    for spec in AUTO_EXTRA_SEGMENTS:
        needed = (spec["start"], spec["code_end"], spec["bss_end"])
        if not all(name in symbols for name in needed):
            continue
        region = REGION_NAME_TO_ID[spec["region"]]
        seg_start = symbols[spec["start"]]
        seg_code_end = symbols[spec["code_end"]]
        seg_bss_end = symbols[spec["bss_end"]]
        code_size = seg_code_end - seg_start
        bss_size = seg_bss_end - seg_code_end
        if code_size < 0 or bss_size < 0:
            sys.exit(
                f"error: auto segment {spec['region']}: negative size "
                f"(code={code_size}, bss={bss_size})"
            )
        if code_size == 0 and bss_size == 0:
            continue
        payload = extract_section_bytes(objcopy, elf_path, spec["section"], code_size)
        found.append((region, code_size, bss_size, payload, spec["region"]))
    return found


def parse_segment_arg(spec: str):
    parts = spec.split(":", 4)
    if len(parts) != 5:
        sys.exit(
            f"error: --segment must be "
            f"region:start_symbol:code_end_symbol:bss_end_symbol:bin_file, got {spec!r}"
        )
    region_name, start_symbol, code_end_symbol, bss_end_symbol, bin_file = parts
    region = REGION_NAME_TO_ID.get(region_name)
    if region is None:
        sys.exit(
            f"error: --segment region {region_name!r} must be one of "
            f"{sorted(REGION_NAME_TO_ID)}"
        )
    return region, start_symbol, code_end_symbol, bss_end_symbol, Path(bin_file)


def parse_version(spec: str) -> tuple[int, int, int]:
    s = spec.strip()
    if not s or s.upper() == "NOTAG":
        return 0, 0, 0
    if s[:1] in ("v", "V"):
        s = s[1:]
    m = re.match(r"^(\d+)\.(\d+)\.(\d+)", s)
    if not m:
        sys.exit(
            f"error: --version expects X.Y.Z or git describe (vX.Y.Z…), "
            f"or NOTAG; got {spec!r}"
        )
    try:
        major, minor, patch = (int(m.group(i)) for i in (1, 2, 3))
    except ValueError:
        sys.exit(f"error: --version components must be integers, got {spec!r}")
    for name, val in (("major", major), ("minor", minor), ("patch", patch)):
        if not 0 <= val <= 255:
            sys.exit(f"error: --version {name}={val} out of range 0..255")
    return major, minor, patch


def run_nm(nm_tool: str, elf_path: Path) -> dict[str, int]:
    out = subprocess.run(
        [nm_tool, str(elf_path)], check=True, capture_output=True, text=True
    )
    symbols: dict[str, int] = {}
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        addr, _kind, name = parts[0], parts[1], parts[2]
        try:
            symbols[name] = int(addr, 16)
        except ValueError:
            continue
    return symbols


def read_u32_at(buf: bytes, off: int) -> int:
    if off < 0 or off + 4 > len(buf):
        sys.exit(f"error: cannot read u32 at payload offset {off}")
    return struct.unpack_from("<I", buf, off)[0]


def jpeg_sof_dimensions(data: bytes) -> tuple[int, int] | None:
    if len(data) < 4 or data[:2] != b"\xff\xd8":
        return None
    i = 2
    while i + 9 < len(data):
        if data[i] != 0xFF:
            i += 1
            continue
        while i < len(data) and data[i] == 0xFF:
            i += 1
        if i >= len(data):
            return None
        marker = data[i]
        i += 1
        if marker in (0xD9, 0xDA):
            return None
        if marker == 0x01 or 0xD0 <= marker <= 0xD7:
            continue
        if i + 2 > len(data):
            return None
        seglen = (data[i] << 8) | data[i + 1]
        if seglen < 2 or i + seglen > len(data):
            return None
        if 0xC0 <= marker <= 0xC3:
            h = (data[i + 3] << 8) | data[i + 4]
            w = (data[i + 5] << 8) | data[i + 6]
            return (w, h) if w > 0 and h > 0 else None
        i += seglen
    return None


def prepare_cover(path: Path | None) -> bytes:
    if path is None:
        return b""
    if not path.is_file():
        sys.exit(f"error: cover not found: {path}")
    data = path.read_bytes()

    dims = jpeg_sof_dimensions(data)
    need_resize = dims is None or dims[0] > COVER_MAX_WIDTH or dims[1] > COVER_MAX_HEIGHT
    if need_resize or path.suffix.lower() in {".png", ".bmp", ".gif", ".webp"}:
        try:
            from PIL import Image
            import io
        except ImportError:
            sys.exit(
                f"error: cover {path} needs resize/convert to fit "
                f"{COVER_MAX_WIDTH}x{COVER_MAX_HEIGHT}; install Pillow "
                f"(pip install Pillow) or provide a JPEG already within limits"
            )
        else:
            img = Image.open(path).convert("RGB")
            img.thumbnail((COVER_MAX_WIDTH, COVER_MAX_HEIGHT))
            buf = io.BytesIO()
            img.save(buf, format="JPEG", quality=85, optimize=True)
            data = buf.getvalue()
            dims = img.size
            print(
                f"pack_homebrew: cover resized to {dims[0]}x{dims[1]} "
                f"({len(data)} bytes)"
            )

    if len(data) > COVER_SIZE_MAX:
        sys.exit(
            f"error: cover {path} is {len(data)} bytes, max is {COVER_SIZE_MAX} "
            f"(gui.c COVER_SIZE)"
        )
    dims = jpeg_sof_dimensions(data)
    if dims and (dims[0] > COVER_MAX_WIDTH or dims[1] > COVER_MAX_HEIGHT):
        sys.exit(
            f"error: cover {path} is {dims[0]}x{dims[1]}, max is "
            f"{COVER_MAX_WIDTH}x{COVER_MAX_HEIGHT} (gui.c COVER_MAX_*)"
        )
    if not (data[:2] == b"\xff\xd8" or path.suffix.lower() in {".jpg", ".jpeg", ".img"}):
        print(
            f"warning: cover {path} does not look like JPEG; "
            "coverflow expects HW-JPEG-decodable data",
            file=sys.stderr,
        )
    return data


def pack_meta(
    required_abi_version: int,
    required_abi_min_size: int,
    flags: int,
    segments: list[tuple[int, int, int]],
    cover_offset: int,
    cover_size: int,
    name_bytes: bytes,
    ver_maj: int,
    ver_min: int,
    ver_pat: int,
) -> bytes:
    fields: list = [
        required_abi_version,
        required_abi_min_size,
        flags,
        len(segments),
    ]
    padded = list(segments) + [(0, 0, 0)] * (GNW_CORE_MAX_SEGMENTS - len(segments))
    for region, code_size, bss_size in padded:
        fields.extend([region, code_size, bss_size])
    fields.extend(
        [
            cover_offset,
            cover_size,
            name_bytes.ljust(32, b"\0"),
            ver_maj,
            ver_min,
            ver_pat,
            0,
            b"\x00" * 16,
        ]
    )
    meta = struct.pack(META_STRUCT_FORMAT, *fields)
    assert len(meta) == META_STRUCT_SIZE
    return meta


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", type=Path, required=True, help="linked homebrew ELF")
    ap.add_argument("--bin", type=Path, required=True,
                    help="flat RAM_EMU payload (objcopy -O binary)")
    ap.add_argument("--name", required=True, help="display name (max 31 bytes)")
    ap.add_argument("--version", default="1.0.0",
                    help="X.Y.Z, git describe (vX.Y.Z…), or NOTAG → 0.0.0 (default: %(default)s)")
    ap.add_argument("--cover", type=Path, default=None,
                    help="optional JPEG cover (<= 10 KiB)")
    ap.add_argument("--flags", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--segment", action="append", default=[],
                    help="repeatable: region:start:code_end:bss_end:bin (segments 1..3)")
    ap.add_argument("--no-auto-segments", action="store_true",
                    help="do not auto-detect ITCM/RAM_UC from ELF symbols")
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    ap.add_argument("--objcopy", default=None)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    name_bytes = args.name.encode()
    if len(name_bytes) >= 32:
        sys.exit(f"error: --name too long (max 31 bytes): {args.name!r}")

    if not args.elf.is_file():
        sys.exit(f"error: ELF not found: {args.elf}")
    if not args.bin.is_file():
        sys.exit(f"error: bin not found: {args.bin}")

    symbols = run_nm(args.nm, args.elf)
    objcopy = args.objcopy or objcopy_tool_from_nm(args.nm)

    def sym(name: str) -> int:
        if name not in symbols:
            sys.exit(f"error: symbol {name} not found in {args.elf}")
        return symbols[name]

    ram_emu_start = sym("__RAM_EMU_START__")
    code_end = sym("__CORE_CODE_END__")
    bss_end = sym("__CORE_BSS_END__")
    seg0_code_size = code_end - ram_emu_start
    seg0_bss_size = bss_end - code_end

    seg0_payload = args.bin.read_bytes()
    if len(seg0_payload) != seg0_code_size:
        sys.exit(
            f"error: {args.bin} is {len(seg0_payload)} bytes, expected code_size={seg0_code_size} "
            f"(from __CORE_CODE_END__ - __RAM_EMU_START__)"
        )

    abi_version_off = sym("GW_CORE_BUILT_ABI_VERSION") - ram_emu_start
    abi_size_off = sym("GW_CORE_BUILT_ABI_SIZE") - ram_emu_start
    required_abi_version = read_u32_at(seg0_payload, abi_version_off)
    required_abi_min_size = read_u32_at(seg0_payload, abi_size_off)

    segments: list[tuple[int, int, int]] = [
        (REGION_NAME_TO_ID["ram_emu"], seg0_code_size, seg0_bss_size)
    ]
    payloads: list[bytes] = [seg0_payload]
    used_regions = {REGION_NAME_TO_ID["ram_emu"]}

    for region, start_symbol, code_end_symbol, bss_end_symbol, bin_file in (
        parse_segment_arg(s) for s in args.segment
    ):
        seg_start = sym(start_symbol)
        seg_code_end = sym(code_end_symbol)
        seg_bss_end = sym(bss_end_symbol)
        seg_code_size = seg_code_end - seg_start
        seg_bss_size = seg_bss_end - seg_code_end
        seg_payload = bin_file.read_bytes()
        if len(seg_payload) != seg_code_size:
            sys.exit(
                f"error: {bin_file} is {len(seg_payload)} bytes, expected "
                f"code_size={seg_code_size}"
            )
        segments.append((region, seg_code_size, seg_bss_size))
        payloads.append(seg_payload)
        used_regions.add(region)

    if not args.no_auto_segments:
        for region, code_size, bss_size, payload, region_name in discover_auto_segments(
            symbols, args.elf, objcopy
        ):
            if region in used_regions:
                continue
            print(
                f"pack_homebrew: auto segment {region_name} "
                f"(code={code_size}B bss={bss_size}B)"
            )
            segments.append((region, code_size, bss_size))
            payloads.append(payload)
            used_regions.add(region)

    if len(segments) > GNW_CORE_MAX_SEGMENTS:
        sys.exit(f"error: {len(segments)} segments total, max is {GNW_CORE_MAX_SEGMENTS}")
    if segments[0][0] != REGION_NAME_TO_ID["ram_emu"]:
        sys.exit("error: segment[0] must be ram_emu")

    cover = prepare_cover(args.cover)
    cover_offset = (GWHB_HEADER_MIN_SIZE + META_STRUCT_SIZE) if cover else 0
    cover_size = len(cover)
    header_length = META_STRUCT_SIZE + cover_size

    ver_maj, ver_min, ver_pat = parse_version(args.version)
    meta = pack_meta(
        required_abi_version,
        required_abi_min_size,
        args.flags,
        segments,
        cover_offset,
        cover_size,
        name_bytes,
        ver_maj,
        ver_min,
        ver_pat,
    )

    envelope = (
        GWHB_MAGIC
        + struct.pack("<HH", GWHB_META_VERSION, header_length)
        + meta
        + cover
        + b"".join(payloads)
    )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(envelope)

    print(f"pack_homebrew: wrote {args.out} ({len(envelope)} bytes)")
    print(f"  name={args.name!r} version={ver_maj}.{ver_min}.{ver_pat}")
    print(f"  cover={cover_size}B")
    for i, (region, code_size, bss_size) in enumerate(segments):
        print(
            f"  segment[{i}]: region={REGION_ID_TO_NAME.get(region, region)} "
            f"code={code_size}B bss={bss_size}B"
        )
    print(
        f"  required_abi_version={required_abi_version} "
        f"required_abi_min_size={required_abi_min_size}"
    )


if __name__ == "__main__":
    main()
