#!/usr/bin/env python3
import argparse
import os
import pathlib
import shutil
import struct
import sys

from littlefs import LittleFS
from littlefs.context import UserContextFile


DEFAULT_DIRS = ("cores",)
ERASED_BYTE = b"\xff"


def parse_int(value):
    return int(str(value), 0)


def write_erased_image(path, size):
    chunk = ERASED_BYTE * (1024 * 1024)
    with path.open("wb") as image:
        remaining = size
        while remaining:
            count = min(remaining, len(chunk))
            image.write(chunk[:count])
            remaining -= count


def reverse_blocks(src, dst, block_size):
    size = src.stat().st_size
    if size % block_size:
        raise ValueError(f"{src} size ({size}) is not a multiple of block size ({block_size})")

    with src.open("rb") as in_file, dst.open("wb") as out_file:
        for block_index in range(size // block_size):
            in_file.seek(size - ((block_index + 1) * block_size))
            out_file.write(in_file.read(block_size))


def set_file_time(fs, path, host_path):
    timestamp = int(host_path.stat().st_mtime)
    fs.setattr(path, "t", struct.pack("<I", timestamp))


def copy_file(fs, src, dst):
    parent = dst.rsplit("/", 1)[0]
    if parent:
        fs.makedirs(parent, exist_ok=True)

    with src.open("rb") as in_file, fs.open(dst, "wb") as out_file:
        shutil.copyfileobj(in_file, out_file, length=64 * 1024)
    set_file_time(fs, dst, src)


def copy_tree(fs, src, dest, exclude_relpaths=None):
    exclude_relpaths = exclude_relpaths or frozenset()
    copied_files = 0
    fs.makedirs("/" + dest, exist_ok=True)

    for path in sorted(src.rglob("*")):
        if path.name == ".DS_Store":
            continue

        rel_path = path.relative_to(src).as_posix()
        if rel_path in exclude_relpaths:
            continue
        fs_path = "/" + dest if not rel_path else "/" + dest + "/" + rel_path

        if path.is_dir():
            fs.makedirs(fs_path, exist_ok=True)
        elif path.is_file():
            copy_file(fs, path, fs_path)
            copied_files += 1

    return copied_files


def copy_tree_cores_filtered(fs, src, dest, active_systems, sd_cores_pack):
    """Copy only core blobs referenced by non-empty roms/<system>/ trees."""
    copied_files = 0
    for path in sorted(src.rglob("*")):
        if path.name == ".DS_Store":
            continue
        rel_path = path.relative_to(src).as_posix()
        if path.is_dir():
            continue
        if not sd_cores_pack.core_relative_path_allowed(rel_path, active_systems):
            continue
        fs_path = "/" + dest if not rel_path else "/" + dest + "/" + rel_path
        copy_file(fs, path, fs_path)
        copied_files += 1
    return copied_files


def main():
    parser = argparse.ArgumentParser(description="Generate a LittleFS image from sd_content.")
    parser.add_argument("--sd-content", default="sd_content", help="Source sd_content directory")
    parser.add_argument("--output", default="build/littlefs.bin", help="Output LittleFS image")
    parser.add_argument("--build-dir", default="build/littlefs", help="LittleFS build/cache directory")
    parser.add_argument("--size", type=parse_int, required=True, help="LittleFS partition size in bytes")
    parser.add_argument("--block-size", type=parse_int, default=4096, help="LittleFS block/erase size in bytes")
    parser.add_argument("--read-size", type=parse_int, default=256, help="LittleFS read size in bytes")
    parser.add_argument("--prog-size", type=parse_int, default=256, help="LittleFS program size in bytes")
    parser.add_argument("--cache-size", type=parse_int, default=256, help="LittleFS cache size in bytes")
    parser.add_argument("--lookahead-size", type=parse_int, default=16, help="LittleFS lookahead size in bytes")
    parser.add_argument("--block-cycles", type=int, default=500, help="LittleFS block_cycles value")
    parser.add_argument(
        "--include",
        action="append",
        default=[],
        help="Top-level sd_content directory to include. May be repeated. Defaults to cores.",
    )
    parser.add_argument(
        "--roms-dir",
        default="roms",
        help="Project ROM tree (merged with sd_content/roms) used to pick which cores to pack",
    )
    parser.add_argument(
        "--no-cores-filter",
        action="store_true",
        help="Copy the entire sd_content/cores tree (disable ROM-based selection).",
    )
    parser.add_argument(
        "--refresh-pico8-cores",
        action="store_true",
        help="Force re-download of the PICO-8 GNW cores ZIP when packing for pico8.",
    )
    parser.add_argument(
        "--pico8-cores-url",
        default=None,
        help="Override URL for the PICO-8 GNW cores release ZIP (default: Macs75 v.1.1.4b bundle).",
    )
    parser.add_argument(
        "--omit-pico8-ro-from-gnw-zip",
        action="store_true",
        help="Do not copy pico8.ro from the GNW ZIP (e.g. when it is bundled in FrogFS).",
    )
    args = parser.parse_args()

    if args.size <= 0:
        print("LittleFS image size must be greater than zero", file=sys.stderr)
        return 1
    if args.size % args.block_size:
        print("LittleFS image size must be a multiple of the block size", file=sys.stderr)
        return 1

    repo = pathlib.Path(__file__).resolve().parents[1]
    sd_content = (repo / args.sd_content).resolve()
    output = (repo / args.output).resolve()
    build_dir = (repo / args.build_dir).resolve()

    if not sd_content.is_dir():
        print(f"sd_content directory not found: {sd_content}", file=sys.stderr)
        return 1

    requested_dirs = tuple(args.include) if args.include else DEFAULT_DIRS
    collect_dirs = []
    for dirname in requested_dirs:
        src = sd_content / dirname
        if src.is_dir():
            collect_dirs.append((src, dirname))

    if not collect_dirs:
        print(f"No LittleFS input directories found in {sd_content}", file=sys.stderr)
        return 1

    scripts_dir = str(repo / "scripts")
    if scripts_dir not in sys.path:
        sys.path.insert(0, scripts_dir)
    import sd_cores_pack  # noqa: E402
    import pico8_gnw_cores  # noqa: E402

    project_roms = (repo / args.roms_dir).resolve()
    sd_roms_tree = sd_content / "roms"
    active_systems = sd_cores_pack.active_system_dirnames_from_roms_trees(
        project_roms if project_roms.is_dir() else None,
        sd_roms_tree if sd_roms_tree.is_dir() else None,
    )

    build_dir.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)

    linear_image = build_dir / (output.name + ".linear")
    write_erased_image(linear_image, args.size)

    context = UserContextFile(str(linear_image))
    fs = LittleFS(
        context=context,
        mount=False,
        block_size=args.block_size,
        block_count=args.size // args.block_size,
        read_size=args.read_size,
        prog_size=args.prog_size,
        cache_size=args.cache_size,
        lookahead_size=args.lookahead_size,
        block_cycles=args.block_cycles,
    )

    copied_files = 0
    try:
        fs.format()
        fs.mount()
        for src, dest in collect_dirs:
            if dest == "cores" and not args.no_cores_filter:
                n = copy_tree_cores_filtered(
                    fs,
                    src,
                    dest,
                    active_systems,
                    sd_cores_pack,
                )
                copied_files += n
                sys_msg = ", ".join(sorted(active_systems)) if active_systems else "(no ROM folders)"
                print(f"LittleFS /cores: packed for systems [{sys_msg}] ({n} files)")
            else:
                excl = (
                    sd_cores_pack.LITTLEFS_EXCLUDE_CORE_RELPATHS
                    if dest == "cores"
                    else frozenset()
                )
                copied_files += copy_tree(fs, src, dest, exclude_relpaths=excl)
        if "pico8" in active_systems:
            url = args.pico8_cores_url or pico8_gnw_cores.PICO8_GNW_CORES_ZIP_URL
            omit = frozenset({"pico8.ro"}) if args.omit_pico8_ro_from_gnw_zip else frozenset()
            n = pico8_gnw_cores.fetch_and_merge_into_littlefs(
                fs,
                build_dir,
                url,
                force_refresh=args.refresh_pico8_cores,
                omit_basenames=omit,
            )
            copied_files += n
        fs.unmount()
    except Exception:
        try:
            fs.unmount()
        except Exception:
            pass
        raise

    reverse_blocks(linear_image, output, args.block_size)

    size = output.stat().st_size
    dirs = ", ".join("/" + dest for _, dest in collect_dirs)
    print(f"Generated {output} ({size} bytes) from {dirs}, {copied_files} file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
