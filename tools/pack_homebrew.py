#!/usr/bin/env python3
"""
Package a standalone homebrew build into the GWHB-header .bin format
loaded from /homebrews/ (see Core/Inc/retro-go/gwhb.h and
run_gwhb_homebrew() in Core/Src/retro-go/rg_emulators.c).

File layout (little-endian):

    offset 0   "GWHB" magic
    offset 4   header_version  u16  == GWHB_META_VERSION
    offset 6   header_length   u16  == sizeof(gwhb_meta_t) + cover_size
    offset 8   gwhb_meta_t
    ...        optional cover JPEG
    8+header_length  code payload (RAM_EMU, entry at offset 0)

Usage:

    tools/pack_homebrew.py \\
        --elf build/celeste_core.elf --bin build/celeste_core.bin \\
        --name "Celeste" --version 1.0.0 \\
        --cover path/to/cover.jpg \\
        --out Celeste.bin
"""
from __future__ import annotations

import argparse
import struct
import subprocess
import sys
from pathlib import Path

GWHB_MAGIC = b"GWHB"
GWHB_HEADER_MIN_SIZE = 8
GWHB_META_VERSION = 1
COVER_SIZE_MAX = 10 * 1024  # must match COVER_SIZE in gui.c
# Must match COVER_MAX_WIDTH/HEIGHT in Core/Src/retro-go/gui.c (HW JPEG scratch).
COVER_MAX_WIDTH = 186
COVER_MAX_HEIGHT = 100

# Mirror gwhb_meta_t exactly (Core/Inc/retro-go/gwhb.h).
META_STRUCT_FORMAT = "<IIIIIII32sBBBB32s"
META_STRUCT_SIZE = struct.calcsize(META_STRUCT_FORMAT)
assert META_STRUCT_SIZE == 96, META_STRUCT_SIZE


def parse_version(spec: str) -> tuple[int, int, int]:
    s = spec.strip()
    if s[:1] in ("v", "V"):
        s = s[1:]
    parts = s.split(".")
    if len(parts) != 3:
        sys.exit(f"error: --version expects X.Y.Z, got {spec!r}")
    try:
        major, minor, patch = (int(p) for p in parts)
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

    # Resize when needed so the firmware HW-JPEG scratch cannot overflow.
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


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--elf", type=Path, required=True, help="linked homebrew ELF")
    ap.add_argument("--bin", type=Path, required=True,
                    help="flat RAM_EMU payload (objcopy -O binary)")
    ap.add_argument("--name", required=True, help="display name (max 31 bytes)")
    ap.add_argument("--version", default="1.0.0", help="X.Y.Z (default: %(default)s)")
    ap.add_argument("--cover", type=Path, default=None,
                    help="optional JPEG cover (<= 10 KiB)")
    ap.add_argument("--flags", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--nm", default="arm-none-eabi-nm")
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

    def sym(name: str) -> int:
        if name not in symbols:
            sys.exit(f"error: symbol {name} not found in {args.elf}")
        return symbols[name]

    ram_emu_start = sym("__RAM_EMU_START__")
    code_end = sym("__CORE_CODE_END__")
    bss_end = sym("__CORE_BSS_END__")
    code_size = code_end - ram_emu_start
    bss_size = bss_end - code_end

    payload = args.bin.read_bytes()
    if len(payload) != code_size:
        sys.exit(
            f"error: {args.bin} is {len(payload)} bytes, expected code_size={code_size} "
            f"(from __CORE_CODE_END__ - __RAM_EMU_START__)"
        )

    abi_version_off = sym("GW_CORE_BUILT_ABI_VERSION") - ram_emu_start
    abi_size_off = sym("GW_CORE_BUILT_ABI_SIZE") - ram_emu_start
    required_abi_version = read_u32_at(payload, abi_version_off)
    required_abi_min_size = read_u32_at(payload, abi_size_off)

    cover = prepare_cover(args.cover)
    cover_offset = (GWHB_HEADER_MIN_SIZE + META_STRUCT_SIZE) if cover else 0
    cover_size = len(cover)
    header_length = META_STRUCT_SIZE + cover_size

    ver_maj, ver_min, ver_pat = parse_version(args.version)

    meta = struct.pack(
        META_STRUCT_FORMAT,
        required_abi_version,
        required_abi_min_size,
        args.flags,
        code_size,
        bss_size,
        cover_offset,
        cover_size,
        name_bytes.ljust(32, b"\0"),
        ver_maj,
        ver_min,
        ver_pat,
        0,
        b"\0" * 32,
    )
    assert len(meta) == META_STRUCT_SIZE

    envelope = (
        GWHB_MAGIC
        + struct.pack("<HH", GWHB_META_VERSION, header_length)
        + meta
        + cover
        + payload
    )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(envelope)

    print(f"pack_homebrew: wrote {args.out} ({len(envelope)} bytes)")
    print(f"  name={args.name!r} version={ver_maj}.{ver_min}.{ver_pat}")
    print(f"  code={code_size}B bss={bss_size}B cover={cover_size}B")
    print(
        f"  required_abi_version={required_abi_version} "
        f"required_abi_min_size={required_abi_min_size}"
    )


if __name__ == "__main__":
    main()
