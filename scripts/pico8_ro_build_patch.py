#!/usr/bin/env python3
"""
Sentinel relocation for a mapped (XiP) sidecar.

A blob linked at a sentinel base holds absolute pointers into itself. Whoever
places it at a real address must add the delta to every such pointer. PICO-8's
pico8.ro (0xBEEF0000) was the first; gba.xip (0xDEC00000) is the same shape, and
a project declares its own base -- see "mapped"/"relocBase" in the distribution
manifest. The runtime equivalent is flash_relocate_cb_t (Core/Inc/gw_flash_alloc.h),
which does this per chunk while caching a file from SD.
"""
from __future__ import annotations

import struct

PICO8_CODE_BASE = 0xBEEF0000


def patch_mapped_bytes(ro: bytes, target_xip_addr: int, base: int) -> tuple[bytes, int]:
    """Relocate `base`-range sentinel refs to target_xip_addr. Returns (blob, patch_count).

    Only 4-byte-aligned words are considered, and only those whose value (with
    bit 0 -- the Thumb bit -- masked off for the test) falls inside
    [base, base + len(ro)), i.e. pointers into the blob itself. The delta is
    added to the UNMASKED value so a Thumb pointer stays odd.

    This is a heuristic: ordinary data that happens to land in that range is
    rewritten too. It is safe only because the sentinel is chosen far from any
    plausible data value, which is a requirement on the project, not a property
    of this function.
    """
    code_size = len(ro)
    data = bytearray(ro)
    offset_u32 = (target_xip_addr - base) & 0xFFFFFFFF
    if offset_u32 >= 0x80000000:
        offset_s32 = offset_u32 - 0x100000000
    else:
        offset_s32 = offset_u32

    patched = 0
    n = (code_size // 4) * 4
    upper = base + code_size
    for i in range(0, n, 4):
        value = struct.unpack_from("<I", data, i)[0]
        masked = value & ~1
        if masked >= base and masked < upper:
            new_val = (value + offset_s32) & 0xFFFFFFFF
            struct.pack_into("<I", data, i, new_val)
            patched += 1
    return bytes(data), patched


def patch_pico8_ro_bytes(ro: bytes, target_xip_addr: int) -> tuple[bytes, int]:
    """Back-compat wrapper: PICO-8's base is 0xBEEF0000."""
    return patch_mapped_bytes(ro, target_xip_addr, PICO8_CODE_BASE)
