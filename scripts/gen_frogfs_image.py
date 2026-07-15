#!/usr/bin/env python3
import argparse
import fnmatch
import hashlib
import importlib.util
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

# Must match external/zelda3/tables/util.py ZELDA3_SHA1_US (US "A Link to the Past").
ZELDA3_US_SHA1 = "6D4F10A8B10E10DBE624CB23CF03B88BB8252973"
# Must match external/smw/assets/util.py SMW_SHA1_US.
SMW_US_SHA1 = "6B47BB75D16514B6A476AA0C73A683A2A4C18765"

FROGFS_HOMEBREW_EXTRA = "frogfs_homebrew_extra"
# Only pack these from restool staging (frogfs_homebrew_extra); ignore copies under project/sd roms trees
# when the matching US ROM was not found (prepare_* returned False).
ROMS_HOMEBREW_RESTOOL_DATS = frozenset(
    (
        pathlib.Path("homebrew") / "zelda3_assets.dat",
        pathlib.Path("homebrew") / "smw_assets.dat",
    )
)
# GNW homebrew payloads tied to restool-built assets; omit from FrogFS if US ROM → assets build skipped.
ROMS_HOMEBREW_SMW_WORLD_BIN = pathlib.Path("homebrew") / "Super Mario World.bin"
ROMS_HOMEBREW_ZELDA3_BIN = pathlib.Path("homebrew") / "Zelda 3.bin"
ROMS_HOMEBREW_ZELDA3_RO = pathlib.Path("homebrew") / "zelda3.ro"


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


def load_frogfsignore_rels(sd_content: pathlib.Path) -> frozenset:
    """Exact roms-relative paths matched by <sd_content>/.frogfsignore.

    One fnmatch pattern per line, relative to the roms/ tree (e.g.
    "homebrew/ff4-j2e.sfc"); '#' comments and blank lines are skipped.
    Lets multi-MiB assets that only make sense on the SD card (pre-patched
    ROM variants, FF4 ADR-008) live in sd_content without blowing the
    FrogFS budget of SD_CARD=0 builds -- the SD sync flow still ships them.
    """
    ign = sd_content / ".frogfsignore"
    if not ign.is_file():
        return frozenset()
    pats = [
        line.strip()
        for line in ign.read_text().splitlines()
        if line.strip() and not line.strip().startswith("#")
    ]
    roms_root = sd_content / "roms"
    if not pats or not roms_root.is_dir():
        return frozenset()
    out = set()
    for p in roms_root.rglob("*"):
        if not p.is_file():
            continue
        rel = p.relative_to(roms_root)
        if any(fnmatch.fnmatch(rel.as_posix(), pat) for pat in pats):
            out.add(rel)
    return frozenset(out)


def is_pico8_cartridge_rom(rel_path: pathlib.Path, path: pathlib.Path) -> bool:
    """True if path should be packed as a PICO-8 game under /roms/pico8."""
    if not rel_path.parts:
        return False
    if rel_path.parts[0].lower() != "pico8":
        return False
    name = path.name.lower()
    return name.endswith(".p8") or name.endswith(".p8.png") or name.endswith(".png")


def snes_rom_body_sha1(rom_path: pathlib.Path) -> str:
    """SHA1 of PRG (SMC 512-byte header strip; same as zelda3/smw util.py)."""
    data = rom_path.read_bytes()
    if (len(data) & 0xFFFFF) == 0x200:
        data = data[0x200:]
    return hashlib.sha1(data).hexdigest().upper()


def homebrew_rels_with_sha1(sha1_expect: str, *roms_roots: pathlib.Path) -> frozenset[pathlib.Path]:
    """Relative paths under each roms root (e.g. homebrew/x.sfc) whose body matches sha1_expect."""
    found: set[pathlib.Path] = set()
    for root in roms_roots:
        if not root.is_dir():
            continue
        hb = root / "homebrew"
        if not hb.is_dir():
            continue
        for p in hb.iterdir():
            if not p.is_file() or p.suffix.lower() not in (".sfc", ".smc"):
                continue
            if snes_rom_body_sha1(p) != sha1_expect:
                continue
            found.add(p.resolve().relative_to(root.resolve()))
    return frozenset(found)


def homebrew_rels_with_sha1_in(
    sha1_expect: frozenset[str], *roms_roots: pathlib.Path
) -> frozenset[pathlib.Path]:
    """Relative paths under each roms root whose file body SHA1 is in sha1_expect."""
    found: set[pathlib.Path] = set()
    for root in roms_roots:
        if not root.is_dir():
            continue
        hb = root / "homebrew"
        if not hb.is_dir():
            continue
        for p in hb.iterdir():
            if not p.is_file() or p.suffix.lower() not in (".sfc", ".smc"):
                continue
            if snes_rom_body_sha1(p) not in sha1_expect:
                continue
            found.add(p.resolve().relative_to(root.resolve()))
    return frozenset(found)


def load_zelda3_sha1_to_lang(tables_dir: pathlib.Path) -> dict[str, str]:
    """Uppercase SHA1 → restool language code; kept in sync by loading util.py."""
    util_path = tables_dir / "util.py"
    spec = importlib.util.spec_from_file_location("zelda3_tables_util", str(util_path))
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot load {util_path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return {k.upper(): v[0] for k, v in mod.ZELDA3_SHA1.items()}


def collect_zelda3_alt_lang_roms(
    sha1_to_lang: dict[str, str],
    *roms_roots: pathlib.Path,
) -> dict[str, pathlib.Path]:
    """
    One ROM path per non-US language code found under roms/homebrew (first match wins per lang).
    SHA1 map must use uppercase keys (see load_zelda3_sha1_to_lang).
    """
    by_lang: dict[str, pathlib.Path] = {}
    seen_resolved: set[pathlib.Path] = set()
    for roms_root in roms_roots:
        if not roms_root.is_dir():
            continue
        hb = roms_root / "homebrew"
        if not hb.is_dir():
            continue
        for p in sorted(
            x for x in hb.iterdir() if x.is_file() and x.suffix.lower() in (".sfc", ".smc")
        ):
            r = p.resolve()
            if r in seen_resolved:
                continue
            seen_resolved.add(r)
            lang = sha1_to_lang.get(snes_rom_body_sha1(p))
            if not lang or lang == "us":
                continue
            if lang not in by_lang:
                by_lang[lang] = p
    return by_lang


def collect_us_zelda3_rom_from_homebrew(
    *roms_roots: pathlib.Path,
) -> pathlib.Path | None:
    """
    Search roms/homebrew under each existing roms root (same layout as SD: /roms/homebrew).
    Returns absolute path to first US ALTTP ROM found, or None.
    """
    candidates_order: list[pathlib.Path] = []
    seen_resolved: set[pathlib.Path] = set()

    def add_candidate(file_path: pathlib.Path) -> None:
        r = file_path.resolve()
        if r in seen_resolved:
            return
        seen_resolved.add(r)
        candidates_order.append(file_path)

    for roms_root in roms_roots:
        if not roms_root.is_dir():
            continue
        hb = roms_root / "homebrew"
        if not hb.is_dir():
            continue
        for name in ("zelda3.sfc", "zelda3.smc"):
            p = hb / name
            if p.is_file():
                add_candidate(p)
        for p in sorted(
            x for x in hb.iterdir() if x.is_file() and x.suffix.lower() in (".sfc", ".smc")
        ):
            add_candidate(p)

    for rom_path in candidates_order:
        if snes_rom_body_sha1(rom_path) == ZELDA3_US_SHA1:
            return rom_path

    if candidates_order:
        ex = candidates_order[0]
        print(
            f"roms/homebrew: no US ALTTP ROM (SHA1 {ZELDA3_US_SHA1}). "
            f"Checked {len(candidates_order)} file(s); e.g. {ex.name} → {snes_rom_body_sha1(ex)}.",
            file=sys.stderr,
        )
    return None


def collect_us_smw_rom_from_homebrew(
    *roms_roots: pathlib.Path,
) -> pathlib.Path | None:
    """
    Search roms/homebrew under each existing roms root.
    Returns absolute path to first US Super Mario World ROM found, or None.
    """
    candidates_order: list[pathlib.Path] = []
    seen_resolved: set[pathlib.Path] = set()

    def add_candidate(file_path: pathlib.Path) -> None:
        r = file_path.resolve()
        if r in seen_resolved:
            return
        seen_resolved.add(r)
        candidates_order.append(file_path)

    for roms_root in roms_roots:
        if not roms_root.is_dir():
            continue
        hb = roms_root / "homebrew"
        if not hb.is_dir():
            continue
        for name in ("smw.sfc", "smw.smc"):
            p = hb / name
            if p.is_file():
                add_candidate(p)
        for p in sorted(
            x for x in hb.iterdir() if x.is_file() and x.suffix.lower() in (".sfc", ".smc")
        ):
            add_candidate(p)

    for rom_path in candidates_order:
        if snes_rom_body_sha1(rom_path) == SMW_US_SHA1:
            return rom_path

    if candidates_order:
        ex = candidates_order[0]
        print(
            f"roms/homebrew: no US SMW ROM (SHA1 {SMW_US_SHA1}). "
            f"Checked {len(candidates_order)} file(s); e.g. {ex.name} → {snes_rom_body_sha1(ex)}.",
            file=sys.stderr,
        )
    return None


def prepare_zelda3_frogfs_assets(
    repo: pathlib.Path,
    build_dir: pathlib.Path,
    project_roms: pathlib.Path,
    sd_roms: pathlib.Path,
) -> tuple[bool, frozenset[pathlib.Path]]:
    """
    If roms/homebrew contains a US ALTTP ROM (expected SHA1), build zelda3_assets.dat via restool
    and stage it for FrogFS at /roms/homebrew/zelda3_assets.dat (see main_zelda3.c).
    Additional localized ROMs listed in external/zelda3/tables/util.py under roms/homebrew trigger
    --extract-dialogue and --languages=... like a manual restool multi-language build.
    All recognized Zelda 3 ROM bodies are excluded from FrogFS packing.
    """
    rom_path = collect_us_zelda3_rom_from_homebrew(project_roms, sd_roms)
    if not rom_path:
        return False, frozenset()

    tables = repo / "external" / "zelda3" / "tables"
    restool = tables / "restool.py"
    if not restool.is_file():
        print(f"Zelda3 restool not found: {restool}", file=sys.stderr)
        return False, frozenset()

    try:
        sha1_to_lang = load_zelda3_sha1_to_lang(tables)
    except ImportError as e:
        print(str(e), file=sys.stderr)
        return False, frozenset()

    alt_by_lang = collect_zelda3_alt_lang_roms(sha1_to_lang, project_roms, sd_roms)
    for lang in sorted(alt_by_lang.keys()):
        alt_rom = alt_by_lang[lang]
        try:
            subprocess.check_call(
                [
                    sys.executable,
                    str(restool),
                    "--extract-dialogue",
                    "-r",
                    str(alt_rom.resolve()),
                ],
                cwd=str(tables),
            )
        except subprocess.CalledProcessError as e:
            print(
                f"zelda3 restool.py --extract-dialogue failed for {lang} ({alt_rom.name}), exit {e.returncode}",
                file=sys.stderr,
            )
            return False, frozenset()
        print(f"Zelda3: extracted dialogue for language '{lang}' from {alt_rom.name}")

    extract_cmd = [
        sys.executable,
        str(restool),
        "--extract-from-rom",
        "-r",
        str(rom_path.resolve()),
    ]
    if alt_by_lang:
        langs_csv = ",".join(sorted(alt_by_lang.keys()))
        extract_cmd.extend(["--languages", langs_csv])
        print(f"Zelda3: building assets with extra languages: {langs_csv}")

    try:
        subprocess.check_call(extract_cmd, cwd=str(tables))
    except subprocess.CalledProcessError as e:
        print(f"zelda3 restool.py failed (exit {e.returncode})", file=sys.stderr)
        return False, frozenset()

    dat = tables / "zelda3_assets.dat"
    if not dat.is_file():
        print(f"zelda3_assets.dat not produced at {dat}", file=sys.stderr)
        return False, frozenset()

    hb = build_dir / FROGFS_HOMEBREW_EXTRA / "homebrew"
    hb.mkdir(parents=True, exist_ok=True)
    shutil.copy2(dat, hb / "zelda3_assets.dat")
    print("Zelda3: built zelda3_assets.dat from US ROM in roms/homebrew → FrogFS /roms/homebrew/zelda3_assets.dat")
    skip = homebrew_rels_with_sha1_in(frozenset(sha1_to_lang.keys()), project_roms, sd_roms)
    if skip:
        listed = ", ".join(sorted(p.as_posix() for p in skip))
        print(f"Zelda3: excluding recognized Zelda 3 ROM(s) from FrogFS (not packed): {listed}")
    return True, skip


def prepare_smw_frogfs_assets(
    repo: pathlib.Path,
    build_dir: pathlib.Path,
    project_roms: pathlib.Path,
    sd_roms: pathlib.Path,
) -> tuple[bool, frozenset[pathlib.Path]]:
    """
    If roms/homebrew contains a US SMW ROM (expected SHA1), build smw_assets.dat via restool
    and stage it for FrogFS at /roms/homebrew/smw_assets.dat (see main_smw.c).
    The matched ROM file is not packed into FrogFS.
    """
    rom_path = collect_us_smw_rom_from_homebrew(project_roms, sd_roms)
    if not rom_path:
        return False, frozenset()

    smw_root = repo / "external" / "smw"
    restool = smw_root / "assets" / "restool.py"
    if not restool.is_file():
        print(f"SMW restool not found: {restool}", file=sys.stderr)
        return False, frozenset()

    try:
        subprocess.check_call(
            [sys.executable, str(restool), "--extract-from-rom", "-r", str(rom_path.resolve())],
            cwd=str(smw_root),
        )
    except subprocess.CalledProcessError as e:
        print(f"smw assets/restool.py failed (exit {e.returncode})", file=sys.stderr)
        return False, frozenset()

    dat = smw_root / "smw_assets.dat"
    if not dat.is_file():
        print(f"smw_assets.dat not produced at {dat}", file=sys.stderr)
        return False, frozenset()

    hb = build_dir / FROGFS_HOMEBREW_EXTRA / "homebrew"
    hb.mkdir(parents=True, exist_ok=True)
    shutil.copy2(dat, hb / "smw_assets.dat")
    print("SMW: built smw_assets.dat from US ROM in roms/homebrew → FrogFS /roms/homebrew/smw_assets.dat")
    skip = homebrew_rels_with_sha1(SMW_US_SHA1, project_roms, sd_roms)
    if skip:
        listed = ", ".join(sorted(p.as_posix() for p in skip))
        print(f"SMW: excluding US ROM from FrogFS (not packed): {listed}")
    return True, skip


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


def _roms_homebrew_zelda_smw_skip(
    rel_path: pathlib.Path, *, zelda3_built: bool, smw_built: bool
) -> bool:
    """True if this roms-relative path must not be packed (orphan homebrew file without assets)."""
    if rel_path == ROMS_HOMEBREW_SMW_WORLD_BIN and not smw_built:
        return True
    if rel_path == ROMS_HOMEBREW_ZELDA3_BIN and not zelda3_built:
        return True
    if rel_path == ROMS_HOMEBREW_ZELDA3_RO and not zelda3_built:
        return True
    return False


def stage_input_dirs(
    collect_dirs,
    stage_dir,
    roms_exclude_top=None,
    roms_skip_rel_paths=None,
    *,
    skip_bios_msx=False,
    frogfs_homebrew_dat_root: pathlib.Path | None = None,
    zelda3_frogfs_built: bool = False,
    smw_frogfs_built: bool = False,
):
    """Merge multiple (src, dest) with the same dest into one staged tree.

    frogfs_homebrew_dat_root: when set, only this source dir may contribute
    homebrew/zelda3_assets.dat and homebrew/smw_assets.dat (restool output).
    zelda3_frogfs_built / smw_frogfs_built: gate homebrew/Zelda3*.bin|ro and Super Mario World.bin.
    """
    if stage_dir.exists():
        shutil.rmtree(stage_dir)
    stage_dir.mkdir(parents=True)

    roms_exclude_top = roms_exclude_top or frozenset()
    roms_skip_rel_paths = roms_skip_rel_paths or frozenset()
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
                if dest == "roms" and rel_path in ROMS_HOMEBREW_RESTOOL_DATS:
                    allow = (
                        frogfs_homebrew_dat_root is not None
                        and src.resolve() == frogfs_homebrew_dat_root.resolve()
                    )
                    if not allow:
                        continue
                if dest == "roms" and _roms_homebrew_zelda_smw_skip(
                    rel_path,
                    zelda3_built=zelda3_frogfs_built,
                    smw_built=smw_frogfs_built,
                ):
                    continue
                staged_path = staged_root / rel_path
                if path.is_dir():
                    staged_path.mkdir(parents=True, exist_ok=True)
                elif path.is_file():
                    if dest == "roms" and rel_path in roms_skip_rel_paths:
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

    frogfs_hb_extra = build_dir / FROGFS_HOMEBREW_EXTRA
    if frogfs_hb_extra.exists():
        shutil.rmtree(frogfs_hb_extra)

    zelda3_frogfs_built, zelda3_rom_skip_rels = prepare_zelda3_frogfs_assets(
        repo, build_dir, project_roms, sd_roms
    )
    smw_frogfs_built, smw_rom_skip_rels = prepare_smw_frogfs_assets(
        repo, build_dir, project_roms, sd_roms
    )

    if "covers" in requested_dirs and not args.no_gencovers:
        if not run_gencovers(repo, build_dir, (project_roms, sd_roms)):
            return 1

    if project_roms.is_dir():
        collect_dirs.append((project_roms, "roms"))

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

    if zelda3_frogfs_built or smw_frogfs_built:
        collect_dirs.append((build_dir / FROGFS_HOMEBREW_EXTRA, "roms"))

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

    roms_skip_merged = load_frogfsignore_rels(sd_content)
    if roms_skip_merged:
        print(
            "frogfs: .frogfsignore skips "
            + ", ".join(sorted(str(p) for p in roms_skip_merged)),
            file=sys.stderr,
        )
    if zelda3_frogfs_built:
        roms_skip_merged = roms_skip_merged | zelda3_rom_skip_rels
    if smw_frogfs_built:
        roms_skip_merged = roms_skip_merged | smw_rom_skip_rels

    skip_bios_msx = "msx" not in active_systems
    if skip_bios_msx:
        print(
            "frogfs: omitting bios/msx (no roms/msx tree with game files)",
            file=sys.stderr,
        )

    hb_dat_root = (
        (build_dir / FROGFS_HOMEBREW_EXTRA)
        if (zelda3_frogfs_built or smw_frogfs_built)
        and (build_dir / FROGFS_HOMEBREW_EXTRA).is_dir()
        else None
    )
    staged_dirs, byteswapped_count = stage_input_dirs(
        collect_dirs,
        build_dir / "input",
        roms_exclude_top=frozenset(roms_exclude),
        roms_skip_rel_paths=roms_skip_merged,
        skip_bios_msx=skip_bios_msx,
        frogfs_homebrew_dat_root=hb_dat_root,
        zelda3_frogfs_built=zelda3_frogfs_built,
        smw_frogfs_built=smw_frogfs_built,
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
