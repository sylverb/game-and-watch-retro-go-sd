#!/usr/bin/env python3
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
from collections import OrderedDict


DEFAULT_DIRS = ("bios", "covers", "fonts", "roms")
# Under /roms, do not duplicate trees that are merged into /bios (routing uses /bios for FrogFS).
ROMS_TOP_EXCLUDE_FOR_BIOS_MERGE = frozenset({"bios"})
# Artifacts / compressed ROMs not packed into FrogFS /roms (covers live under /covers).
ROMS_SKIP_EXTENSIONS = frozenset({".img", ".jpg", ".jpeg", ".png", ".bmp"})
# Thumbnails from tools/gencovers.py (--dst); merged into FrogFS /covers (not repo ./covers).
GENERATED_COVERS_SUBDIR = "covers_from_roms"

def parse_int(value):
    return int(str(value), 0)


def is_md_rom(dest, rel_path):
    return dest == "roms" and len(rel_path.parts) > 1 and rel_path.parts[0].lower() == "md" and rel_path.name != ".DS_Store"


def skip_under_roms_top(rel_path, exclude_top):
    if not exclude_top or not rel_path.parts:
        return False
    return rel_path.parts[0].lower() in exclude_top


def skip_roms_file_by_extension(dest, path, rel_path):
    if dest != "roms" or not path.is_file():
        return False
    # PNG exports / .p8.png carts use suffix ".png", which is normally skipped for /roms.
    if is_pico8_cartridge_rom(rel_path, path):
        return False
    return path.suffix.lower() in ROMS_SKIP_EXTENSIONS


def is_pico8_cartridge_rom(rel_path: pathlib.Path, path: pathlib.Path) -> bool:
    """True if path should be packed as a PICO-8 game under /roms/pico8."""
    if not rel_path.parts:
        return False
    if rel_path.parts[0].lower() != "pico8":
        return False
    name = path.name.lower()
    return name.endswith(".p8") or name.endswith(".p8.png") or name.endswith(".png")



def run_gencovers(repo: pathlib.Path, build_dir: pathlib.Path, rom_dirs):
    """Run tools/gencovers.py for each distinct rom tree into build_dir / GENERATED_COVERS_SUBDIR."""
    tool = repo / "tools" / "gencovers.py"
    if not tool.is_file():
        print(f"gencovers.py not found: {tool}", file=sys.stderr)
        return False

    dst = build_dir / GENERATED_COVERS_SUBDIR
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True, exist_ok=True)

    seen = set()
    for rom_root in rom_dirs:
        if not rom_root or not rom_root.is_dir():
            continue
        resolved = rom_root.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        subprocess.check_call(
            [sys.executable, str(tool), "--src", str(rom_root), "--dst", str(dst)],
            cwd=str(repo),
        )
    return True


def link_or_copy(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        dst.unlink()
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def copy_byteswapped_16(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    with src.open("rb") as in_file, dst.open("wb") as out_file:
        pending = b""
        while True:
            chunk = in_file.read(1024 * 1024)
            if not chunk:
                break

            if pending:
                chunk = pending + chunk
                pending = b""

            if len(chunk) & 1:
                pending = chunk[-1:]
                chunk = chunk[:-1]

            data = bytearray(chunk)
            data[0::2], data[1::2] = data[1::2], data[0::2]
            out_file.write(data)

        if pending:
            out_file.write(pending)
    shutil.copystat(src, dst)




def stage_input_dirs(
    collect_dirs,
    stage_dir,
    roms_exclude_top=None,
    roms_skip_rel_paths=None,
    *,
    skip_bios_msx=False,
):
    """Merge multiple (src, dest) with the same dest into one staged tree."""
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    stage_dir.mkdir(parents=True)

    roms_exclude_top = roms_exclude_top or frozenset()
    roms_skip_rel_paths = roms_skip_rel_paths or frozenset()
    # SHA1-matched source ROMs under roms/homebrew → also skip when packing /homebrews.
    homebrews_skip_rels = frozenset(
        p.relative_to(pathlib.Path("homebrew"))
        for p in roms_skip_rel_paths
        if len(p.parts) >= 2 and p.parts[0] == "homebrew"
    )
    by_dest = OrderedDict()
    for src, dest in collect_dirs:
        by_dest.setdefault(dest, []).append(src)

    staged_dirs = []
    byteswapped_count = 0
    for dest, sources in by_dest.items():
        staged_root = stage_dir / dest
        staged_root.mkdir(parents=True, exist_ok=True)
        staged_dirs.append((staged_root, dest))

        for src in sources:
            for path in src.rglob("*"):
                rel_path = path.relative_to(src)
                if (
                    skip_bios_msx
                    and dest == "bios"
                    and rel_path.parts
                    and rel_path.parts[0].lower() == "msx"
                ):
                    continue
                if dest == "roms" and skip_under_roms_top(rel_path, roms_exclude_top):
                    continue
                # Do not also pack roms/homebrew/* into /roms — those go to /homebrews.
                if dest == "roms" and rel_path.parts and rel_path.parts[0] == "homebrew":
                    continue
                staged_path = staged_root / rel_path
                if path.is_dir():
                    staged_path.mkdir(parents=True, exist_ok=True)
                elif path.is_file():
                    if dest == "roms" and rel_path in roms_skip_rel_paths:
                        continue
                    if dest == "homebrews" and rel_path in homebrews_skip_rels:
                        continue
                    # Do not pack raw SNES ROMs into /homebrews (homebrew payloads only).
                    if dest == "homebrews" and path.suffix.lower() in (
                        ".sfc",
                        ".smc",
                        ".fig",
                        ".swc",
                    ):
                        continue
                    if skip_roms_file_by_extension(dest, path, rel_path):
                        continue
                    if is_md_rom(dest, rel_path):
                        copy_byteswapped_16(path, staged_path)
                        byteswapped_count += 1
                    else:
                        link_or_copy(path, staged_path)

    return staged_dirs, byteswapped_count


def main():
    parser = argparse.ArgumentParser(description="Generate a FrogFS image from sd_content.")
    parser.add_argument("--sd-content", default="sd_content", help="Source sd_content directory")
    parser.add_argument("--output", default="build/frogfs.bin", help="Output FrogFS image")
    parser.add_argument("--build-dir", default="build/frogfs", help="FrogFS build/cache directory")
    parser.add_argument(
        "--include",
        action="append",
        default=[],
        help=(
            "Top-level sd_content directory to include. May be repeated. "
            "Defaults to bios, covers, fonts/font and roms; roms/bios is merged into /bios. "
            "/covers is built from tools/gencovers.py (roms + sd_content/roms images) into "
            "build-dir/covers_from_roms, then sd_content/covers overlays."
        ),
    )
    parser.add_argument("--roms-dir", default="roms", help="Project root ROM directory to merge into /roms when present")
    parser.add_argument("--reserve-size", type=parse_int, default=0, help="Optional maximum reserved size in bytes")
    parser.add_argument(
        "--no-gencovers",
        action="store_true",
        help="Do not run tools/gencovers.py; only sd_content/covers is used for /covers (if any).",
    )
    parser.add_argument(
        "--no-covers",
        action="store_true",
        help="Omit the /covers tree from FrogFS (skip gencovers and sd_content/covers). "
        "Use when firmware is built with COVERFLOW!=1.",
    )
    parser.add_argument(
        "--bundle-pico8-ro-in-frogfs",
        action="store_true",
        help="When roms/pico8 is active: add pico8.ro from GNW ZIP to FrogFS /cores and post-patch XiP sentinels.",
    )
    parser.add_argument(
        "--refresh-pico8-cores",
        action="store_true",
        help="Force re-download of PICO-8 GNW ZIP when using --bundle-pico8-ro-in-frogfs.",
    )
    parser.add_argument(
        "--pico8-cores-url",
        default=None,
        help="Override URL for PICO-8 GNW cores ZIP.",
    )
    parser.add_argument(
        "--extflash-base",
        type=parse_int,
        default=0x90000000,
        help="QSPI mmap base (__EXTFLASH_BASE__) for pico8.ro post-patch.",
    )
    parser.add_argument(
        "--rom-compression",
        choices=("none", "lzma"),
        default="none",
        help="Pre-compress ROM payloads under /roms in the FrogFS staging tree (SD_CARD=0 build; "
        "same LZMA formats as legacy parse_roms.py). Use with firmware built without GNW_DISABLE_COMPRESSION.",
    )
    parser.add_argument(
        "--compress-gb-speed",
        action="store_true",
        help="Game Boy: selective bank compression (legacy --compress_gb_speed).",
    )
    parser.add_argument(
        "--extflash-offset",
        type=parse_int,
        default=0,
        help="Byte offset of Retro-Go region (__EXTFLASH_OFFSET__).",
    )
    args = parser.parse_args()

    repo = pathlib.Path(__file__).resolve().parents[1]
    sd_content = (repo / args.sd_content).resolve()
    output = (repo / args.output).resolve()
    build_dir = (repo / args.build_dir).resolve()
    mkfrogfs = repo / "Core/Src/porting/lib/frogfs/tools/mkfrogfs.py"

    if not mkfrogfs.exists():
        print(f"FrogFS tool not found: {mkfrogfs}", file=sys.stderr)
        return 1

    if not sd_content.is_dir():
        print(f"sd_content directory not found: {sd_content}", file=sys.stderr)
        return 1

    build_dir.mkdir(parents=True, exist_ok=True)

    requested_dirs = tuple(args.include) if args.include else DEFAULT_DIRS
    if args.no_covers:
        requested_dirs = tuple(d for d in requested_dirs if d != "covers")
    collect_dirs = []

    project_roms = (repo / args.roms_dir).resolve()
    sd_roms = sd_content / "roms"

    scripts_dir = str(repo / "scripts")
    if scripts_dir not in sys.path:
        sys.path.insert(0, scripts_dir)
    import sd_cores_pack  # noqa: E402

    active_systems = sd_cores_pack.active_system_dirnames_from_roms_trees(
        project_roms if project_roms.is_dir() else None,
        sd_roms if sd_roms.is_dir() else None,
    )

    if "covers" in requested_dirs and not args.no_gencovers:
        if not run_gencovers(repo, build_dir, (project_roms, sd_roms)):
            return 1

    if project_roms.is_dir():
        collect_dirs.append((project_roms, "roms"))

    # /homebrews: GWHB .bin (+ sibling assets). May stage from project roms/homebrew/.
    proj_hb = project_roms / "homebrew"
    if proj_hb.is_dir():
        collect_dirs.append((proj_hb, "homebrews"))
    sd_hb = sd_content / "homebrews"
    if sd_hb.is_dir():
        collect_dirs.append((sd_hb, "homebrews"))
    elif (sd_roms / "homebrew").is_dir():
        collect_dirs.append((sd_roms / "homebrew", "homebrews"))

    # /bios: sd_content/bios + sd_content/roms/bios + project roms/bios (FrogFS path is /bios, not /roms/bios).
    bios_sources = []
    sd_bios = sd_content / "bios"
    if sd_bios.is_dir():
        bios_sources.append(sd_bios)
    sd_roms_bios = sd_content / "roms" / "bios"
    if sd_roms_bios.is_dir():
        bios_sources.append(sd_roms_bios)
    if project_roms.is_dir():
        proj_bios = project_roms / "bios"
        if proj_bios.is_dir() and proj_bios not in bios_sources:
            bios_sources.append(proj_bios)
    for bsrc in bios_sources:
        collect_dirs.append((bsrc, "bios"))

    # /covers: tools/gencovers.py output under build_dir, then sd_content/covers overlays.
    if "covers" in requested_dirs:
        covers_sources = []
        if not args.no_gencovers:
            generated_covers = build_dir / GENERATED_COVERS_SUBDIR
            if generated_covers.is_dir():
                covers_sources.append(generated_covers)
        sd_covers = sd_content / "covers"
        if sd_covers.is_dir():
            covers_sources.append(sd_covers)
        for csrc in covers_sources:
            collect_dirs.append((csrc, "covers"))

    for dirname in requested_dirs:
        if dirname in ("bios", "roms", "covers"):
            continue
        src = sd_content / dirname
        if src.is_dir():
            collect_dirs.append((src, dirname))

    if sd_roms.is_dir():
        collect_dirs.append((sd_roms, "roms"))

    pico8_ro_in_frogfs = False
    if args.bundle_pico8_ro_in_frogfs and "pico8" in active_systems:
        import pico8_gnw_cores  # noqa: E402

        cache = build_dir / pico8_gnw_cores.PICO8_CACHE_FILENAME
        url = args.pico8_cores_url or pico8_gnw_cores.PICO8_GNW_CORES_ZIP_URL
        zip_bytes = pico8_gnw_cores.fetch_zip_bytes(
            cache, url, force_refresh=args.refresh_pico8_cores
        )
        st = build_dir / "frogfs_pico8_staging"
        if st.exists():
            shutil.rmtree(st)
        st.mkdir(parents=True)
        if not pico8_gnw_cores.extract_pico8_ro_from_zip_bytes(zip_bytes, st / "pico8.ro"):
            print("frogfs: pico8.ro missing from GNW ZIP", file=sys.stderr)
            return 1
        collect_dirs.append((st, "cores"))
        pico8_ro_in_frogfs = True
    elif args.bundle_pico8_ro_in_frogfs:
        print(
            "frogfs: --bundle-pico8-ro-in-frogfs ignored (no pico8 ROM folder detected)",
            file=sys.stderr,
        )

    if not collect_dirs:
        print(f"No FrogFS input directories found in {sd_content}", file=sys.stderr)
        return 1

    output.parent.mkdir(parents=True, exist_ok=True)

    roms_exclude = set(ROMS_TOP_EXCLUDE_FOR_BIOS_MERGE)

    roms_skip_merged = frozenset()

    skip_bios_msx = "msx" not in active_systems
    if skip_bios_msx:
        print(
            "frogfs: omitting bios/msx (no roms/msx tree with game files)",
            file=sys.stderr,
        )

    staged_dirs, byteswapped_count = stage_input_dirs(
        collect_dirs,
        build_dir / "input",
        roms_exclude_top=frozenset(roms_exclude),
        roms_skip_rel_paths=roms_skip_merged,
        skip_bios_msx=skip_bios_msx,
    )

    if args.rom_compression == "lzma":
        import rom_frogfs_lzma  # noqa: E402

        rom_staging = build_dir / "input" / "roms"
        rom_frogfs_lzma.pack_staged_roms(
            repo,
            rom_staging,
            compress_gb_speed=args.compress_gb_speed,
        )

    config_path = build_dir / "frogfs.yaml"
    with config_path.open("w", encoding="utf-8") as config:
        config.write("collect:\n")
        for src, dest in staged_dirs:
            source = str(src).replace(os.sep, "/") + "/"
            config.write(f"  - {json.dumps(source)}: {json.dumps('/' + dest)}\n")
        config.write("\nfilter:\n")
        config.write("  '*/.DS_Store':\n")
        config.write("    - discard\n")
        config.write("  '.DS_Store':\n")
        config.write("    - discard\n")
        if pico8_ro_in_frogfs:
            config.write("  'cores/pico8.ro':\n")
            config.write("    - no compress\n")

    cmd = [
        sys.executable,
        str(mkfrogfs),
        "-C",
        str(repo),
        str(config_path),
        str(build_dir),
        str(output),
    ]
    subprocess.check_call(cmd)

    if pico8_ro_in_frogfs:
        import frogfs_pico8_ro  # noqa: E402

        if not frogfs_pico8_ro.patch_frogfs_pico8_ro_inplace(
            output,
            extflash_base=args.extflash_base,
            extflash_offset=args.extflash_offset,
        ):
            return 1

    size = output.stat().st_size
    if args.reserve_size and size > args.reserve_size:
        print(
            f"FrogFS image is {size} bytes, larger than reserved size {args.reserve_size} bytes",
            file=sys.stderr,
        )
        return 1

    dirs = ", ".join("/" + dest for _, dest in staged_dirs)
    print(f"Generated {output} ({size} bytes) from {dirs}")
    if byteswapped_count:
        print(f"Byteswapped {byteswapped_count} file(s) under /roms/md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
