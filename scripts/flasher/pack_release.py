#!/usr/bin/env python3
"""
Pack a Retro-Go SD 2.0 release: eight zips plus three JSON files.

See docs/RELEASE_2_0.md — this script implements the format described there.

Cores and homebrew are decoupled and ship from their own projects. Nothing
under cores/, homebrews/, covers/ or cheats/ appears here; the firmware release
carries only the intflash image and the static content the launcher itself
needs (fonts, language blobs, the boot logo).

Per build (storage x bank, four of them):

    retro-go-sd-<tag>-<storage>-bank<n>.zip         image + content, self-contained
    retro-go-sd-<tag>-<storage>-bank<n>-debug.zip   the matching ELF

Plus, unzipped so a version picker reads metadata without fetching an archive:

    manifest.json    this release: builds, firmware identity, install paths
    versions.json    the index, newest first
    projects.json    the curated core/homebrew list (from gen_projects_json.py)

Language blobs are per-build on purpose: lang_t gains fields under
CHEAT_CODES=1 and INTFLASH_BANK=2, so a blob generated for one build has a
different field ORDER than another, and rg_i18n.c truncates a mismatched blob
rather than rejecting it. Fonts and the logo are build-independent but are
copied into each zip anyway (84 KB); a self-contained zip cannot be
mis-assembled.

Firmware identity is read out of the built binary, never copied from headers:
the point is to describe what actually shipped.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import struct
import sys
import zipfile

SCHEMA_VERSION = 1
PROJECT = "retro-go-sd"
TITLE = "Retro-Go SD"
DOCS = "https://github.com/slash-proc/game-and-watch-retro-go-sd#readme"

# Offset of the firmware ABI struct within the intflash image. Mirrors
# GW_FIRMWARE_ABI_OFFSET in Core/Inc/retro-go/gw_firmware_abi.h; the struct
# starts with two uint32_t, version then size.
ABI_OFFSET = 0x400

# GnwLayoutSuperblock, located by magic rather than by a fixed offset (the
# linker places it wherever the section lands). See gw_layout_superblock.h.
LAYOUT_MAGIC = b"GWLB"

# The /data/INSTALL marker the firmware writes at boot. Declared here so an
# installer can find and validate it without hardcoding any of it.
INSTALL_FILE = {"path": "data/INSTALL", "magic": "RGIN", "version": 1}

# Install directories this firmware uses. An installer reads these instead of
# hardcoding them, so a rename is a one-value change here.
PATHS = {
    "cores": "/cores",
    "homebrew": "/homebrews",
    "bios": "/bios",
    "roms": "/roms",
    "covers": "/covers",
    "cheats": "/cheats",
    "data": "/data",
}

# Subtrees of sd_content that belong in a firmware release. Everything else in
# there (cores/, homebrews/, covers/, cheats/) is either decoupled or user
# content, and must not ship with the firmware.
CONTENT_DIRS = ("fonts", "lang", "bios")

# GIT_TAG is baked as the string literal "Retro-Go SD <describe>" (see
# scripts/update_gittag.sh). This is the same pattern the web builder scans a
# device's flash for, so reading it here means the manifest and a device scan
# agree by construction. The whole string is captured, prefix included: the
# firmware stores it verbatim in /data/INSTALL (rg_install.c), so a tool can
# byte-compare the two with no normalisation on either side.
GITTAG_RE = re.compile(rb"(Retro-Go SD (?:v[0-9][\w.+\-]*|NOTAG))\x00")

# Feature flags that change what a user gets, mapped to the capability names an
# installer shows. Derived from the build flags rather than hand-written, so the
# manifest cannot drift from the build.
CAPABILITY_FLAGS = [
    ("COVERFLOW", "coverflow", "1"),
    ("CHEAT_CODES", "cheatCodes", "1"),
    ("ENABLE_SCREENSHOT", "screenshot", "1"),
    ("SHARED_HIBERNATE_SAVESTATE", "sharedHibernateSavestate", "1"),
]
# Defaults from Makefile.common, for flags a build did not pass explicitly.
CAPABILITY_DEFAULTS = {"ENABLE_SCREENSHOT": "1"}

LITTLEFS_BLOCK_SIZE_DEFAULT = 4096

# Fixed zip timestamp so the same inputs produce the same bytes.
ZIP_DATE = (1980, 1, 1, 0, 0, 0)


class PackError(Exception):
    pass


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def read_provides_abi(image_path):
    """Read {version, size} from the ABI struct at ABI_OFFSET in the image.

    Read from the binary, not from gw_firmware_abi.h: a core's compatibility
    check is `required_abi_min_size <= g_firmware_abi.size` against these exact
    bytes, so this is the only source that cannot disagree with the device.
    """
    with open(image_path, "rb") as f:
        f.seek(ABI_OFFSET)
        raw = f.read(8)
    if len(raw) != 8:
        raise PackError(f"{image_path}: too small to hold an ABI struct at {ABI_OFFSET:#x}")
    version, size = struct.unpack("<II", raw)
    # Bound-check the same way the web builder does, so a garbage read is caught
    # here instead of shipping a manifest that sends installers to a bad offset.
    if not (1 <= version <= 255):
        raise PackError(
            f"{image_path}: implausible ABI version {version} at {ABI_OFFSET:#x} "
            "— is the .firmware_abi section still pinned there?"
        )
    if not (8 <= size <= 16384):
        raise PackError(
            f"{image_path}: implausible ABI size {size} at {ABI_OFFSET:#x}"
        )
    return {"version": version, "size": size}


def read_superblock(image_path):
    """Locate GnwLayoutSuperblock by magic and read its version and struct_size.

    Returned so an installer can assert before patching: a GNW_LAYOUT_VERSION
    bump then fails loudly instead of silently writing the wrong field layout.

    The magic is also loaded as an immediate by the code that validates it, so
    the four bytes occur more than once in a real image (confirmed: a Thumb
    instruction stream matches too). Candidates are therefore filtered on the
    two fields that follow, which no code sequence plausibly satisfies.
    """
    with open(image_path, "rb") as f:
        blob = f.read()

    candidates = []
    idx = blob.find(LAYOUT_MAGIC)
    while idx >= 0:
        if idx + 8 <= len(blob):
            version, struct_size = struct.unpack_from("<HH", blob, idx + 4)
            # A real superblock is a small version and a struct size that both
            # fits the file and is a sane struct; 36 today, room to grow.
            if 1 <= version <= 255 and 16 <= struct_size <= 256:
                if idx + struct_size <= len(blob):
                    candidates.append((idx, version, struct_size))
        idx = blob.find(LAYOUT_MAGIC, idx + 1)

    if not candidates:
        raise PackError(
            f"{image_path}: no GWLB layout superblock found — "
            "gw_layout_superblock.c must be linked in"
        )
    if len(candidates) > 1:
        where = ", ".join(f"{off:#x}" for off, _, _ in candidates)
        raise PackError(
            f"{image_path}: {len(candidates)} plausible GWLB superblocks at {where}; "
            "cannot identify which one is g_layout_superblock"
        )

    _off, version, struct_size = candidates[0]
    return {"magic": LAYOUT_MAGIC.decode(), "version": version, "structSize": struct_size}


def read_git_tag(image_path):
    """Pull the baked GIT_TAG string out of the image."""
    with open(image_path, "rb") as f:
        blob = f.read()
    m = GITTAG_RE.search(blob)
    if not m:
        raise PackError(
            f"{image_path}: no 'Retro-Go SD <tag>' string found — "
            "GIT_TAG is not baked into this build"
        )
    return m.group(1).decode()


def read_core_meta_version(header_path):
    """GNW_CORE_META_VERSION from its header.

    Unlike providesAbi and the superblock this one is NOT recoverable from the
    binary — it exists only as a compile-time comparison — so the header is the
    only source available.
    """
    try:
        with open(header_path, encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        raise PackError(
            f"{header_path}: cannot read GNW_CORE_META_VERSION ({e.strerror}) "
            "— pass --core-meta-header if not running from the repo root"
        ) from None
    m = re.search(
        r"#define\s+GNW_CORE_META_VERSION\s*\(\(uint16_t\)\s*(\d+)u?\s*\)", text
    )
    if not m:
        raise PackError(f"{header_path}: could not find GNW_CORE_META_VERSION")
    return int(m.group(1))


def parse_flags(build_flags):
    """Split a make command line into a {VAR: value} dict."""
    out = {}
    for token in build_flags.split():
        if "=" in token:
            k, v = token.split("=", 1)
            out[k] = v
    return out


def capabilities_from_flags(flags):
    caps = []
    for var, name, on_value in CAPABILITY_FLAGS:
        value = flags.get(var, CAPABILITY_DEFAULTS.get(var, "0"))
        if value == on_value:
            caps.append(name)
    return caps


def collect_content(content_dir, storage, bank):
    """Return [(abs_path, arcname, install_path, language)] for one build.

    Only CONTENT_DIRS are taken. The SD update image is handled separately by
    the caller because it is not installed content — it is a file the on-device
    updater looks for by an exact name.
    """
    members = []
    for sub in CONTENT_DIRS:
        root_dir = os.path.join(content_dir, sub)
        if not os.path.isdir(root_dir):
            continue
        for root, dirs, files in os.walk(root_dir):
            dirs.sort()
            for fn in sorted(files):
                full = os.path.join(root, fn)
                rel = os.path.relpath(full, content_dir).replace(os.sep, "/")
                language = None
                if sub == "lang" and fn.endswith(".bin"):
                    language = fn[: -len(".bin")]
                members.append((full, rel, rel, language))
    if not members:
        raise PackError(
            f"{content_dir}: no content found under {CONTENT_DIRS} "
            "— did create_sd_data run?"
        )
    return members


def find_sd_update(content_dir, bank):
    """The update_bank<n>.bin the on-device updater looks for, if present."""
    name = f"update_bank{bank}.bin"
    path = os.path.join(content_dir, name)
    return (path, name) if os.path.isfile(path) else (None, name)


def add_file(zf, full, arcname):
    zi = zipfile.ZipInfo(arcname, date_time=ZIP_DATE)
    zi.compress_type = zipfile.ZIP_DEFLATED
    with open(full, "rb") as f:
        zf.writestr(zi, f.read())


def parse_build_arg(spec):
    """Parse `id=...,storage=...,bank=...,image=...,elf=...,content=...,flags=...`."""
    fields = {}
    # flags= holds a whole make command line and may contain commas in theory,
    # so it must be last; split on commas only up to that point.
    head, sep, flags = spec.partition(",flags=")
    for part in head.split(","):
        if not part:
            continue
        if "=" not in part:
            raise PackError(f"--build: expected key=value, got {part!r}")
        k, v = part.split("=", 1)
        fields[k.strip()] = v.strip()
    if sep:
        fields["flags"] = flags.strip()

    required = ("id", "storage", "bank", "image", "elf", "content")
    missing = [k for k in required if k not in fields]
    if missing:
        raise PackError(f"--build {fields.get('id', '?')}: missing {', '.join(missing)}")
    if fields["storage"] not in ("sd", "flash"):
        raise PackError(f"--build {fields['id']}: storage must be sd or flash")
    fields["bank"] = int(fields["bank"])
    if fields["bank"] not in (1, 2):
        raise PackError(f"--build {fields['id']}: bank must be 1 or 2")
    fields.setdefault("flags", "")
    for key in ("image", "elf"):
        if not os.path.isfile(fields[key]):
            raise PackError(f"--build {fields['id']}: {key} not found: {fields[key]}")
    if not os.path.isdir(fields["content"]):
        raise PackError(
            f"--build {fields['id']}: content dir not found: {fields['content']}"
        )
    return fields


def pack_build(build, tag, out_dir):
    """Write one build's two zips; return its manifest entry."""
    bid = build["id"]
    storage, bank = build["storage"], build["bank"]
    flags = parse_flags(build["flags"])

    bundle_name = f"{PROJECT}-{tag}-{bid}.zip"
    debug_name = f"{PROJECT}-{tag}-{bid}-debug.zip"
    bundle_path = os.path.join(out_dir, bundle_name)
    debug_path = os.path.join(out_dir, debug_name)

    image_arc = "gw_retro_go_intflash.bin"
    content = collect_content(build["content"], storage, bank)
    sd_update_path, sd_update_name = find_sd_update(build["content"], bank)

    if storage == "sd" and sd_update_path is None:
        raise PackError(
            f"{bid}: {sd_update_name} missing from {build['content']} — "
            "an SD build must ship the image the on-device updater looks for"
        )

    # create_sd_data copies the intflash image to update_bank<n>.bin, so the two
    # are usually the same bytes. Store them once and let sdUpdate point at the
    # image's entry — a ~239 KB image is over a third of the SD bundle, and a zip
    # holding it twice buys nothing. If they ever diverge, both are stored.
    image_sha = sha256_file(build["image"])
    sd_update_sha = sha256_file(sd_update_path) if sd_update_path else None
    sd_update_shared = storage == "sd" and sd_update_sha == image_sha

    with zipfile.ZipFile(bundle_path, "w") as zf:
        add_file(zf, build["image"], image_arc)
        if storage == "sd" and not sd_update_shared:
            add_file(zf, sd_update_path, sd_update_name)
        for full, arc, _install, _lang in content:
            add_file(zf, full, arc)

    with zipfile.ZipFile(debug_path, "w") as zf:
        add_file(zf, build["elf"], "gw_retro_go.elf")

    entry = {
        "id": bid,
        "storage": storage,
        "bank": bank,
        "capabilities": capabilities_from_flags(flags),
        "littlefsBlockSize": int(
            flags.get("LITTLEFS_BLOCK_SIZE", LITTLEFS_BLOCK_SIZE_DEFAULT)
        ),
        "buildFlags": build["flags"],
        "bundle": {
            "bytes": os.path.getsize(bundle_path),
            "sha256": sha256_file(bundle_path),
            "url": bundle_name,
        },
        "debug": {
            "bytes": os.path.getsize(debug_path),
            "sha256": sha256_file(debug_path),
            "url": debug_name,
        },
        "image": {
            "bytes": os.path.getsize(build["image"]),
            "sha256": image_sha,
            "path": image_arc,
        },
    }

    if storage == "sd":
        entry["sdUpdate"] = {
            "bytes": os.path.getsize(sd_update_path),
            "sha256": sd_update_sha,
            # Points at the image's entry when the two are the same bytes.
            "path": image_arc if sd_update_shared else sd_update_name,
            # The only place a filename is load-bearing: the on-device updater
            # matches these exact names (firmware_update.c:20,24).
            "filename": sd_update_name,
        }

    entry["content"] = []
    for full, arc, install, language in content:
        item = {
            "path": arc,
            "install": install,
            "bytes": os.path.getsize(full),
            "sha256": sha256_file(full),
        }
        if language:
            item["language"] = language
        entry["content"].append(item)

    return entry


def build_manifest(builds, tag, commit, ref, built_at, core_meta_header, projects_path):
    reference = builds[0]
    provides_abi = read_provides_abi(reference["image"])
    superblock = read_superblock(reference["image"])
    git_tag = read_git_tag(reference["image"])

    # Every build must agree about firmware identity; if they do not, one of the
    # four was built from a different tree and pairing it with the others would
    # ship a core-compat lie.
    for other in builds[1:]:
        for name, fn, expected in (
            ("providesAbi", read_provides_abi, provides_abi),
            ("superblock", read_superblock, superblock),
            ("gitTag", read_git_tag, git_tag),
        ):
            got = fn(other["image"])
            if got != expected:
                raise PackError(
                    f"{other['id']}: {name} {got!r} does not match "
                    f"{reference['id']} {expected!r} — builds are not from one tree"
                )

    entries = [pack_build(b, tag, b["out_dir"]) for b in builds]

    # The union of what the builds actually shipped, plus en_us, which is baked
    # into rodata and therefore has no blob of its own.
    languages = {"en_us"}
    for entry in entries:
        for item in entry["content"]:
            if "language" in item:
                languages.add(item["language"])

    manifest = {
        "schemaVersion": SCHEMA_VERSION,
        "project": PROJECT,
        "title": TITLE,
        "docs": DOCS,
        "source": {"repo": ref["repo"], "commit": commit, "ref": ref["ref"]},
        "firmware": {
            "gitTag": git_tag,
            "providesAbi": provides_abi,
            "abiOffset": hex(ABI_OFFSET),
            "coreMetaVersion": read_core_meta_version(core_meta_header),
            "superblock": superblock,
            "installFile": dict(INSTALL_FILE),
        },
        "paths": dict(PATHS),
        "languages": sorted(languages),
        "builds": entries,
        "builtAt": built_at,
    }

    if projects_path:
        manifest["projects"] = {
            "bytes": os.path.getsize(projects_path),
            "sha256": sha256_file(projects_path),
            "url": os.path.basename(projects_path),
        }

    return manifest


def build_versions(manifest, tag, published_at, prerelease, previous, retained):
    """Regenerate the index, newest first.

    `previous` is the prior versions.json if one exists; entries for this tag are
    replaced rather than duplicated, and the list is trimmed to `retained`.
    """
    entry = {
        "tag": tag,
        "manifest": f"{tag}/manifest.json",
        "publishedAt": published_at,
        "prerelease": prerelease,
        "gitTag": manifest["firmware"]["gitTag"],
        "providesAbi": dict(manifest["firmware"]["providesAbi"]),
        "coreMetaVersion": manifest["firmware"]["coreMetaVersion"],
    }

    older = [v for v in (previous or {}).get("versions", []) if v.get("tag") != tag]
    versions = [entry] + older

    return {
        "schemaVersion": SCHEMA_VERSION,
        "project": PROJECT,
        "title": TITLE,
        "repo": manifest["source"]["repo"],
        "releasesUrl": f"https://github.com/{manifest['source']['repo']}/releases",
        "retained": retained,
        "versions": versions[:retained],
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--build",
        action="append",
        required=True,
        metavar="id=..,storage=..,bank=..,image=..,elf=..,content=..[,flags=..]",
        help="one build; repeat for each. flags= must come last.",
    )
    ap.add_argument("--tag", required=True, help="release tag, e.g. v2.0.0")
    ap.add_argument("--commit", required=True)
    ap.add_argument("--repo", default="slash-proc/game-and-watch-retro-go-sd")
    ap.add_argument("--ref", required=True, help="git ref this was built from")
    ap.add_argument("--built-at", required=True, help="ISO 8601; CI provides")
    ap.add_argument("--published-at", default=None, help="defaults to --built-at")
    ap.add_argument("--prerelease", action="store_true")
    ap.add_argument("--retained", type=int, default=5)
    ap.add_argument(
        "--core-meta-header", default="Core/Inc/retro-go/gnw_core_meta.h"
    )
    ap.add_argument(
        "--projects",
        default=None,
        help="projects.json from gen_projects_json.py; copied into --out",
    )
    ap.add_argument(
        "--previous-versions",
        default=None,
        help="existing versions.json to fold this release into",
    )
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    try:
        builds = [parse_build_arg(spec) for spec in args.build]
    except PackError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    ids = [b["id"] for b in builds]
    if len(set(ids)) != len(ids):
        print(f"error: duplicate build ids in {ids}", file=sys.stderr)
        return 1

    for b in builds:
        b["out_dir"] = args.out

    projects_out = None
    if args.projects:
        projects_out = os.path.join(args.out, "projects.json")
        if os.path.abspath(args.projects) != os.path.abspath(projects_out):
            with open(args.projects, "rb") as src, open(projects_out, "wb") as dst:
                dst.write(src.read())

    try:
        manifest = build_manifest(
            builds,
            args.tag,
            args.commit,
            {"repo": args.repo, "ref": args.ref},
            args.built_at,
            args.core_meta_header,
            projects_out,
        )
    except PackError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    previous = None
    if args.previous_versions and os.path.isfile(args.previous_versions):
        with open(args.previous_versions, encoding="utf-8") as f:
            previous = json.load(f)

    versions = build_versions(
        manifest,
        args.tag,
        args.published_at or args.built_at,
        args.prerelease,
        previous,
        args.retained,
    )

    for name, doc in (("manifest.json", manifest), ("versions.json", versions)):
        with open(os.path.join(args.out, name), "w", encoding="utf-8") as f:
            json.dump(doc, f, indent=2, ensure_ascii=False)
            f.write("\n")

    print(f"tag        {args.tag}  ({manifest['firmware']['gitTag']})")
    print(
        f"abi        v{manifest['firmware']['providesAbi']['version']} "
        f"size {manifest['firmware']['providesAbi']['size']}  "
        f"coreMeta v{manifest['firmware']['coreMetaVersion']}  "
        f"superblock v{manifest['firmware']['superblock']['version']}"
    )
    print(f"languages  {len(manifest['languages'])}")
    for entry in manifest["builds"]:
        print(
            f"  {entry['id']:<12} bundle {entry['bundle']['bytes']:>9,} B  "
            f"debug {entry['debug']['bytes']:>10,} B  "
            f"{len(entry['content'])} content files"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
