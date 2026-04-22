#!/usr/bin/env python3
"""
gen_unicode_bin.py
==================
Generates Unicode-ordered binary font files for the SD-card font loader
in rg_i18n.c.  No codepages involved: the TTF font is queried directly
by Unicode codepoint.

Two binary formats are used depending on the script:

  FIXED-WIDTH (cjk, hangul, hiragana, katakana, cjk_punct):
    Dense array, each glyph = FONT_HEIGHT x BYTES_PER_ROW bytes (LSB-first).
    Glyph index = codepoint - BASE_CODEPOINT.
    C-side lookup:
      char_offset      = codepoint - BASE;
      char_data_offset = char_offset * FONT_HEIGHT * BYTES_PER_ROW;

  VARIABLE-WIDTH (greek, math_operators, geometric, misc_symbols, fullwidth):
    Header followed by pixel data:
      [0      .. N-1  ] widths[N]    1 byte per glyph (0 = empty)
      [N      .. 3N-1 ] offsets[N]   2 bytes per glyph (uint16 LE),
                                     offset into pixel data section
      [3N     .. EOF  ] pixel data   FONT_HEIGHT rows x ceil(width/8) bytes
    C-side lookup:
      char_offset = codepoint - BASE;
      fseek(file, char_offset, SEEK_SET);
      fread(&width, 1, 1, file);
      fseek(file, N + char_offset * 2, SEEK_SET);
      fread(&data_offset, 2, 1, file);   // uint16 LE
      data_length = FONT_HEIGHT * ((width + 7) / 8);
      fseek(file, 3*N + data_offset, SEEK_SET);
      fread(buf, 1, data_length, file);

Predefined scripts (--script):
  greek          U+0370-U+03FF   variable-width   Greek and Coptic (omega etc.)
  math_operators U+2200-U+22FF   variable-width   Mathematical Operators (inf ~= etc.)
  geometric      U+25A0-U+25FF   variable-width   Geometric Shapes (square triangle etc.)
  misc_symbols   U+2600-U+26FF   variable-width   Miscellaneous Symbols (star etc.)
  cjk_punct      U+3000-U+303F   fixed-width      CJK Symbols and Punctuation
  hiragana       U+3040-U+309F   fixed-width      Hiragana
  katakana       U+30A0-U+30FF   fixed-width      Katakana
  cjk            U+4E00-U+9FFF   fixed-width      CJK Unified Ideographs
  hangul         U+AC00-U+D7A3   fixed-width      Hangul Syllables
  fullwidth      U+FF00-U+FFEF   variable-width   Halfwidth and Fullwidth Forms

Usage:
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --script greek          -o unicode_greek.bin
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --script math_operators -o unicode_math_operators.bin
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --script geometric      -o unicode_geometric.bin
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --script misc_symbols   -o unicode_misc_symbols.bin
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --script cjk_punct      -o unicode_cjk_punct.bin
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --script hiragana       -o unicode_hiragana.bin
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --script katakana       -o unicode_katakana.bin
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --script cjk            -o unicode_cjk.bin
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --script hangul         -o unicode_hangul.bin
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --script fullwidth      -o unicode_fullwidth.bin

  # Generate all scripts at once into a directory
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --all --outdir /path/to/fonts/
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --all  # outputs next to the font file

  # Preview a single codepoint (ASCII art + hex dump)
  python gen_unicode_bin.py NotoSansCJK-Regular.ttc --preview 0x03C9
"""

import argparse
import struct
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# ---------------------------------------------------------------------------
# Layout constants — must match i18n_get_text_height()
# ---------------------------------------------------------------------------

FONT_HEIGHT = 12   # rows per glyph
FIXED_WIDTH = 12   # pixel width for fixed-width scripts
FIXED_BPR   = (FIXED_WIDTH + 7) // 8   # bytes per row = 2
FIXED_BPG   = FONT_HEIGHT * FIXED_BPR  # bytes per glyph = 24

# ---------------------------------------------------------------------------
# Predefined script blocks
# name: (base, end, description, fixed_width)
# ---------------------------------------------------------------------------

SCRIPTS = {
    "greek"          : (0x0370, 0x03FF, "Greek and Coptic",              False),
    "math_operators" : (0x2200, 0x22FF, "Mathematical Operators",        False),
    "geometric"      : (0x25A0, 0x25FF, "Geometric Shapes",              False),
    "misc_symbols"   : (0x2600, 0x26FF, "Miscellaneous Symbols",         False),
    "cjk_punct"      : (0x3000, 0x303F, "CJK Symbols and Punctuation",   True),
    "hiragana"       : (0x3040, 0x309F, "Hiragana",                      True),
    "katakana"       : (0x30A0, 0x30FF, "Katakana",                      True),
    "cjk"            : (0x4E00, 0x9FFF, "CJK Unified Ideographs",        True),
    "hangul"         : (0xAC00, 0xD7A3, "Hangul Syllables",              True),
    "fullwidth"      : (0xFF00, 0xFFEF, "Halfwidth and Fullwidth Forms",  False),
}

# ---------------------------------------------------------------------------
# Rendering helpers
# ---------------------------------------------------------------------------

def render_glyph_fixed(img, draw, font, codepoint, xoffset, yoffset):
    """
    Render at FIXED_WIDTH (12px), return FIXED_BPG bytes (LSB-first).
    Returns (bytes, actual_pixel_width). actual_pixel_width=0 means no glyph.
    """
    char = chr(codepoint)
    try:
        mask = font.getmask(char)
        if mask.size == (0, 0):
            return bytes(FIXED_BPG), 0
    except Exception:
        return bytes(FIXED_BPG), 0

    draw.rectangle((0, 0, 15, 15), fill="#000000")
    draw.fontmode = "1"
    draw.text((xoffset, yoffset), char, font=font, fill=(255, 255, 255))
    pixels = img.load()

    actual_width = 0
    for y in range(FONT_HEIGHT):
        for x in range(15, -1, -1):
            if sum(pixels[x, y]) >= 100:
                actual_width = max(actual_width, x + 1)
                break

    result = bytearray(FIXED_BPG)
    for y in range(FONT_HEIGHT):
        b1 = b2 = 0
        for x in range(8):
            if sum(pixels[x, y]) >= 100:
                b1 |= (0x80 >> x)
            if x < 4 and sum(pixels[x + 8, y]) >= 100:
                b2 |= (0x80 >> x)
        b1 = int(f"{b1:08b}"[::-1], 2)
        b2 = int(f"{b2:08b}"[::-1], 2)
        result[y * FIXED_BPR]     = b1
        result[y * FIXED_BPR + 1] = b2

    return bytes(result), actual_width


def render_glyph_varwidth(img, draw, font, codepoint, xoffset, yoffset):
    """
    Render at actual pixel width.
    Returns (pixel_data_bytes, width). width=0 means no glyph.
    """
    char = chr(codepoint)
    try:
        mask = font.getmask(char)
        if mask.size == (0, 0):
            return b'', 0
    except Exception:
        return b'', 0

    draw.rectangle((0, 0, 15, 15), fill="#000000")
    draw.fontmode = "1"
    draw.text((xoffset, yoffset), char, font=font, fill=(255, 255, 255))
    pixels = img.load()

    actual_width = 0
    for y in range(FONT_HEIGHT):
        for x in range(15, -1, -1):
            if sum(pixels[x, y]) >= 100:
                actual_width = max(actual_width, x + 1)
                break

    if actual_width == 0:
        return b'', 0

    bpr = (actual_width + 7) // 8
    result = bytearray(FONT_HEIGHT * bpr)
    for y in range(FONT_HEIGHT):
        for byte_idx in range(bpr):
            b = 0
            for bit in range(8):
                x = byte_idx * 8 + bit
                if x < actual_width and sum(pixels[x, y]) >= 100:
                    b |= (1 << bit)   # LSB-first
            result[y * bpr + byte_idx] = b

    return bytes(result), actual_width


def glyph_ascii_art(raw, width):
    if not raw or width == 0:
        return "(empty)"
    bpr = (width + 7) // 8
    lines = []
    for row in range(FONT_HEIGHT):
        word = 0
        for b in range(bpr):
            word |= raw[row * bpr + b] << (b * 8)
        lines.append("".join("█" if (word >> bit) & 1 else "·"
                              for bit in range(width)))
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Range parsing
# ---------------------------------------------------------------------------

def parse_range(spec):
    result = []
    for part in spec.split(","):
        part = part.strip()
        if "-" in part:
            lo, hi = part.split("-", 1)
            result.append((int(lo, 0), int(hi, 0)))
        else:
            cp = int(part, 0)
            result.append((cp, cp))
    return result


# ---------------------------------------------------------------------------
# Fixed-width generation
# ---------------------------------------------------------------------------

def generate_fixed(font_path, font_size, font_index, codepoints,
                   xoffset, yoffset, output_path, verbose):
    base_cp = codepoints[0]
    end_cp  = codepoints[-1]
    total   = len(codepoints)

    font = ImageFont.truetype(font_path, font_size, index=font_index)
    img  = Image.new("RGB", (16, 16), 0)
    draw = ImageDraw.Draw(img)
    out  = bytearray(total * FIXED_BPG)
    covered = 0

    print(f"Format : fixed-width ({FIXED_WIDTH}px, {FIXED_BPG} bytes/glyph)")
    print(f"Range  : U+{base_cp:04X}..U+{end_cp:04X}  ({total} slots)")

    for i, cp in enumerate(codepoints):
        raw, w = render_glyph_fixed(img, draw, font, cp, xoffset, yoffset)
        out[i * FIXED_BPG:(i + 1) * FIXED_BPG] = raw
        if w > 0:
            covered += 1
            if verbose:
                print(f"  U+{cp:04X} '{chr(cp)}' width={w}")
        if (i + 1) % 1000 == 0 or i == total - 1:
            print(f"  {i+1}/{total} ({(i+1)*100//total}%)", flush=True)

    Path(output_path).parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(out)

    print(f"\nDone. {len(out)} bytes written to '{output_path}'")
    print(f"  Covered : {covered}/{total} glyphs")
    print(f"\nC-side (is_fixed_width = true):")
    print(f"  char_offset      = codepoint - 0x{base_cp:04X};")
    print(f"  char_data_offset = char_offset * {FIXED_BPG};")


# ---------------------------------------------------------------------------
# Variable-width generation
# ---------------------------------------------------------------------------

def generate_varwidth(font_path, font_size, font_index, codepoints,
                      xoffset, yoffset, output_path, verbose):
    base_cp = codepoints[0]
    end_cp  = codepoints[-1]
    N       = len(codepoints)

    font = ImageFont.truetype(font_path, font_size, index=font_index)
    img  = Image.new("RGB", (16, 16), 0)
    draw = ImageDraw.Draw(img)

    widths  = bytearray(N)
    offsets = bytearray(N * 2)
    pixdata = bytearray()
    covered = 0
    cur_off = 0

    print(f"Format : variable-width  (header={N*3}B: {N}B widths + {N*2}B offsets)")
    print(f"Range  : U+{base_cp:04X}..U+{end_cp:04X}  ({N} slots)")

    for i, cp in enumerate(codepoints):
        raw, w = render_glyph_varwidth(img, draw, font, cp, xoffset, yoffset)
        widths[i] = w
        struct.pack_into("<H", offsets, i * 2, cur_off)
        if w > 0:
            pixdata += raw
            cur_off += len(raw)
            covered += 1
            if verbose:
                print(f"  U+{cp:04X} '{chr(cp)}' width={w} data_off={cur_off - len(raw)}")
        if (i + 1) % 1000 == 0 or i == N - 1:
            print(f"  {i+1}/{N} ({(i+1)*100//N}%)", flush=True)

    total_size = N + N * 2 + len(pixdata)

    Path(output_path).parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(widths)
        f.write(offsets)
        f.write(pixdata)

    print(f"\nDone. {total_size} bytes written to '{output_path}'")
    print(f"  Header  : {N * 3} bytes")
    print(f"  Pixdata : {len(pixdata)} bytes")
    print(f"  Covered : {covered}/{N} glyphs")
    print(f"\nC-side (is_fixed_width = false):")
    print(f"  char_offset = codepoint - 0x{base_cp:04X};")
    print(f"  fseek(file, char_offset, SEEK_SET);")
    print(f"  fread(&width, 1, 1, file);")
    print(f"  fseek(file, {N} + char_offset * 2, SEEK_SET);")
    print(f"  fread(&data_offset, 2, 1, file);  // uint16 LE")
    print(f"  fseek(file, {N * 3} + data_offset, SEEK_SET);")
    print(f"  fread(buf, 1, FONT_HEIGHT * ((width + 7) / 8), file);")


# ---------------------------------------------------------------------------
# Dispatcher
# ---------------------------------------------------------------------------

def generate(font_path, font_size, font_index, ranges, fixed_width,
             xoffset, yoffset, output_path, verbose):

    codepoints = []
    for base, end in ranges:
        codepoints.extend(range(base, end + 1))

    if not codepoints:
        print("ERROR: empty codepoint range", file=sys.stderr)
        sys.exit(1)

    base_cp = codepoints[0]
    if codepoints != list(range(base_cp, base_cp + len(codepoints))):
        print("ERROR: ranges must be contiguous.", file=sys.stderr)
        sys.exit(1)

    print(f"Font   : {font_path}  size={font_size}  index={font_index}")
    try:
        ImageFont.truetype(font_path, font_size, index=font_index)
    except OSError as e:
        print(f"ERROR: cannot open font: {e}", file=sys.stderr)
        sys.exit(1)

    if fixed_width:
        generate_fixed(font_path, font_size, font_index, codepoints,
                       xoffset, yoffset, output_path, verbose)
    else:
        generate_varwidth(font_path, font_size, font_index, codepoints,
                          xoffset, yoffset, output_path, verbose)


# ---------------------------------------------------------------------------
# Preview
# ---------------------------------------------------------------------------

def preview(font_path, font_size, font_index, codepoint, xoffset, yoffset):
    try:
        font = ImageFont.truetype(font_path, font_size, index=font_index)
    except OSError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)

    img  = Image.new("RGB", (16, 16), 0)
    draw = ImageDraw.Draw(img)

    raw_var, w  = render_glyph_varwidth(img, draw, font, codepoint, xoffset, yoffset)
    raw_fix, _  = render_glyph_fixed(img, draw, font, codepoint, xoffset, yoffset)

    print(f"U+{codepoint:04X}  '{chr(codepoint)}'  "
          f"{'width=' + str(w) if w else 'NO GLYPH IN FONT'}")
    print()
    print(glyph_ascii_art(raw_var, w))
    print()
    if w:
        bpr = (w + 7) // 8
        print(f"Variable-width bytes ({w}px, {bpr} bytes/row):")
        for row in range(FONT_HEIGHT):
            chunk = raw_var[row * bpr:(row + 1) * bpr]
            print(f"  row {row:2d}: {' '.join(f'{b:02X}' for b in chunk)}")
        print()
        print(f"Fixed-width bytes (12px, {FIXED_BPR} bytes/row):")
        for row in range(FONT_HEIGHT):
            chunk = raw_fix[row * FIXED_BPR:(row + 1) * FIXED_BPR]
            print(f"  row {row:2d}: {' '.join(f'{b:02X}' for b in chunk)}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    p = argparse.ArgumentParser(
        description="Generate Unicode-ordered binary font files (no codepage).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("fontfile", help="TTF/OTF/TTC font path")
    p.add_argument("--script", choices=list(SCRIPTS),
                   help="Predefined script block")
    p.add_argument("--all", action="store_true",
                   help="Generate all predefined script blocks into --outdir")
    p.add_argument("--outdir", default=None,
                   help="Output directory for --all (default: same dir as font)")
    p.add_argument("--range", dest="range_spec",
                   help="Custom range(s), e.g. '0x2200-0x22FF'")
    p.add_argument("--fixed", action="store_true",
                   help="Force fixed-width format for custom --range")
    p.add_argument("-o", "--output", default=None,
                   help="Output .bin path (default: auto-named)")
    p.add_argument("--size", type=int, default=12,
                   help="Font render size in pixels (default: 12)")
    p.add_argument("--xoffset", type=int, default=0)
    p.add_argument("--yoffset", type=int, default=0)
    p.add_argument("--index", type=int, default=0,
                   help="Face index for TTC collections (default: 0)")
    p.add_argument("--preview", type=lambda x: int(x, 0), metavar="CODEPOINT",
                   help="Preview one codepoint and exit (e.g. 0x03C9)")
    p.add_argument("--verbose", action="store_true",
                   help="Print each rendered codepoint")

    args = p.parse_args()

    if args.preview is not None:
        preview(args.fontfile, args.size, args.index,
                args.preview, args.xoffset, args.yoffset)
        return

    if args.all:
        if args.script or args.range_spec or args.output:
            p.error("--all cannot be combined with --script, --range or -o")
        outdir = Path(args.outdir) if args.outdir else Path(args.fontfile).parent
        outdir.mkdir(parents=True, exist_ok=True)
        print(f"Generating all {len(SCRIPTS)} scripts into '{outdir}'")
        print(f"Font   : {args.fontfile}  size={args.size}  index={args.index}")
        print()
        errors = []
        for name, (base, end, desc, fixed_width) in SCRIPTS.items():
            output_path = str(outdir / f"unicode_{name}.bin")
            print(f"=== {name} — {desc} ===")
            try:
                generate(args.fontfile, args.size, args.index,
                         [(base, end)], fixed_width,
                         args.xoffset, args.yoffset, output_path, args.verbose)
            except Exception as e:
                print(f"  ERROR: {e}")
                errors.append(name)
            print()
        print(f"Done. {len(SCRIPTS) - len(errors)}/{len(SCRIPTS)} scripts generated in '{outdir}'")
        if errors:
            print(f"Failed: {', '.join(errors)}")
        return

    if args.script and args.range_spec:
        p.error("use --script OR --range, not both")
    if not args.script and not args.range_spec:
        p.error("provide --script, --range or --all")

    if args.script:
        base, end, desc, fixed_width = SCRIPTS[args.script]
        ranges = [(base, end)]
        print(f"Script : {args.script} — {desc}")
        if args.output is None:
            args.output = str(Path(args.fontfile).parent /
                               f"unicode_{args.script}.bin")
    else:
        ranges = parse_range(args.range_spec)
        fixed_width = args.fixed
        if args.output is None:
            base0 = ranges[0][0]
            end0  = ranges[-1][1]
            args.output = str(Path(args.fontfile).parent /
                               f"unicode_{base0:04X}_{end0:04X}.bin")

    generate(args.fontfile, args.size, args.index, ranges, fixed_width,
             args.xoffset, args.yoffset, args.output, args.verbose)


if __name__ == "__main__":
    main()

