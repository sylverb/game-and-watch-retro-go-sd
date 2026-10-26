#!/usr/bin/env python3
"""Fail the build if .rodata_earthbound holds a pointer into itself.

EarthBound's .rodata_earthbound is extracted to earthbound.ro and cached at a
runtime extflash address that differs from its link VMA. PatchCodeRodataOffset
(Core/Src/porting/earthbound/main_earthbound.c) only rewrites pointers that live
in RAM_EMU, so a pointer stored *inside* .rodata_earthbound that targets
.rodata_earthbound keeps its link-time value and reads garbage on the device
(e.g. a const table of string pointers, or a const struct with a pointer field).
Such tables must be tagged EB_NORELOC (core/embedded.h) so they land in RAM.

The ELF must be linked with -Wl,--emit-relocs: every R_ARM_ABS32 the linker
resolved inside .rodata_earthbound is then listed in .rel.rodata_earthbound, and
the final word at each of those offsets is the resolved pointer. This is exact:
no byte-pattern guessing over asset data.

Usage: check_eb_rodata_relocs.py <tool-prefix> <elf>
  tool-prefix: e.g. /path/to/bin/arm-none-eabi-
"""

import bisect
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

SECTION = ".rodata_earthbound"


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def main():
    prefix, elf = sys.argv[1], sys.argv[2]

    m = re.search(
        r"\s" + re.escape(SECTION) + r"\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)",
        run(prefix + "readelf", "-S", "--wide", elf),
    )
    if m is None:
        return 0  # no EarthBound in this build
    vma, size = int(m.group(1), 16), int(m.group(3), 16)

    relocs = run(prefix + "readelf", "-r", "--wide", elf)
    block = re.search(r"Relocation section '\.rel" + re.escape(SECTION) + r"'.*?\n\n", relocs + "\n\n", re.S)
    if block is None:
        if "Relocation section" not in relocs:
            print(f"error: {elf} has no relocations; link it with -Wl,--emit-relocs", file=sys.stderr)
            return 1
        return 0  # linked with relocs, but nothing in this section points anywhere
    offsets = [
        int(line.split()[0], 16)
        for line in block.group(0).splitlines()
        if re.match(r"^[0-9a-f]{8}\s", line) and "R_ARM_ABS32" in line
    ]

    with tempfile.TemporaryDirectory() as tmp:
        blob = Path(tmp) / "ro.bin"
        run(prefix + "objcopy", "-O", "binary", "--only-section=" + SECTION, elf, str(blob))
        data = blob.read_bytes()

    syms = []
    for line in run(prefix + "nm", "-n", "-S", elf).splitlines():
        p = line.split()
        if len(p) >= 4 and vma <= int(p[0], 16) < vma + size:
            syms.append((int(p[0], 16), int(p[1], 16), p[3]))
    addrs = [s[0] for s in syms]

    bad = {}
    for off in offsets:
        # REL offsets are VMAs for a linked image.
        pos = off - vma
        if not 0 <= pos <= len(data) - 4:
            continue
        target = struct.unpack_from("<I", data, pos)[0]
        if not vma <= target < vma + size:
            continue
        i = bisect.bisect_right(addrs, off) - 1
        name = syms[i][2] if i >= 0 and off < syms[i][0] + max(syms[i][1], 1) else f"<0x{off:08x}>"
        bad.setdefault(name, 0)
        bad[name] += 1

    if bad:
        print(f"error: {SECTION} contains pointers into itself, which are not relocated at runtime.", file=sys.stderr)
        print("       Tag each table EB_NORELOC (#include \"core/embedded.h\"):", file=sys.stderr)
        for name, n in sorted(bad.items()):
            print(f"         {name} ({n} pointer{'s' if n > 1 else ''})", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
