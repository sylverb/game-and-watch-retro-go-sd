#!/usr/bin/env python3
"""Parse a gnwmanager monitor log containing an FF4 autonomous savestate
dump and reconstruct the binary .lss file.

Expected log fragments:
    === FF4_SAVESTATE_DUMP_BEGIN size=NNN ===
    FF4_DUMP: <6-hex offset> <hex bytes...>
    FF4_DUMP: ...
    === FF4_SAVESTATE_DUMP_END size=NNN ===

Usage:
    extract_ff4_savestate.py <input_log> <output_lss>
"""
import re
import sys

BEGIN_RE = re.compile(r"=== FF4_SAVESTATE_DUMP_BEGIN ===")
END_RE   = re.compile(r"=== FF4_SAVESTATE_DUMP_END size=(\d+) ===")
DUMP_RE  = re.compile(r"FF4_DUMP: ([0-9a-f]{6}) ([0-9a-f]+)")
OVF_RE   = re.compile(r"=== FF4_SAVESTATE_DUMP_OVERFLOW need=(\d+) had=(\d+) ===")


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    log_path, out_path = argv[1], argv[2]

    expected_size = None
    in_block = False
    chunks = {}     # offset -> bytes

    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip()
            if BEGIN_RE.search(line):
                in_block = True
                chunks.clear()
                print("[begin]")
                continue
            m = OVF_RE.search(line)
            if m:
                print(f"[overflow] need={m.group(1)} had={m.group(2)} — aborting",
                      file=sys.stderr)
                return 3
            m = END_RE.search(line)
            if m:
                in_block = False
                expected_size = int(m.group(1))
                print(f"[end] declared size={expected_size}")
                break
            if not in_block:
                continue
            m = DUMP_RE.search(line)
            if m:
                off = int(m.group(1), 16)
                payload = bytes.fromhex(m.group(2))
                chunks[off] = payload

    if expected_size is None:
        print("[error] no savestate dump found in log", file=sys.stderr)
        return 4

    # Reconstruct contiguous buffer
    out = bytearray(expected_size)
    written = 0
    for off in sorted(chunks):
        payload = chunks[off]
        if off + len(payload) > expected_size:
            # tail line may exceed declared size if printf rounding; clamp
            payload = payload[: expected_size - off]
        out[off:off + len(payload)] = payload
        written += len(payload)

    missing = expected_size - written
    if missing:
        print(f"[warn] {missing} bytes missing — log may be truncated",
              file=sys.stderr)

    # The LSSF header has a length field at bytes 8..11 that snes_saveState
    # backfills via sh_placeInt(sh, 8, sh->offset). On G&W that placeInt
    # writes straight into the (undersized) scratch buffer instead of going
    # through the streamed sh_writeByte path, so the dumped length field
    # still shows the placeholder. Overwrite it client-side with the value
    # the END marker just gave us — this matches what the file would have
    # contained if sh_placeInt had streamed.
    out[8:12] = expected_size.to_bytes(4, "little")

    with open(out_path, "wb") as f:
        f.write(out)

    print(f"[done] wrote {len(out)} bytes to {out_path}")

    if len(out) >= 4 and bytes(out[:4]) == b"LSSF":
        ver = int.from_bytes(out[4:8], "little")
        length = int.from_bytes(out[8:12], "little")
        print(f"[hdr ] LSSF v{ver}, length field = {length}")
    else:
        print("[error] output does not start with LSSF magic", file=sys.stderr)
        return 5
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
