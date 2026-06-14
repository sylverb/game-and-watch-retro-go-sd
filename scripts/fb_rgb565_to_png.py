#!/usr/bin/env python3
"""Convert a raw 320x240 RGB565 little-endian framebuffer to PNG.

Usage: fb_rgb565_to_png.py <input.raw> <output.png>
"""
import sys
import struct

WIDTH, HEIGHT = 320, 240
EXPECTED_BYTES = WIDTH * HEIGHT * 2


def rgb565_to_rgb888(pixel: int) -> bytes:
    r5 = (pixel >> 11) & 0x1F
    g6 = (pixel >> 5) & 0x3F
    b5 = pixel & 0x1F
    r = (r5 << 3) | (r5 >> 2)
    g = (g6 << 2) | (g6 >> 4)
    b = (b5 << 3) | (b5 >> 2)
    return bytes((r, g, b))


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    in_path, out_path = argv[1], argv[2]
    with open(in_path, "rb") as f:
        data = f.read()
    if len(data) < EXPECTED_BYTES:
        print(f"[error] expected {EXPECTED_BYTES} bytes, got {len(data)}",
              file=sys.stderr)
        return 3
    data = data[:EXPECTED_BYTES]

    # Decode all pixels to RGB888 first.
    rgb = bytearray(WIDTH * HEIGHT * 3)
    for i in range(WIDTH * HEIGHT):
        pixel = data[2 * i] | (data[2 * i + 1] << 8)
        r5 = (pixel >> 11) & 0x1F
        g6 = (pixel >> 5) & 0x3F
        b5 = pixel & 0x1F
        rgb[3 * i + 0] = (r5 << 3) | (r5 >> 2)
        rgb[3 * i + 1] = (g6 << 2) | (g6 >> 4)
        rgb[3 * i + 2] = (b5 << 3) | (b5 >> 2)

    # Write a minimal PNG (uncompressed IDAT via zlib).
    import zlib
    raw = bytearray()
    for y in range(HEIGHT):
        raw.append(0)  # filter byte: none
        raw.extend(rgb[y * WIDTH * 3: (y + 1) * WIDTH * 3])
    compressed = zlib.compress(bytes(raw), 9)

    def chunk(tag: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(tag + payload)
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", crc))

    ihdr = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", compressed)
           + chunk(b"IEND", b""))
    with open(out_path, "wb") as f:
        f.write(png)
    print(f"[done] {out_path} ({WIDTH}x{HEIGHT}, {len(png)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
