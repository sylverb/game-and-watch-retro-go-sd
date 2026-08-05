#!/usr/bin/env python3
"""
Package a standalone "core" build (see cores/_template/, cores/wsv/) into
the CORE-header .bin format the launcher discovers at boot
(emulators_scan_cores() / gnw_core_probe() in Core/Src/retro-go/rg_emulators.c).

File layout produced (all integers little-endian):

    offset 0   "CORE" magic (4 bytes, no NUL)
    offset 4   header_version  u16  == GNW_CORE_META_VERSION
    offset 6   header_length   u16  == sizeof(gnw_core_meta_t) + pad_logo_size + header_logo_size
    offset 8   gnw_core_meta_t (see Core/Inc/retro-go/gnw_core_meta.h)
    ...        pad logo blob   (retro_logo_image: u16 width, u16 height, packed 1bpp rows)
    ...        header logo blob (same shape)
    8+header_length  payload (code+data, exactly meta.code_size bytes — the
                     core's own linker script marks .bss NOLOAD so the ELF's
                     flat binary IS exactly this length; the firmware zeroes
                     meta.bss_size bytes right after loading it into RAM_EMU)

Usage (see cores/wsv/Makefile for the concrete invocation):

    tools/pack_core.py \\
        --elf build/wsv_core.elf --bin build/wsv_core.bin \\
        --system-name "Watara Supervision" --dirname wsv \\
        --extensions "wsv sv bin lzma" \\
        --pad-logo-c ../../Core/Src/retro-go/rg_logos.c:pad_wsv \\
        --header-logo-c ../../Core/Src/retro-go/rg_logos.c:header_wsv \\
        --out ../wsv.bin
"""
import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

CORE_HEADER_MAGIC = b"CORE"
CORE_HEADER_MIN_SIZE = 8
GNW_CORE_META_VERSION = 2

# Must mirror gnw_core_meta_t exactly (Core/Inc/retro-go/gnw_core_meta.h):
# char[32] system_name, char[16] dirname, char[32] extensions,
# 9x uint32_t, uint8_t[32] reserved. Verified against the real struct via
# offsetof() on the host compiler (stdint.h types only, no ABI ambiguity).
META_STRUCT_FORMAT = "<32s16s32s9I32s"
META_STRUCT_SIZE = struct.calcsize(META_STRUCT_FORMAT)
assert META_STRUCT_SIZE == 148, META_STRUCT_SIZE


def run_nm(nm_tool, elf_path):
    """Returns {symbol_name: address} for every symbol nm reports."""
    out = subprocess.run([nm_tool, str(elf_path)], check=True,
                          capture_output=True, text=True).stdout
    symbols = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        addr_str, _type, name = parts[0], parts[1], parts[2]
        try:
            symbols[name] = int(addr_str, 16)
        except ValueError:
            continue
    return symbols


def read_u32_at(payload, offset):
    return struct.unpack_from("<I", payload, offset)[0]


def extract_logo_from_c(spec):
    """spec is 'path/to/rg_logos.c:varname'. Parses a
    `const retro_logo_image <varname> LOGO_DATA = { width, height, { 0x.., ... } };`
    declaration (see tools/png_to_logo.py for the writer side) and returns
    the packed (width, height, bytes) retro_logo_image payload — i.e. the
    exact same bytes already compiled into the firmware for this logo, so a
    core migrated to the dynamic model keeps a pixel-identical tab icon."""
    path_str, _, varname = spec.rpartition(":")
    if not path_str or not varname:
        raise ValueError(f"expected PATH:VARNAME, got {spec!r}")
    text = Path(path_str).read_text()
    pattern = re.compile(
        re.escape(varname) + r"\s+LOGO_DATA\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*\{(.*?)\}\s*,\s*\}\s*;",
        re.DOTALL,
    )
    m = pattern.search(text)
    if not m:
        raise ValueError(f"could not find 'const retro_logo_image {varname} LOGO_DATA = ...' in {path_str}")
    width, height = int(m.group(1)), int(m.group(2))
    hex_bytes = re.findall(r"0x[0-9a-fA-F]{1,2}", m.group(3))
    data = bytes(int(h, 16) for h in hex_bytes)
    expected_len = ((width + 7) // 8) * height
    if len(data) != expected_len:
        raise ValueError(
            f"{varname}: parsed {len(data)} logo bytes, expected {expected_len} "
            f"for {width}x{height} (padded-width-to-8 1bpp)"
        )
    return struct.pack("<HH", width, height) + data


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", required=True, type=Path, help="linked core ELF (for symbol addresses)")
    ap.add_argument("--bin", required=True, type=Path,
                     help="objcopy -O binary of --elf (the raw payload, .bss NOLOAD so this is exactly code_size bytes)")
    ap.add_argument("--system-name", required=True, help='e.g. "Watara Supervision"')
    ap.add_argument("--dirname", required=True, help='ROM subdirectory under /roms, e.g. "wsv"')
    ap.add_argument("--extensions", required=True, help='space-separated, e.g. "wsv sv bin lzma"')
    ap.add_argument("--pad-logo-c", help="PATH:VARNAME to extract the pad (controller) logo from a rg_logos.c-style C source")
    ap.add_argument("--header-logo-c", help="PATH:VARNAME to extract the header (console) logo similarly")
    ap.add_argument("--flags", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--nm", default="arm-none-eabi-nm", help="nm tool to use (default: %(default)s)")
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    if len(args.system_name.encode()) >= 32:
        sys.exit(f"error: --system-name too long (max 31 bytes): {args.system_name!r}")
    if len(args.dirname.encode()) >= 16:
        sys.exit(f"error: --dirname too long (max 15 bytes): {args.dirname!r}")
    if len(args.extensions.encode()) >= 32:
        sys.exit(f"error: --extensions too long (max 31 bytes): {args.extensions!r}")

    symbols = run_nm(args.nm, args.elf)

    def sym(name):
        if name not in symbols:
            sys.exit(f"error: symbol '{name}' not found in {args.elf} — is core_ram_emu.ld / gw_core_bridge.c out of date?")
        return symbols[name]

    ram_emu_start = sym("__RAM_EMU_START__")
    code_end = sym("__CORE_CODE_END__")
    bss_end = sym("__CORE_BSS_END__")
    code_size = code_end - ram_emu_start
    bss_size = bss_end - code_end

    payload = args.bin.read_bytes()
    if len(payload) != code_size:
        sys.exit(f"error: {args.bin} is {len(payload)} bytes, expected code_size={code_size} "
                 f"(from __CORE_CODE_END__ - __RAM_EMU_START__) — is .bss really NOLOAD in the linker script?")

    # Read the ABI version/size the core was actually compiled against
    # straight out of its own payload bytes — see gw_core_bridge.c.
    abi_version_off = sym("GW_CORE_BUILT_ABI_VERSION") - ram_emu_start
    abi_size_off = sym("GW_CORE_BUILT_ABI_SIZE") - ram_emu_start
    required_abi_version = read_u32_at(payload, abi_version_off)
    required_abi_min_size = read_u32_at(payload, abi_size_off)

    pad_logo = extract_logo_from_c(args.pad_logo_c) if args.pad_logo_c else b""
    header_logo = extract_logo_from_c(args.header_logo_c) if args.header_logo_c else b""

    pad_logo_offset = CORE_HEADER_MIN_SIZE + META_STRUCT_SIZE
    header_logo_offset = pad_logo_offset + len(pad_logo)
    header_length = META_STRUCT_SIZE + len(pad_logo) + len(header_logo)

    meta = struct.pack(
        META_STRUCT_FORMAT,
        args.system_name.encode(), args.dirname.encode(), args.extensions.encode(),
        required_abi_version, required_abi_min_size,
        code_size, bss_size,
        pad_logo_offset, len(pad_logo),
        header_logo_offset, len(header_logo),
        args.flags,
        b"\x00" * 32,
    )

    out_bytes = (
        CORE_HEADER_MAGIC
        + struct.pack("<HH", GNW_CORE_META_VERSION, header_length)
        + meta
        + pad_logo
        + header_logo
        + payload
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out_bytes)

    print(f"pack_core: {args.out} ({len(out_bytes)} bytes)")
    print(f"  system_name={args.system_name!r} dirname={args.dirname!r} extensions={args.extensions!r}")
    print(f"  required_abi_version={required_abi_version} required_abi_min_size={required_abi_min_size}")
    print(f"  code_size={code_size} bss_size={bss_size} (RAM_EMU budget used: {code_size + bss_size} bytes)")
    print(f"  pad_logo={len(pad_logo)}B header_logo={len(header_logo)}B header_length={header_length}")


if __name__ == "__main__":
    main()
