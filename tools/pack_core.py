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

Logos (pad = controller icon in the footer, header = console name glyph)
can come from either:

  - PNG/BMP/GIF/JPEG via --pad-logo / --header-logo (or pad_logo=img.png
    inside --system). Converted to packed 1bpp retro_logo_image (same
    rules as tools/png_to_logo.py). Requires Pillow.
  - Existing C arrays via --pad-logo-c / --header-logo-c (or
    pad_logo=.../rg_logos.c:varname) for pixel-identical migration from
    the firmware's baked-in rg_logos.c.

Usage — image logos (template / new cores):

    tools/pack_core.py \
        --elf build/example_core.elf --bin build/example_core.bin \
        --system-name "Example Core" --dirname example \
        --extensions "bin" \
        --pad-logo assets/pad.bmp \
        --header-logo assets/header.bmp \
        --out example.bin

Usage — single-system, single-segment core (see cores/wsv/Makefile):

    tools/pack_core.py \\
        --elf build/wsv_core.elf --bin build/wsv_core.bin \\
        --system-name "Watara Supervision" --dirname wsv \\
        --extensions "wsv sv bin lzma" \\
        --pad-logo assets/pad.bmp \\
        --header-logo assets/header.bmp \\
        --version 1.0.0 \\
        --out ../wsv.bin

`--version X.Y.Z` (optional leading `v`, default 1.0.0) and `--core-name`
(default: --out stem) are stored in `gnw_core_meta_t` and shown in the
in-game pause → Info dialog (name, version, path, file date).

Usage — multi-system, multi-segment core (see cores/pce/Makefile):

    tools/pack_core.py \\
        --elf build/pce_core.elf --bin build/pce_core.bin \\
        --system name="PC Engine",dirname=pce,ext=pce,parse=rom,\\
pad_logo=assets/pad.bmp,header_logo=assets/header.bmp,cheat_ext=pceplus \\
        --system name="PC Engine CD",dirname=pcecd,ext=cue,parse=cdrom,\\
pad_logo=assets/pad.bmp,header_logo=assets/header_cd.bmp,cheat_ext=pceplus \\
        --segment itcm:__ITCM_CORE_START__:__CORE_ITCM_CODE_END__:__CORE_ITCM_BSS_END__:build/pce_core_itcm.bin \\
        --out ../pce.bin

Extra ITCM segments are auto-detected from ELF symbols
(__CORE_ITCM_*, section .core_itcm) when present; pass
--no-auto-segments to disable. Explicit --segment still wins for a
given region. AHB/DTCM are not valid load targets (firmware heap /
dtcm bump) — pack_core rejects them.

--system/--segment are repeatable (up to GNW_CORE_MAX_SYSTEMS=4 /
GNW_CORE_MAX_SEGMENTS=4, segment 0 already implied by --elf/--bin so
--segment only covers segments 1..3). The legacy --system-name/--dirname/
--extensions/--pad-logo/--pad-logo-c/--header-logo/--header-logo-c flags
remain as sugar for "one system, parse=rom". --system and the legacy
flags are mutually exclusive. Image vs C logo specs are auto-detected
(see resolve_logo()).
"""
import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

IMAGE_LOGO_EXTENSIONS = {".png", ".bmp", ".gif", ".jpg", ".jpeg"}

CORE_HEADER_MAGIC = b"CORE"
CORE_HEADER_MIN_SIZE = 8
GNW_CORE_META_VERSION = 3

GNW_CORE_MAX_SEGMENTS = 4
GNW_CORE_MAX_SYSTEMS = 4

REGION_NAME_TO_ID = {"ram_emu": 0, "itcm": 1}
PARSE_NAME_TO_ID = {"rom": 0, "cdrom": 1}

# Must mirror gnw_core_segment_t exactly (Core/Inc/retro-go/gnw_core_meta.h):
# 3x uint32_t (region, code_size, bss_size).
SEGMENT_STRUCT_FORMAT = "<III"
SEGMENT_STRUCT_SIZE = struct.calcsize(SEGMENT_STRUCT_FORMAT)
assert SEGMENT_STRUCT_SIZE == 12, SEGMENT_STRUCT_SIZE

# Must mirror gnw_core_system_t exactly: char[32] system_name, char[16]
# dirname, char[32] extensions, 5x uint32_t, cheat_ext[8], reserved[8].
SYSTEM_STRUCT_FORMAT = "<32s16s32sIIIII8s8s"
SYSTEM_STRUCT_SIZE = struct.calcsize(SYSTEM_STRUCT_FORMAT)
assert SYSTEM_STRUCT_SIZE == 116, SYSTEM_STRUCT_SIZE

# Must mirror gnw_core_meta_t exactly: 4x uint32_t (required_abi_version,
# required_abi_min_size, flags, segments_count), segments[4], uint32_t
# systems_count, systems[4], version_major/minor/patch (3 bytes),
# core_name[24], uint8_t[5] reserved.
META_STRUCT_SIZE = (4 * 4 + GNW_CORE_MAX_SEGMENTS * SEGMENT_STRUCT_SIZE
                     + 4 + GNW_CORE_MAX_SYSTEMS * SYSTEM_STRUCT_SIZE
                     + 3 + 24 + 5)
assert META_STRUCT_SIZE == 564, META_STRUCT_SIZE
CORE_NAME_MAX = 23  # stored as char[24] including NUL


def parse_version(spec):
    """Parse 'X.Y.Z' (optional leading 'v') into (major, minor, patch),
    each 0..255. Used for gnw_core_meta_t.version_*. """
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


class SystemSpec:
    def __init__(self, name, dirname, extensions, parse_type, pad_logo=None, header_logo=None,
                 cheat_ext=""):
        self.name = name
        self.dirname = dirname
        self.extensions = extensions
        self.parse_type = parse_type
        # Image path (*.png/...), C extract PATH:VAR, or None.
        self.pad_logo = pad_logo
        self.header_logo = header_logo
        # Cheat file extension under /cheats/ (no leading '.'), or "".
        self.cheat_ext = (cheat_ext or "").lstrip(".")

    def validate(self):
        if len(self.name.encode()) >= 32:
            sys.exit(f"error: system name too long (max 31 bytes): {self.name!r}")
        if len(self.dirname.encode()) >= 16:
            sys.exit(f"error: dirname too long (max 15 bytes): {self.dirname!r}")
        if len(self.extensions.encode()) >= 32:
            sys.exit(f"error: extensions too long (max 31 bytes): {self.extensions!r}")
        if len(self.cheat_ext.encode()) >= 8:
            sys.exit(f"error: cheat_ext too long (max 7 bytes): {self.cheat_ext!r}")


def logo_from_image(path, *, invert=False, target_width=None, target_height=None):
    """Convert PNG/BMP(/GIF/JPEG) to packed retro_logo_image bytes.

    Same thresholding / width-pad-to-8 / MSB-first packing as tools/png_to_logo.py
    so assets authored for that tool drop straight into a core .bin.
    """
    try:
        from PIL import Image, ImageOps
    except ImportError as e:
        sys.exit(
            "error: converting logos from images requires Pillow "
            f"(pip install pillow) — while loading {path}: {e}"
        )

    path = Path(path)
    if not path.is_file():
        sys.exit(f"error: logo image not found: {path}")

    img = Image.open(path)

    if target_height is not None or target_width is not None:
        original_width, original_height = img.size
        aspect_ratio = original_width / original_height if original_height else 1.0
        if target_width is not None and target_height is not None:
            new_width, new_height = target_width, target_height
        elif target_width is not None:
            new_width = target_width
            new_height = max(1, int(target_width / aspect_ratio))
        else:
            new_height = target_height
            new_width = max(1, int(target_height * aspect_ratio))
        img = img.resize((new_width, new_height), Image.Resampling.NEAREST)

    if img.mode != "RGBA":
        img = img.convert("RGBA")

    result = Image.new("1", img.size, 0)
    for x in range(img.width):
        for y in range(img.height):
            r, g, b, a = img.getpixel((x, y))
            if a == 0:
                continue
            if (r + g + b) < 384:
                result.putpixel((x, y), 255)

    if invert:
        result = ImageOps.invert(result)

    width, height = result.size
    padded_width = ((width + 7) // 8) * 8
    if padded_width != width:
        padded = Image.new("1", (padded_width, height), 0)
        padded.paste(result, (0, 0))
        result = padded
        width = padded_width

    pixels = [0 if p == 0 else 1 for p in result.getdata()]
    byte_data = bytearray()
    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for bit in range(8):
                if x + bit < width and pixels[y * width + (x + bit)]:
                    byte |= 1 << (7 - bit)
            byte_data.append(byte)

    return struct.pack("<HH", width, height) + bytes(byte_data)


def looks_like_c_logo_spec(spec):
    """True for 'path/to/file.c:varname' (existing rg_logos.c extracts)."""
    if ":" not in spec:
        return False
    path_str, _, varname = spec.rpartition(":")
    if not path_str or not varname:
        return False
    suffix = Path(path_str).suffix.lower()
    return suffix in {".c", ".h", ".cpp", ".cc"} or Path(path_str).name == "rg_logos.c"


def resolve_logo(spec, *, invert=False, target_width=None, target_height=None):
    """Resolve a logo argument to packed retro_logo_image bytes (or b'')."""
    if not spec:
        return b""
    if looks_like_c_logo_spec(spec):
        return extract_logo_from_c(spec)
    path = Path(spec)
    if path.suffix.lower() in IMAGE_LOGO_EXTENSIONS:
        return logo_from_image(path, invert=invert, target_width=target_width,
                               target_height=target_height)
    if ":" in spec:
        return extract_logo_from_c(spec)
    sys.exit(
        f"error: logo {spec!r} must be an image "
        f"({', '.join(sorted(IMAGE_LOGO_EXTENSIONS))}) or PATH:VARNAME into a .c file"
    )


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
    pad_logo=IMG_OR_C,header_logo=IMG_OR_C[,cheat_ext=ggcodes|pceplus|mcf|…]'
    argument. pad_logo/header_logo are optional; each value is an image path
    or PATH:VAR into a .c file (see resolve_logo). pad_logo_c/header_logo_c
    are accepted aliases. cheat_ext empty/absent = no cheat support."""
    fields = {}
    for token in spec.split(","):
        if "=" not in token:
            sys.exit(f"error: --system token {token!r} is not KEY=VALUE (full spec: {spec!r})")
        key, _, value = token.partition("=")
        fields[key.strip()] = value.strip()

    unknown = set(fields) - {"name", "dirname", "ext", "parse", "pad_logo", "header_logo",
                             "pad_logo_c", "header_logo_c", "cheat_ext"}
    if unknown:
        sys.exit(f"error: --system has unknown key(s) {sorted(unknown)} (spec: {spec!r})")
    for required in ("name", "dirname", "ext", "parse"):
        if required not in fields:
            sys.exit(f"error: --system missing required key '{required}' (spec: {spec!r})")

    parse_type = PARSE_NAME_TO_ID.get(fields["parse"])
    if parse_type is None:
        sys.exit(f"error: --system parse={fields['parse']!r} must be one of {sorted(PARSE_NAME_TO_ID)}")

    pad = fields.get("pad_logo") or fields.get("pad_logo_c")
    header = fields.get("header_logo") or fields.get("header_logo_c")
    return SystemSpec(fields["name"], fields["dirname"], fields["ext"], parse_type, pad, header,
                      fields.get("cheat_ext", ""))


def parse_segment_arg(spec):
    """Parses one --segment 'region:start_symbol:code_end_symbol:bss_end_symbol:bin_file' argument."""
    parts = spec.split(":", 4)
    if len(parts) != 5:
        sys.exit(f"error: --segment must be region:start_symbol:code_end_symbol:bss_end_symbol:bin_file, got {spec!r}")
    region_name, start_symbol, code_end_symbol, bss_end_symbol, bin_file = parts
    region = REGION_NAME_TO_ID.get(region_name)
    if region is None:
        sys.exit(
            f"error: --segment region {region_name!r} must be one of "
            f"{sorted(REGION_NAME_TO_ID)} (AHB/DTCM are firmware pools, not load targets)"
        )
    return region, start_symbol, code_end_symbol, bss_end_symbol, Path(bin_file)


# Optional extra segments discovered from ELF symbols when a custom
# linker script defines them (see cores/pce/pce_core.ld, cores/gba/…).
# If the triple is absent, packing is a no-op for that region.
# AHB is intentionally omitted — AHB SRAM is the firmware malloc heap.
AUTO_EXTRA_SEGMENTS = (
    {
        "region": "itcm",
        "start": "__ITCM_CORE_START__",
        "code_end": "__CORE_ITCM_CODE_END__",
        "bss_end": "__CORE_ITCM_BSS_END__",
        "section": ".core_itcm",
    },
)


def objcopy_tool_from_nm(nm_tool):
    """arm-none-eabi-nm → arm-none-eabi-objcopy (and likewise for a path)."""
    nm_tool = str(nm_tool)
    if nm_tool.endswith("nm"):
        return nm_tool[:-2] + "objcopy"
    return "arm-none-eabi-objcopy"


def extract_section_bytes(objcopy, elf_path, section, expected_size):
    """objcopy --only-section into a temp file. Empty segment → b''."""
    if expected_size == 0:
        return b""
    import tempfile
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
        tmp_path = Path(tmp.name)
    try:
        subprocess.run(
            [objcopy, "-O", "binary", f"--only-section={section}", str(elf_path), str(tmp_path)],
            check=True, capture_output=True, text=True,
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


def discover_auto_segments(symbols, elf_path, objcopy):
    """Return [(region_id, start_sym, code_end_sym, bss_end_sym, payload_bytes)]
    for every AUTO_EXTRA_SEGMENTS entry whose linker symbols exist."""
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
                f"(code={code_size}, bss={bss_size}) — check linker script symbols"
            )
        payload = extract_section_bytes(objcopy, elf_path, spec["section"], code_size)
        found.append((region, code_size, bss_size, payload, spec["region"]))
    return found


def pack_segment(region, code_size, bss_size):
    return struct.pack(SEGMENT_STRUCT_FORMAT, region, code_size, bss_size)


def pack_system(spec, pad_logo_offset, pad_logo_size, header_logo_offset, header_logo_size):
    return struct.pack(
        SYSTEM_STRUCT_FORMAT,
        spec.name.encode(), spec.dirname.encode(), spec.extensions.encode(),
        spec.parse_type,
        pad_logo_offset, pad_logo_size,
        header_logo_offset, header_logo_size,
        spec.cheat_ext.encode(),
        b"\x00" * 8,
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
    ap.add_argument("--pad-logo",
                     help="pad (controller) logo image (.png/.bmp/...) (single-system sugar)")
    ap.add_argument("--header-logo",
                     help="header (console) logo image (.png/.bmp/...) (single-system sugar)")
    ap.add_argument("--pad-logo-c",
                     help="PATH:VARNAME to extract the pad logo from a .c file (single-system sugar)")
    ap.add_argument("--header-logo-c",
                     help="PATH:VARNAME to extract the header logo from a .c file (single-system sugar)")
    ap.add_argument("--logo-invert", action="store_true",
                     help="invert colors when converting image logos")
    ap.add_argument("--logo-width", type=int, default=None,
                     help="optional resize width for image logos (keeps aspect if height omitted)")
    ap.add_argument("--logo-height", type=int, default=None,
                     help="optional resize height for image logos (keeps aspect if width omitted)")

    # v3 multi-system / multi-segment flags.
    ap.add_argument("--system", action="append", default=[],
                     help="repeatable: name=...,dirname=...,ext=...,parse=rom|cdrom"
                          "[,pad_logo=IMG_OR_C][,header_logo=IMG_OR_C][,cheat_ext=ggcodes|pceplus|mcf]")
    ap.add_argument("--cheat-ext", default="",
                     help='cheat file extension under /cheats/ (no leading "."), '
                          'e.g. "ggcodes" (single-system sugar for --system cheat_ext=)')
    ap.add_argument("--segment", action="append", default=[],
                     help="repeatable: region:start_symbol:code_end_symbol:bss_end_symbol:bin_file (segments 1..3; segment 0 is --elf/--bin)")

    ap.add_argument("--flags", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--version", default="1.0.0",
                     help="core semantic version X.Y.Z (optional leading 'v'; "
                          "stored as 3 bytes in gnw_core_meta_t, default: %(default)s)")
    ap.add_argument("--core-name", default=None,
                     help="short core pack name stored in gnw_core_meta_t "
                          f"(max {CORE_NAME_MAX} chars). Default: --out stem "
                          "(e.g. sms.bin → 'sms')")
    ap.add_argument("--nm", default="arm-none-eabi-nm", help="nm tool to use (default: %(default)s)")
    ap.add_argument("--objcopy", default=None,
                     help="objcopy tool (default: derived from --nm, e.g. arm-none-eabi-objcopy)")
    ap.add_argument("--no-auto-segments", action="store_true",
                     help="do not auto-detect ITCM segments from ELF symbols "
                          "(only use explicit --segment)")
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    version_major, version_minor, version_patch = parse_version(args.version)

    if args.core_name is not None:
        core_name = args.core_name.strip()
    else:
        core_name = args.out.stem
    if not core_name:
        sys.exit("error: empty --core-name / --out stem")
    if len(core_name.encode()) > CORE_NAME_MAX:
        sys.exit(f"error: --core-name too long (max {CORE_NAME_MAX} bytes): {core_name!r}")

    legacy_used = any([args.system_name, args.dirname, args.extensions,
                       args.pad_logo, args.header_logo, args.pad_logo_c, args.header_logo_c,
                       args.cheat_ext])
    if legacy_used and args.system:
        sys.exit("error: --system-name/--dirname/--extensions/--pad-logo*/--header-logo*/--cheat-ext "
                  "are mutually exclusive with --system")

    if args.system:
        systems = [parse_system_arg(s) for s in args.system]
    else:
        if not (args.system_name and args.dirname and args.extensions):
            sys.exit("error: need either --system (repeatable) or --system-name/--dirname/--extensions")
        if args.pad_logo and args.pad_logo_c:
            sys.exit("error: use only one of --pad-logo / --pad-logo-c")
        if args.header_logo and args.header_logo_c:
            sys.exit("error: use only one of --header-logo / --header-logo-c")
        systems = [SystemSpec(args.system_name, args.dirname, args.extensions, PARSE_NAME_TO_ID["rom"],
                               args.pad_logo or args.pad_logo_c,
                               args.header_logo or args.header_logo_c,
                               args.cheat_ext)]

    if len(systems) > GNW_CORE_MAX_SYSTEMS:
        sys.exit(f"error: {len(systems)} systems given, max is {GNW_CORE_MAX_SYSTEMS}")
    for s in systems:
        s.validate()

    explicit_segments = [parse_segment_arg(s) for s in args.segment]

    symbols = run_nm(args.nm, args.elf)
    objcopy = args.objcopy or objcopy_tool_from_nm(args.nm)

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

    # --- Extra segments: explicit --segment first, then auto-detect ITCM ---
    used_regions = {REGION_NAME_TO_ID["ram_emu"]}
    for region, start_symbol, code_end_symbol, bss_end_symbol, bin_file in explicit_segments:
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
        used_regions.add(region)

    if not args.no_auto_segments:
        for region, code_size, bss_size, payload, region_name in discover_auto_segments(
                symbols, args.elf, objcopy):
            if region in used_regions:
                continue  # explicit --segment already covered this region
            print(f"pack_core: auto segment {region_name} "
                  f"(code={code_size}B bss={bss_size}B)")
            segments.append((region, code_size, bss_size))
            payloads.append(payload)
            used_regions.add(region)

    if len(segments) > GNW_CORE_MAX_SEGMENTS:
        sys.exit(f"error: {len(segments)} segments total, max is {GNW_CORE_MAX_SEGMENTS}")

    # --- Logos: extracted per-system, laid out back to back right after the meta struct ---
    logo_offset = CORE_HEADER_MIN_SIZE + META_STRUCT_SIZE
    logo_blobs = []
    system_logo_info = []  # (pad_logo_offset, pad_logo_size, header_logo_offset, header_logo_size)
    logo_kw = dict(invert=args.logo_invert, target_width=args.logo_width,
                   target_height=args.logo_height)
    for s in systems:
        pad_logo = resolve_logo(s.pad_logo, **logo_kw)
        header_logo = resolve_logo(s.header_logo, **logo_kw)

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
    meta_bytes += struct.pack("<BBB", version_major, version_minor, version_patch)
    meta_bytes += core_name.encode() + b"\x00" * (24 - len(core_name.encode()))
    meta_bytes += b"\x00" * 5  # reserved

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
    print(f"  core_name={core_name!r} version=v{version_major}.{version_minor}.{version_patch}")
    print(f"  required_abi_version={required_abi_version} required_abi_min_size={required_abi_min_size}")
    for i, s in enumerate(systems):
        print(f"  system[{i}]: name={s.name!r} dirname={s.dirname!r} extensions={s.extensions!r} parse_type={s.parse_type}")
    for i, (region, code_size, bss_size) in enumerate(segments):
        region_name = next(n for n, v in REGION_NAME_TO_ID.items() if v == region)
        print(f"  segment[{i}]: region={region_name} code_size={code_size}B bss_size={bss_size}B")
    print(f"  header_length={header_length}")


if __name__ == "__main__":
    main()
