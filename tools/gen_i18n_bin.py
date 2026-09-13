#!/usr/bin/env python3
"""
gen_i18n_bin.py
===============
Generates per-language string binaries for the SD-card i18n loader in
Core/Src/retro-go/i18n/rg_i18n.c.  Each .bin holds the translated strings
of one language in the field order defined by `lang_t` in
Core/Inc/retro-go/rg_i18n_lang.h, padded with en_us fallbacks for any
field the target language does not translate.

Binary format (little-endian):

  [0]                u32 magic = 'I18N'   (0x4E383149)
  [4]                u16 version = 1
  [6]                u16 count             (number of string fields)
  [8]                u32 offsets[count]    (offset of each string into
                                            the data section, starting
                                            at the end of this header
                                            block = 8 + 4*count)
  [8 + 4*count]      string data: null-terminated UTF-8 strings,
                                  concatenated in field order

C-side reader fread()s the header, malloc()s a buffer for the data
section, fread()s it, then populates a runtime `lang_t lang_active`
by assigning each `s_XXX` pointer to `&buffer[offsets[i]]`.

Usage:
  gen_i18n_bin.py --header  Core/Inc/retro-go/rg_i18n_lang.h \\
                  --en-us   Core/Src/retro-go/i18n/rg_i18n_en_us.c \\
                  --lang    Core/Src/retro-go/i18n/rg_i18n_de_de.c \\
                  --define  CHEAT_CODES=1 --define INTFLASH_BANK=2 \\
                  --output  sd_content/lang/de_de.bin

`--define` must match the firmware build flags: `#if CHEAT_CODES` /
`#if INTFLASH_BANK` in lang_t change the field count/order, and a
mismatched .bin shifts every translated string (e.g. French UI labels).
"""

import argparse
import re
import struct
import sys
from pathlib import Path


MAGIC = 0x4E383149  # 'I18N' little-endian
VERSION = 1

# Field declaration in lang_t: `    const char *s_FieldName;`
HEADER_FIELD_RE = re.compile(r'^\s*const\s+char\s*\*\s*s_(\w+)\s*;')

# Field initializer in a lang_xx_xx.c file: `    .s_FieldName = "string",`
# Captures field name and the C string body (with escapes intact).
INIT_FIELD_RE = re.compile(
    r'^\s*\.s_(\w+)\s*=\s*"((?:[^"\\]|\\.)*)"\s*,?\s*(?://.*)?$'
)

# C block comment, non-greedy across newlines.
BLOCK_COMMENT_RE = re.compile(r'/\*.*?\*/', re.DOTALL)


def _strip_line_comments(text: str) -> str:
    """Remove `// ...` line comments. Done BEFORE block-comment stripping so
    a path glob like `porting/*/*_i18n.c` inside a // comment cannot open a
    bogus /* ... */ span that swallows following field declarations."""
    out = []
    for line in text.splitlines(keepends=True):
        # Keep // inside strings out of scope — lang_t headers don't put
        # URLs in string literals on field lines.
        if '//' in line:
            # Preserve the newline.
            nl = '\n' if line.endswith('\n') else ''
            code = line[:-1] if nl else line
            # Truncate at first // not inside a block we're about to strip.
            code = code.split('//', 1)[0].rstrip()
            out.append(code + nl)
        else:
            out.append(line)
    return ''.join(out)


def _strip_block_comments(text: str) -> str:
    """Remove `/* ... */` blocks but preserve line numbering so any later
    error message still points at the right source line. Each comment is
    replaced with the same count of newlines it contained.
    """
    def repl(m: re.Match) -> str:
        return '\n' * m.group(0).count('\n')
    return BLOCK_COMMENT_RE.sub(repl, text)


def _strip_c_comments(text: str) -> str:
    return _strip_block_comments(_strip_line_comments(text))


def parse_defines(define_args: list[str]) -> dict[str, int]:
    """Parse repeated --define NAME=VALUE into {NAME: int}."""
    out: dict[str, int] = {}
    for item in define_args:
        if '=' not in item:
            raise SystemExit(f'--define expects NAME=VALUE, got {item!r}')
        name, val = item.split('=', 1)
        name = name.strip()
        try:
            out[name] = int(val.strip(), 0)
        except ValueError:
            raise SystemExit(f'--define {name}={val!r}: value must be an integer')
    return out


def _eval_if_condition(cond: str, defines: dict[str, int]) -> bool:
    """Evaluate a simple `#if` condition used in rg_i18n_lang.h.

    Supports: `NAME == N`, `NAME != N`, `!defined (NAME)`, `defined (NAME)`.
    Unknown identifiers default to 0 (same as an undefined C macro in `#if`).
    """
    cond = cond.strip()
    m = re.fullmatch(r'!defined\s*\(\s*(\w+)\s*\)', cond)
    if m:
        return m.group(1) not in defines
    m = re.fullmatch(r'defined\s*\(\s*(\w+)\s*\)', cond)
    if m:
        return m.group(1) in defines
    m = re.fullmatch(r'(\w+)\s*==\s*(\d+)', cond)
    if m:
        return defines.get(m.group(1), 0) == int(m.group(2))
    m = re.fullmatch(r'(\w+)\s*!=\s*(\d+)', cond)
    if m:
        return defines.get(m.group(1), 0) != int(m.group(2))
    raise SystemExit(f'unsupported #if condition in header: {cond!r}')


def parse_header_field_order(path: Path, defines=None) -> list:
    """Return the ordered list of `s_XXX` field names declared in lang_t.

    `#if` / `#endif` inside the struct are evaluated with `defines` so the
    .bin layout matches the firmware's compile-time lang_t (CHEAT_CODES,
    INTFLASH_BANK). Without matching defines the loader assigns strings
    to the wrong fields — a classic "every French label is shifted" bug.
    """
    if defines is None:
        defines = {}
    fields = []
    seen = set()
    in_struct = False
    # Stack of active skip flags: True means this #if region is excluded.
    skip_stack: list[bool] = []
    text = _strip_c_comments(path.read_text(encoding='utf-8'))
    for line in text.splitlines():
        stripped = line.strip()
        if not in_struct:
            if stripped.startswith('typedef struct'):
                in_struct = True
            continue
        if stripped.startswith('}'):
            break
        if stripped.startswith('#if'):
            cond = stripped[3:].strip()
            # Nested: if parent already skipped, keep skipping.
            parent_skip = skip_stack[-1] if skip_stack else False
            skip_stack.append(parent_skip or not _eval_if_condition(cond, defines))
            continue
        if stripped.startswith('#endif'):
            if skip_stack:
                skip_stack.pop()
            continue
        if stripped.startswith('#'):
            # #else / #elif not used in this header today — refuse rather
            # than silently mis-parse.
            raise SystemExit(f'unsupported preprocessor directive in {path}: {stripped}')
        if skip_stack and skip_stack[-1]:
            continue
        m = HEADER_FIELD_RE.match(line)
        if m:
            name = m.group(1)
            if name in seen:
                raise SystemExit(f'duplicate field s_{name} in {path}')
            seen.add(name)
            fields.append(name)
    if not fields:
        raise SystemExit(f'no s_XXX fields found in {path}')
    return fields


_HEX = set('0123456789abcdefABCDEF')
_OCT = set('01234567')
_SIMPLE_ESCAPES = {
    'n': b'\n', 't': b'\t', 'r': b'\r', '0': b'\0',
    'a': b'\a', 'b': b'\b', 'f': b'\f', 'v': b'\v',
    '\\': b'\\', '"': b'"', "'": b"'", '?': b'?',
}


def decode_c_string(raw: str) -> str:
    """Decode a C string-literal body to a Python str.

    Handles: \\n \\t \\r \\\\ \\" \\xH... (greedy hex, 1+ digits)
             \\NNN (octal, 1-3 digits). UTF-8 byte sequences that
    appear literally in the source (e.g. "日本語") pass through.

    Python's stdlib `unicode_escape` chokes on `\\x6` (single hex
    digit) which C accepts, so we walk the string ourselves.
    """
    src = raw.encode('utf-8')
    out = bytearray()
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c != ord('\\'):
            out.append(c)
            i += 1
            continue
        if i + 1 >= n:
            raise ValueError(f'lone backslash in {raw!r}')
        nxt = chr(src[i + 1])
        if nxt == 'x':
            # Greedy hex: consume as many hex digits as available.
            j = i + 2
            while j < n and chr(src[j]) in _HEX:
                j += 1
            if j == i + 2:
                raise ValueError(f'\\x with no hex digits in {raw!r}')
            value = int(src[i + 2:j].decode('ascii'), 16) & 0xFF
            out.append(value)
            i = j
        elif nxt in _OCT:
            # Octal: 1-3 digits.
            j = i + 1
            while j < n and j - i <= 3 and chr(src[j]) in _OCT:
                j += 1
            value = int(src[i + 1:j].decode('ascii'), 8) & 0xFF
            out.append(value)
            i = j
        elif nxt in _SIMPLE_ESCAPES:
            out += _SIMPLE_ESCAPES[nxt]
            i += 2
        else:
            raise ValueError(f'unknown escape \\{nxt} in {raw!r}')
    # The resulting bytes are the C string's raw content, which is
    # encoded as UTF-8 in the source for all non-ASCII content.
    return out.decode('utf-8')


def parse_lang_strings(path: Path) -> dict[str, str]:
    """Return {field_name: decoded_python_string} for the .c file."""
    result = {}
    text = _strip_c_comments(path.read_text(encoding='utf-8'))
    for line in text.splitlines():
        m = INIT_FIELD_RE.match(line)
        if not m:
            continue
        name, raw = m.group(1), m.group(2)
        try:
            decoded = decode_c_string(raw)
        except (ValueError, UnicodeError) as e:
            raise SystemExit(f'cannot decode {path}:{name}: {e}')
        if name in result:
            # Match C compiler: later designated initializer overrides
            # the earlier one. Flag it so source bugs get noticed.
            print(f'  warning: duplicate .s_{name} in {path.name} '
                  f'— keeping the later value', file=sys.stderr)
        result[name] = decoded
    if not result:
        raise SystemExit(f'no .s_XXX initializers found in {path}')
    return result


def build_blob(field_order: list[str],
               lang_strings: dict[str, str],
               en_us_strings: dict[str, str]) -> bytes:
    """Build the .bin payload for one language.

    For each field in `field_order`:
      - use lang_strings[field] if present
      - else fall back to en_us_strings[field]
      - else hard error (a field exists in lang_t but no language has it)
    """
    encoded = []
    fell_back = 0
    missing = 0
    for name in field_order:
        s = lang_strings.get(name)
        if s is None:
            s = en_us_strings.get(name)
            if s is None:
                missing += 1
                print(f'  warning: field s_{name} missing from both target '
                      f'language and en_us — emitting empty string',
                      file=sys.stderr)
                s = ''
            else:
                fell_back += 1
        encoded.append(s.encode('utf-8') + b'\0')

    count = len(field_order)
    header_size = 8 + 4 * count  # magic+version+count+offsets[count]

    # offsets are relative to the start of the data section (right after
    # the offset table). Computing them sequentially:
    offsets = []
    cursor = 0
    for blob in encoded:
        offsets.append(cursor)
        cursor += len(blob)
    data_size = cursor

    out = bytearray()
    out += struct.pack('<IHH', MAGIC, VERSION, count)
    for o in offsets:
        out += struct.pack('<I', o)
    for blob in encoded:
        out += blob

    expected_size = header_size + data_size
    assert len(out) == expected_size, (len(out), expected_size)
    build_blob.last_stats = (
        f'{count} fields, {fell_back} fell back to en_us, '
        f'{missing} missing, payload = {len(out)} bytes')
    return bytes(out)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--header', required=True, type=Path,
                   help='path to rg_i18n_lang.h (defines field order)')
    p.add_argument('--en-us', required=True, type=Path,
                   help='path to rg_i18n_en_us.c (fallback source)')
    p.add_argument('--lang', required=True, type=Path,
                   help='path to rg_i18n_xx_xx.c (language to bundle)')
    p.add_argument('--output', required=True, type=Path,
                   help='output .bin path')
    p.add_argument('--define', action='append', default=[], metavar='NAME=VALUE',
                   help='preprocessor define for evaluating #if in the header '
                        '(repeatable). Pass the same CHEAT_CODES / INTFLASH_BANK '
                        'as the firmware build so .bin field order matches lang_t.')
    args = p.parse_args()

    defines = parse_defines(args.define)
    # Match the header's `#if !defined(CHEAT_CODES) #define CHEAT_CODES 0`.
    defines.setdefault('CHEAT_CODES', 0)
    field_order = parse_header_field_order(args.header, defines)
    en_us = parse_lang_strings(args.en_us)
    lang = parse_lang_strings(args.lang) if args.lang != args.en_us else en_us
    blob = build_blob(field_order, lang, en_us)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)

    # Emit start + result as ONE write so concurrent `make -jN` invocations
    # don't interleave each other's lines.
    define_note = ' '.join(f'{k}={v}' for k, v in sorted(defines.items())
                           if k in ('CHEAT_CODES', 'INTFLASH_BANK'))
    sys.stderr.write(
        f'gen_i18n_bin: {args.lang.name} -> {args.output}'
        f'{(" (" + define_note + ")") if define_note else ""}\n'
        f'  {build_blob.last_stats}\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
