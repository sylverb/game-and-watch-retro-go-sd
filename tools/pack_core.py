#!/usr/bin/env python3
"""
Package a standalone "core" build (see cores/_template/, cores/wsv/,
cores/pce/) into the CORE-header .bin format the launcher discovers at boot
(emulators_scan_cores() / gnw_core_probe() in Core/Src/retro-go/rg_emulators.c).

File layout produced (all integers little-endian):

    offset 0   "CORE" magic (4 bytes, no NUL)
    offset 4   header_version  u16  == GNW_CORE_META_VERSION
    offset 6   header_length   u16  == sizeof(gnw_core_meta_t) + sum(logo sizes)
    offset 8   gnw_core_meta_t (see Core/Inc/retro-go/gnw_core_meta.h) —
               segments[] describe N independently-loaded code+bss blobs
               (segment 0 is always RAM_EMU and carries the entry
               trampoline), systems[] describe N launcher tabs sharing this
               one core (e.g. PC Engine + PC Engine CD from one pce.bin)
    ...        pad/header logo blobs, one pair per system that has them
               (retro_logo_image: u16 width, u16 height, packed 1bpp rows),
               in systems[] order
    8+header_length  payload: segments[0].code_size bytes, then
                     segments[1].code_size bytes, ... back to back (each
                     segment's own linker script marks .bss NOLOAD so its
                     flat binary is exactly that segment's code_size; the
                     firmware zeroes each segment's bss_size bytes right
                     after loading it into that segment's fixed region)

Usage — single-system, single-segment core (see cores/wsv/Makefile):

    tools/pack_core.py \\
        --elf build/wsv_core.elf --bin build/wsv_core.bin \\
        --system-name "Watara Supervision" --dirname wsv \\
        --extensions "wsv sv bin lzma" \\
        --pad-logo-c ../../Core/Src/retro-go/rg_logos.c:pad_wsv \\
        --header-logo-c ../../Core/Src/retro-go/rg_logos.c:header_wsv \\
        --out ../wsv.bin

Usage — multi-system, multi-segment core (see cores/pce/Makefile):

    tools/pack_core.py \\
        --elf build/pce_core.elf --bin build/pce_core.bin \\
        --system name="PC Engine",dirname=pce,ext="pce lzma",parse=rom,\\
pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_pce,\\
header_logo=../../Core/Src/retro-go/rg_logos.c:header_pce \\
        --system name="PC Engine CD",dirname=pcecd,ext=cue,parse=cdrom,\\
pad_logo=../../Core/Src/retro-go/rg_logos.c:pad_pce,\\
header_logo=../../Core/Src/retro-go/rg_logos.c:header_pcecd \\
        --segment itcm:__ITCM_CORE_START__:__CORE_ITCM_CODE_END__:__CORE_ITCM_BSS_END__:build/pce_core_itcm.bin \\
        --out ../pce.bin

--system/--segment are repeatable (up to GNW_CORE_MAX_SYSTEMS=4 /
GNW_CORE_MAX_SEGMENTS=4, segment 0 already implied by --elf/--bin so
--segment only covers segments 1..3). The legacy --system-name/--dirname/
--extensions/--pad-logo-c/--header-logo-c flags remain as sugar for "one
system, parse=rom" so cores/wsv/Makefile and cores/md/Makefile need no
changes. --system and the legacy flags are mutually exclusive.
"""
import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

CORE_HEADER_MAGIC = b"CORE"
CORE_HEADER_MIN_SIZE = 8
GNW_CORE_META_VERSION = 3

GNW_CORE_MAX_SEGMENTS = 4
GNW_CORE_MAX_SYSTEMS = 4

REGION_NAME_TO_ID = {"ram_emu": 0, "itcm": 1, "ahb": 2, "dtcm": 3}
PARSE_NAME_TO_ID = {"rom": 0, "cdrom": 1}

# Must mirror gnw_core_segment_t exactly (Core/Inc/retro-go/gnw_core_meta.h):
# 3x uint32_t (region, code_size, bss_size).
SEGMENT_STRUCT_FORMAT = "<III"
SEGMENT_STRUCT_SIZE = struct.calcsize(SEGMENT_STRUCT_FORMAT)
assert SEGMENT_STRUCT_SIZE == 12, SEGMENT_STRUCT_SIZE

# Must mirror gnw_core_system_t exactly: char[32] system_name, char[16]
# dirname, char[32] extensions, 5x uint32_t, uint8_t[16] reserved.
SYSTEM_STRUCT_FORMAT = "<32s16s32sIIIII16s"
SYSTEM_STRUCT_SIZE = struct.calcsize(SYSTEM_STRUCT_FORMAT)
assert SYSTEM_STRUCT_SIZE == 116, SYSTEM_STRUCT_SIZE

# Must mirror gnw_core_meta_t exactly: 4x uint32_t (required_abi_version,
# required_abi_min_size, flags, segments_count), segments[4], uint32_t
# systems_count, systems[4], uint8_t[32] reserved.
META_STRUCT_SIZE = (4 * 4 + GNW_CORE_MAX_SEGMENTS * SEGMENT_STRUCT_SIZE
                     + 4 + GNW_CORE_MAX_SYSTEMS * SYSTEM_STRUCT_SIZE + 32)
assert META_STRUCT_SIZE == 564, META_STRUCT_SIZE


class SystemSpec:
    def __init__(self, name, dirname, extensions, parse_type, pad_logo_c=None, header_logo_c=None):
        self.name = name
        self.dirname = dirname
        self.extensions = extensions
        self.parse_type = parse_type
        self.pad_logo_c = pad_logo_c
        self.header_logo_c = header_logo_c

    def validate(self):
        if len(self.name.encode()) >= 32:
            sys.exit(f"error: system name too long (max 31 bytes): {self.name!r}")
        if len(self.dirname.encode()) >= 16:
            sys.exit(f"error: dirname too long (max 15 bytes): {self.dirname!r}")
        if len(self.extensions.encode()) >= 32:
            sys.exit(f"error: extensions too long (max 31 bytes): {self.extensions!r}")


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
    core migrated to the dynamic model keeps a pixel-identical tab icon.

    A few logos (e.g. header_gen, the Sega Genesis header) wrap their
    width/height/bytes triple in a `#if INCLUDED_xx_xx == 1 ... #else ...
    #endif` locale variant (a different bitmap is baked in when a CJK font
    able to render it is compiled into the firmware). Since a standalone
    core has no compile-time knowledge of which languages the *firmware*
    it will run against was built with (same simplification already made
    for all of a dynamic core's own UI strings — see main_wsv.c/
    main_gwenesis.c, hardcoded English), we always take the `#else`
    (default/international) branch here."""
    path_str, _, varname = spec.rpartition(":")
    if not path_str or not varname:
        raise ValueError(f"expected PATH:VARNAME, got {spec!r}")
    text = Path(path_str).read_text()

    decl = re.search(re.escape(varname) + r"\s+LOGO_DATA\s*=\s*\{", text)
    if not decl:
        raise ValueError(f"could not find 'const retro_logo_image {varname} LOGO_DATA = ...' in {path_str}")

    # Balanced-brace scan for the matching closing '}' of this initializer
    # (the byte array itself is a nested { ... }, so a non-greedy regex
    # can't tell an inner close-brace from the outer one).
    depth = 1
    i = decl.end()
    while depth > 0:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        i += 1
    block = text[decl.end():i - 1]

    if "#if" in block:
        branch = re.search(r"#else(.*?)#endif", block, re.DOTALL)
        if not branch:
            raise ValueError(f"{varname}: found '#if' with no '#else' branch to fall back to in {path_str}")
        block = branch.group(1)

    # Some logos (e.g. pad_pce) keep an old/alternate bitmap around as a
    # /* ... */-commented-out block of the same 0x.. array literal, purely
    # for human reference — strip block comments before scanning for hex
    # bytes so they aren't double-counted (the `//` ASCII-art comments on
    # each row are harmless: they never contain a "0x" substring).
    block = re.sub(r"/\*.*?\*/", "", block, flags=re.DOTALL)

    fields = re.match(r"\s*(\d+)\s*,\s*(\d+)\s*,\s*\{(.*?)\}\s*,?\s*$", block, re.DOTALL)
    if not fields:
        raise ValueError(f"{varname}: could not parse width/height/bytes in {path_str}")
    width, height = int(fields.group(1)), int(fields.group(2))
    hex_bytes = re.findall(r"0x[0-9a-fA-F]{1,2}", fields.group(3))
    data = bytes(int(h, 16) for h in hex_bytes)
    expected_len = ((width + 7) // 8) * height
    if len(data) != expected_len:
        raise ValueError(
            f"{varname}: parsed {len(data)} logo bytes, expected {expected_len} "
            f"for {width}x{height} (padded-width-to-8 1bpp)"
        )
    return struct.pack("<HH", width, height) + data


def parse_system_arg(spec):
    """Parses one --system 'name=...,dirname=...,ext=...,parse=rom|cdrom,
    pad_logo=PATH:VAR,header_logo=PATH:VAR' argument. pad_logo/header_logo
    are optional; unrecognized keys are rejected to catch typos early."""
    fields = {}
    for token in spec.split(","):
        if "=" not in token:
            sys.exit(f"error: --system token {token!r} is not KEY=VALUE (full spec: {spec!r})")
        key, _, value = token.partition("=")
        fields[key.strip()] = value.strip()

    unknown = set(fields) - {"name", "dirname", "ext", "parse", "pad_logo", "header_logo"}
    if unknown:
        sys.exit(f"error: --system has unknown key(s) {sorted(unknown)} (spec: {spec!r})")
    for required in ("name", "dirname", "ext", "parse"):
        if required not in fields:
            sys.exit(f"error: --system missing required key '{required}' (spec: {spec!r})")

    parse_type = PARSE_NAME_TO_ID.get(fields["parse"])
    if parse_type is None:
        sys.exit(f"error: --system parse={fields['parse']!r} must be one of {sorted(PARSE_NAME_TO_ID)}")

    return SystemSpec(fields["name"], fields["dirname"], fields["ext"], parse_type,
                       fields.get("pad_logo"), fields.get("header_logo"))


def parse_segment_arg(spec):
    """Parses one --segment 'region:start_symbol:code_end_symbol:bss_end_symbol:bin_file' argument."""
    parts = spec.split(":", 4)
    if len(parts) != 5:
        sys.exit(f"error: --segment must be region:start_symbol:code_end_symbol:bss_end_symbol:bin_file, got {spec!r}")
    region_name, start_symbol, code_end_symbol, bss_end_symbol, bin_file = parts
    region = REGION_NAME_TO_ID.get(region_name)
    if region is None:
        sys.exit(f"error: --segment region {region_name!r} must be one of {sorted(REGION_NAME_TO_ID)}")
    return region, start_symbol, code_end_symbol, bss_end_symbol, Path(bin_file)


def pack_segment(region, code_size, bss_size):
    return struct.pack(SEGMENT_STRUCT_FORMAT, region, code_size, bss_size)


def pack_system(spec, pad_logo_offset, pad_logo_size, header_logo_offset, header_logo_size):
    return struct.pack(
        SYSTEM_STRUCT_FORMAT,
        spec.name.encode(), spec.dirname.encode(), spec.extensions.encode(),
        spec.parse_type,
        pad_logo_offset, pad_logo_size,
        header_logo_offset, header_logo_size,
        b"\x00" * 16,
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--elf", required=True, type=Path, help="linked core ELF (for symbol addresses)")
    ap.add_argument("--bin", required=True, type=Path,
                     help="objcopy -O binary of --elf's segment-0 (RAM_EMU) sections — exactly segments[0].code_size bytes")

    # Legacy single-system sugar (kept so cores/wsv, cores/md need no changes).
    ap.add_argument("--system-name", help='e.g. "Watara Supervision" (single-system sugar for --system)')
    ap.add_argument("--dirname", help='ROM subdirectory under /roms, e.g. "wsv" (single-system sugar)')
    ap.add_argument("--extensions", help='space-separated, e.g. "wsv sv bin lzma" (single-system sugar)')
    ap.add_argument("--pad-logo-c", help="PATH:VARNAME to extract the pad (controller) logo (single-system sugar)")
    ap.add_argument("--header-logo-c", help="PATH:VARNAME to extract the header (console) logo (single-system sugar)")

    # v3 multi-system / multi-segment flags.
    ap.add_argument("--system", action="append", default=[],
                     help="repeatable: name=...,dirname=...,ext=...,parse=rom|cdrom[,pad_logo=PATH:VAR][,header_logo=PATH:VAR]")
    ap.add_argument("--segment", action="append", default=[],
                     help="repeatable: region:start_symbol:code_end_symbol:bss_end_symbol:bin_file (segments 1..3; segment 0 is --elf/--bin)")

    ap.add_argument("--flags", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--nm", default="arm-none-eabi-nm", help="nm tool to use (default: %(default)s)")
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    legacy_used = any([args.system_name, args.dirname, args.extensions, args.pad_logo_c, args.header_logo_c])
    if legacy_used and args.system:
        sys.exit("error: --system-name/--dirname/--extensions/--pad-logo-c/--header-logo-c "
                  "are mutually exclusive with --system")

    if args.system:
        systems = [parse_system_arg(s) for s in args.system]
    else:
        if not (args.system_name and args.dirname and args.extensions):
            sys.exit("error: need either --system (repeatable) or --system-name/--dirname/--extensions")
        systems = [SystemSpec(args.system_name, args.dirname, args.extensions, PARSE_NAME_TO_ID["rom"],
                               args.pad_logo_c, args.header_logo_c)]

    if len(systems) > GNW_CORE_MAX_SYSTEMS:
        sys.exit(f"error: {len(systems)} systems given, max is {GNW_CORE_MAX_SYSTEMS}")
    for s in systems:
        s.validate()

    extra_segments = [parse_segment_arg(s) for s in args.segment]
    if 1 + len(extra_segments) > GNW_CORE_MAX_SEGMENTS:
        sys.exit(f"error: {1 + len(extra_segments)} segments given, max is {GNW_CORE_MAX_SEGMENTS}")

    symbols = run_nm(args.nm, args.elf)

    def sym(name):
        if name not in symbols:
            sys.exit(f"error: symbol '{name}' not found in {args.elf} — is the linker script / gw_core_bridge.c out of date?")
        return symbols[name]

    # --- Segment 0 (always RAM_EMU, carries the entry trampoline) ---
    ram_emu_start = sym("__RAM_EMU_START__")
    code_end = sym("__CORE_CODE_END__")
    bss_end = sym("__CORE_BSS_END__")
    seg0_code_size = code_end - ram_emu_start
    seg0_bss_size = bss_end - code_end

    seg0_payload = args.bin.read_bytes()
    if len(seg0_payload) != seg0_code_size:
        sys.exit(f"error: {args.bin} is {len(seg0_payload)} bytes, expected code_size={seg0_code_size} "
                 f"(from __CORE_CODE_END__ - __RAM_EMU_START__) — is .bss really NOLOAD in the linker script?")

    # Read the ABI version/size the core was actually compiled against
    # straight out of its own payload bytes — see gw_core_bridge.c.
    abi_version_off = sym("GW_CORE_BUILT_ABI_VERSION") - ram_emu_start
    abi_size_off = sym("GW_CORE_BUILT_ABI_SIZE") - ram_emu_start
    required_abi_version = read_u32_at(seg0_payload, abi_version_off)
    required_abi_min_size = read_u32_at(seg0_payload, abi_size_off)

    segments = [(REGION_NAME_TO_ID["ram_emu"], seg0_code_size, seg0_bss_size)]
    payloads = [seg0_payload]

    # --- Extra segments (ITCM/AHB), each its own start/code_end/bss_end symbol triple ---
    for region, start_symbol, code_end_symbol, bss_end_symbol, bin_file in extra_segments:
        seg_start = sym(start_symbol)
        seg_code_end = sym(code_end_symbol)
        seg_bss_end = sym(bss_end_symbol)
        seg_code_size = seg_code_end - seg_start
        seg_bss_size = seg_bss_end - seg_code_end

        seg_payload = bin_file.read_bytes()
        if len(seg_payload) != seg_code_size:
            sys.exit(f"error: {bin_file} is {len(seg_payload)} bytes, expected code_size={seg_code_size} "
                     f"(from {code_end_symbol} - {start_symbol})")

        segments.append((region, seg_code_size, seg_bss_size))
        payloads.append(seg_payload)

    # --- Logos: extracted per-system, laid out back to back right after the meta struct ---
    logo_offset = CORE_HEADER_MIN_SIZE + META_STRUCT_SIZE
    logo_blobs = []
    system_logo_info = []  # (pad_logo_offset, pad_logo_size, header_logo_offset, header_logo_size)
    for s in systems:
        pad_logo = extract_logo_from_c(s.pad_logo_c) if s.pad_logo_c else b""
        header_logo = extract_logo_from_c(s.header_logo_c) if s.header_logo_c else b""

        pad_logo_offset = logo_offset if pad_logo else 0
        logo_offset += len(pad_logo)
        header_logo_offset = logo_offset if header_logo else 0
        logo_offset += len(header_logo)

        logo_blobs.append(pad_logo)
        logo_blobs.append(header_logo)
        system_logo_info.append((pad_logo_offset, len(pad_logo), header_logo_offset, len(header_logo)))

    header_length = META_STRUCT_SIZE + sum(len(b) for b in logo_blobs)

    # --- Assemble gnw_core_meta_t ---
    meta_bytes = struct.pack("<IIII", required_abi_version, required_abi_min_size, args.flags, len(segments))
    for i in range(GNW_CORE_MAX_SEGMENTS):
        if i < len(segments):
            meta_bytes += pack_segment(*segments[i])
        else:
            meta_bytes += b"\x00" * SEGMENT_STRUCT_SIZE

    meta_bytes += struct.pack("<I", len(systems))
    for i in range(GNW_CORE_MAX_SYSTEMS):
        if i < len(systems):
            meta_bytes += pack_system(systems[i], *system_logo_info[i])
        else:
            meta_bytes += b"\x00" * SYSTEM_STRUCT_SIZE
    meta_bytes += b"\x00" * 32  # reserved

    assert len(meta_bytes) == META_STRUCT_SIZE, len(meta_bytes)

    out_bytes = (
        CORE_HEADER_MAGIC
        + struct.pack("<HH", GNW_CORE_META_VERSION, header_length)
        + meta_bytes
        + b"".join(logo_blobs)
        + b"".join(payloads)
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out_bytes)

    print(f"pack_core: {args.out} ({len(out_bytes)} bytes)")
    print(f"  required_abi_version={required_abi_version} required_abi_min_size={required_abi_min_size}")
    for i, s in enumerate(systems):
        print(f"  system[{i}]: name={s.name!r} dirname={s.dirname!r} extensions={s.extensions!r} parse_type={s.parse_type}")
    for i, (region, code_size, bss_size) in enumerate(segments):
        region_name = next(n for n, v in REGION_NAME_TO_ID.items() if v == region)
        print(f"  segment[{i}]: region={region_name} code_size={code_size}B bss_size={bss_size}B")
    print(f"  header_length={header_length}")


if __name__ == "__main__":
    main()
