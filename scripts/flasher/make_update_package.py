#!/usr/bin/env python3
"""Build one bank-specific retro-go_update archive."""

import argparse
import io
import os
import struct
import tarfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--updater", required=True)
    ap.add_argument("--content", required=True)
    ap.add_argument("--output", required=True)
    args = ap.parse_args()

    with open(args.updater, "rb") as f:
        updater = f.read()
    if len(updater) > 1024 * 1024:
        raise SystemExit("firmware_update.bin exceeds 1 MiB")

    tar_bytes = io.BytesIO()
    def normalize(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        info.mtime = 0
        return info

    with tarfile.open(fileobj=tar_bytes, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for name in sorted(os.listdir(args.content)):
            path = os.path.join(args.content, name)
            tar.add(path, arcname=name, recursive=True, filter=normalize)

    payload = tar_bytes.getvalue()
    output = updater + bytes(1024 * 1024 - len(updater))
    output += struct.pack("<I", len(updater))
    output += payload
    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(output)


if __name__ == "__main__":
    main()
