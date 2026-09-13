#!/usr/bin/env python3
"""
Sentinel relocation for PICO-8 pico8.ro (0xBEEF0000-range refs → QSPI XIP).
"""
from __future__ import annotations

import struct

PICO8_CODE_BASE = 0xBEEF0000


def patch_pico8_ro_bytes(ro: bytes, target_xip_addr: int) -> tuple[bytes, int]:
    """Relocate 0xBEEF0000-range sentinel refs to target_xip_addr. Returns (blob, patch_count)."""
    code_size = len(ro)
    data = bytearray(ro)
    offset_u32 = (target_xip_addr - PICO8_CODE_BASE) & 0xFFFFFFFF
    if offset_u32 >= 0x80000000:
        offset_s32 = offset_u32 - 0x100000000
    else:
        offset_s32 = offset_u32

    patched = 0
    n = (code_size // 4) * 4
    upper = PICO8_CODE_BASE + code_size
    for i in range(0, n, 4):
        value = struct.unpack_from("<I", data, i)[0]
        masked = value & ~1
        if masked >= PICO8_CODE_BASE and masked < upper:
            new_val = (value + offset_s32) & 0xFFFFFFFF
            struct.pack_into("<I", data, i, new_val)
            patched += 1
    return bytes(data), patched
