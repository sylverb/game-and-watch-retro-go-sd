#!/usr/bin/env python3
"""Resolve sampled PCs against the ld map, killing overlay VMA aliasing.

Overlay cores (earthbound/zelda3/smw/...) all link to the same RAM VMA. For any
0x24xxxxxx PC we keep ONLY build/earthbound/* sections, so a running EB PC never
resolves to an aliased zelda3/smw symbol. Flash (0x08) / ITCM (0x00) / DTCM (0x20)
code is unambiguous and kept as-is.

Usage: eb_resolve.py <map_file> <pcs_file>
"""
import sys, re, os
from collections import Counter

MAP, PCS = sys.argv[1], sys.argv[2]

# overlay RAM window where cores alias
OV_LO, OV_HI = 0x24000000, 0x24800000

# placement line: optional inline section name, then vma, size, object
place = re.compile(
    r"^\s*(\.\S+)?\s+0x0*([0-9a-fA-F]+)\s+0x0*([0-9a-fA-F]+)\s+(\S+\.o|\S+\.a\([^)]+\))\s*$"
)
# lone section-name line (name wraps to its own line)
nameonly = re.compile(r"^\s+(\.\S+)\s*$")

def is_code(sec):
    return (sec.startswith(".text") or sec.startswith(".overlay")
            or "itcram" in sec or "ram_exec" in sec or "_hot" in sec)

ranges = []  # (lo, hi, obj, func)
pending = None
with open(MAP, errors="replace") as f:
    for line in f:
        mo = place.match(line)
        if mo:
            sec = mo.group(1) or pending
            pending = None
            vma = int(mo.group(2), 16)
            size = int(mo.group(3), 16)
            obj = mo.group(4)
            if size == 0 or vma == 0 or sec is None:
                continue
            if not is_code(sec):
                continue
            # kill overlay aliasing: in the RAM window keep only earthbound
            if OV_LO <= vma < OV_HI and "build/earthbound/" not in obj:
                continue
            func = re.sub(r"^\.(text|overlay_\w+|_?itcram\w*|_?ram_exec\w*)\.?", "", sec)
            ranges.append((vma, vma + size, obj, func or sec))
            continue
        no = nameonly.match(line)
        if no:
            pending = no.group(1)

ranges.sort()
los = [r[0] for r in ranges]
import bisect
def lookup(pc):
    i = bisect.bisect_right(los, pc) - 1
    if 0 <= i < len(ranges):
        lo, hi, obj, func = ranges[i]
        if lo <= pc < hi:
            return obj, func
    return None, None

pcs = []      # list of (pc, lr|None)
for ln in open(PCS):
    parts = ln.split()
    if not parts or not parts[0].startswith("0x"):
        continue
    pc = int(parts[0], 16)
    lr = int(parts[1], 16) & ~1 if len(parts) > 1 and parts[1].startswith("0x") else None
    pcs.append((pc, lr))

def region(pc):
    top = pc >> 24
    return {0x08: "FLASH-shell", 0x24: "EB-overlay(RAM)", 0x00: "ITCM",
            0x20: "DTCM", 0x1c: "?", 0x30: "?"}.get(top, f"0x{top:02x}xxxxxx")

reg_h, obj_h, fn_h = Counter(), Counter(), Counter()
unresolved = Counter()
leaf_callers = Counter()   # who called memset/memcpy (leaf libc), via LR
LEAF = ("memset", "memcpy", ".text")  # .text = memcpy-armv7m.o
for pc, lr in pcs:
    reg_h[region(pc)] += 1
    obj, func = lookup(pc)
    if obj is None:
        obj_h["<unresolved>"] += 1
        unresolved[region(pc)] += 1
    else:
        base = os.path.basename(obj)
        obj_h[base] += 1
        fn_h[(func, base)] += 1
        if ("memset" in base or "memcpy" in base) and lr is not None:
            lo, lf = lookup(lr)
            leaf_callers[(lf or f"0x{lr:08x}", os.path.basename(lo) if lo else "?")] += 1

n = len(pcs)
def bar(c):
    return "#" * int(40 * c / n) if n else ""

print(f"\n=== {n} samples ===\n")
print("--- REGION ---")
for k, c in reg_h.most_common():
    print(f"  {c*100/n:5.1f}%  {c:5d}  {k}")
print("\n--- OBJECT (module) ---")
for k, c in obj_h.most_common(25):
    print(f"  {c*100/n:5.1f}%  {c:5d}  {bar(c):40s} {k}")
print("\n--- FUNCTION (top 30) ---")
for (fn, obj), c in fn_h.most_common(30):
    print(f"  {c*100/n:5.1f}%  {c:5d}  {fn:34s} {obj}")
if leaf_callers:
    tot = sum(leaf_callers.values())
    print(f"\n--- CALLERS of memset/memcpy (via LR, {tot} samples) ---")
    for (fn, obj), c in leaf_callers.most_common(15):
        print(f"  {c*100/n:5.1f}% of frame  {c:5d}  {fn:30s} {obj}")
if unresolved:
    print("\n--- UNRESOLVED by region ---")
    for k, c in unresolved.most_common():
        print(f"  {c*100/n:5.1f}%  {c:5d}  {k}")
