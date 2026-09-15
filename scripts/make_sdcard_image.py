#!/usr/bin/env python3
"""Create a blank SD-card image with a single FAT32 primary partition.

This replaces the `dd` + `parted` pair that used to build build/sdcard.img.

`parted` is Linux-only: there is no Homebrew formula for it and no macOS port,
so `make gwemu_release` could not build an SD image on a Mac at all. mtools'
`mpartition` is not a drop-in either -- it refuses the `::` pseudo-drive that
every other mtools call in this build uses and would need a generated MTOOLSRC.

Writing the 64-byte MBR partition table ourselves is less machinery than either,
and it is byte-for-byte identical on every host, which matters here: the guiding
rule of the gwemu integration is that the emulator runs the same bytes as the
hardware. python3 is already a hard build dependency, so this adds no new one.

The output matches what `parted -s img mklabel msdos mkpart primary fat32 1MiB 100%`
produced on Linux in every field that is read:

    type 0x0C (FAT32 LBA), first LBA 2048 (=1MiB), and the remaining sectors --
    all three verified identical against a parted-generated reference image.

The legacy CHS triples differ, and deliberately so: parted derives a per-image
fake geometry (4/32 for a 256 MB file), while this writes the conventional
255/63 that fdisk and every distro image builder use. Partition type 0x0C means
"LBA addressing is authoritative", so nothing -- not FatFS on the device, not
mtools, not QEMU -- consults those bytes. Reproducing parted's geometry
heuristic would be guesswork for a field no reader looks at.

The filesystem itself is still laid down by mformat at the @@1M offset, exactly
as before -- this script only creates the container and the partition table.
"""

import argparse
import os
import struct
import sys

SECTOR = 512
# The partition starts at 1 MiB, which is what the mformat/mcopy calls in
# Makefile.common address as `img@@1M`. Keep the two in step.
FIRST_LBA = 2048
PART_TYPE_FAT32_LBA = 0x0C


def chs(lba, heads=255, sectors=63):
    """Legacy CHS triple for `lba`, clamped to the classic 1023/254/63 maximum.

    Nothing reads these -- the partition is type 0x0C ("LBA") precisely so that
    the LBA fields are authoritative -- but parted fills them in and some tools
    sanity-check them, so we reproduce the same convention.
    """
    c, rem = divmod(lba, heads * sectors)
    h, s = divmod(rem, sectors)
    if c > 1023:
        c, h, s = 1023, heads - 1, sectors - 1
    return bytes([h, ((c >> 2) & 0xC0) | ((s + 1) & 0x3F), c & 0xFF])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image", help="output image path")
    ap.add_argument("--size-mb", type=int, required=True, help="total image size in MiB")
    args = ap.parse_args()

    total_sectors = args.size_mb * 1024 * 1024 // SECTOR
    if total_sectors <= FIRST_LBA:
        sys.exit(f"error: --size-mb {args.size_mb} leaves no room for a partition")
    part_sectors = total_sectors - FIRST_LBA

    # Create the file sparsely: a 256 MB image of zeros costs no real disk space
    # until the guest writes to it. `dd if=/dev/zero` allocated the lot.
    with open(args.image, "wb") as f:
        f.truncate(args.size_mb * 1024 * 1024)

        entry = (
            b"\x00"                                  # not bootable
            + chs(FIRST_LBA)                         # CHS of first sector
            + bytes([PART_TYPE_FAT32_LBA])           # partition type
            + chs(FIRST_LBA + part_sectors - 1)      # CHS of last sector
            + struct.pack("<II", FIRST_LBA, part_sectors)
        )
        assert len(entry) == 16

        f.seek(446)
        f.write(entry + b"\x00" * 48)                # entry 1, entries 2-4 empty
        f.write(b"\x55\xaa")                         # MBR signature

    print(
        f"Created {args.size_mb}MB SD card image with a FAT32 partition "
        f"at sector {FIRST_LBA} ({part_sectors} sectors): {args.image}"
    )


if __name__ == "__main__":
    main()
