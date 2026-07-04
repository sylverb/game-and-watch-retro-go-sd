#!/usr/bin/env python3
"""Assemble a web-flasher artifact from a SD_CARD=0 build, published as a GitHub
Release asset (NOT for end users — end users want retro-go_update.bin).

Outputs into <out>/:
  web-artifacts.zip  - gw_retro_go_intflash_bank{1,2}.bin (two superblock blobs, one
                       per intflash bank — bank is the link address, not a runtime
                       patch) + sd_content/ (default content: cores, bios, fonts, lang,
                       logo, homebrew; covers/ and cheats/ EXCLUDED for now) + manifest.json.
  manifest.json      - standalone copy, uploaded as its own release asset so the
                       web-flasher's version picker can read metadata without
                       downloading the whole zip.

The zip is a matched set: the cores are objcopy slices of THIS exact ELF, so the
blob and sd_content must ship together. Consumed by gnw-web-builder (merge
sd_content + user ROMs -> FrogFS -> patch superblock -> flash).

Layout (flat): gw_retro_go_intflash.bin, sd_content/..., manifest.json
"""
import argparse
import hashlib
import json
import os
import sys
import zipfile

EXCLUDE_TOP = {"covers", "cheats"}  # subordinate features, packed later
# Firmware-update trigger files: the firmware_update bootloader flashes these
# (firmware_update.c). They are SD-update plumbing — irrelevant to the web flasher
# (we flash over USB) and the ONLY files in sd_content that could cause a flash if
# someone merged the archive onto an SD card. Excluded so NOTHING here can flash.
EXCLUDE_FILES = {"update_bank1.bin", "update_bank2.bin", "retro-go_update.bin"}
ZIP_NAME = "web-artifacts.zip"
# Neutral name on purpose — nothing here invites an end user. Safe even if someone
# overwrites their SD with these: the firmware reads /cores, /bios, /roms from the
# SD ROOT, but our SD content lives under sd_content/, so it's simply absent at root
# (no content loads); and the on-device updater only flashes /retro-go_update.bin,
# which we never ship → no flash, no brick.

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sd-content", required=True)
    ap.add_argument("--blob-bank1", required=True)  # INTFLASH_BANK=1 link (0x08000000)
    ap.add_argument("--blob-bank2", required=True)  # INTFLASH_BANK=2 link (0x08100000)
    ap.add_argument("--blob-sd-bank1", required=True)     # SD_CARD=1 link (0x08000000)
    ap.add_argument("--blob-sd-bank2", required=True)     # SD_CARD=1 link (0x08100000)
    # Optional matching ELFs (debug symbols), one per blob above. Bundled in so a
    # debugger can be pointed at symbols that actually match whatever's flashed on a
    # given device, instead of a locally-built ELF that may be a different commit or
    # build variant entirely (a real mix-up during this branch's debugging sessions).
    ap.add_argument("--elf-bank1", default=None)
    ap.add_argument("--elf-bank2", default=None)
    ap.add_argument("--elf-sd-bank1", default=None)
    ap.add_argument("--elf-sd-bank2", default=None)
    ap.add_argument("--out", required=True)
    ap.add_argument("--id", required=True)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--sha", required=True)
    ap.add_argument("--built-at", required=True)  # ISO8601; CI provides
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    zip_path = os.path.join(args.out, ZIP_NAME)

    cores_dir = os.path.join(args.sd_content, "cores")
    cores = sorted(
        f[:-4]
        for f in os.listdir(cores_dir)
        if f.endswith(".bin") and not f.endswith("_defprops.bin")
    ) if os.path.isdir(cores_dir) else []

    # collect sd_content files (excluding covers/cheats), deterministic order
    members = []  # (abs_path, arcname)
    for root, dirs, files in os.walk(args.sd_content):
        rel = os.path.relpath(root, args.sd_content)
        if rel.split(os.sep)[0] in EXCLUDE_TOP:
            dirs[:] = []
            continue
        dirs.sort()
        for fn in sorted(files):
            if fn in EXCLUDE_FILES:
                continue
            full = os.path.join(root, fn)
            arc = os.path.join("sd_content", os.path.relpath(full, args.sd_content))
            members.append((full, arc.replace(os.sep, "/")))

    # Two linked blobs per configuration — bank is the intflash address, not a runtime patch.
    banks = [
        ("bank1", "gw_retro_go_intflash_bank1.bin", args.blob_bank1, "0x08000000", args.elf_bank1),
        ("bank2", "gw_retro_go_intflash_bank2.bin", args.blob_bank2, "0x08100000", args.elf_bank2),
        ("sd_bank1", "gw_retro_go_intflash_sd_bank1.bin", args.blob_sd_bank1, "0x08000000", args.elf_sd_bank1),
        ("sd_bank2", "gw_retro_go_intflash_sd_bank2.bin", args.blob_sd_bank2, "0x08100000", args.elf_sd_bank2),
    ]
    elf_arcnames = {}  # bank -> arcname, only for banks with an ELF actually supplied
    for bank, _fname, _path, _addr, elf_path in banks:
        if elf_path and os.path.isfile(elf_path):
            elf_arcnames[bank] = f"elf/gw_retro_go_{bank}.elf"

    manifest = {
        "id": args.id,
        "ref": args.ref,
        "sha": args.sha,
        "sdCard": 0,
        "superblock": True,
        "builtAt": args.built_at,
        "asset": ZIP_NAME,
        "blobs": {
            bank: {
                "file": fname,
                "intflashAddr": addr,
                "bytes": os.path.getsize(path),
                **({"elf": elf_arcnames[bank]} if bank in elf_arcnames else {}),
            }
            for bank, fname, path, addr, _elf_path in banks
        },
        # Capabilities baked into the blobs (content like covers/cheats is added later
        # by the browser into the FrogFS; these flags just enable the firmware paths).
        "capabilities": ["coverflow", "cheatCodes", "screenshot", "sharedHibernateSavestate"],
        "cores": cores,
        "fileCount": len(members),
    }

    # fixed timestamp → reproducible-ish zips
    zdate = (1980, 1, 1, 0, 0, 0)

    def add_bytes(zf, arcname, data):
        zi = zipfile.ZipInfo(arcname, date_time=zdate)
        zi.compress_type = zipfile.ZIP_DEFLATED
        zf.writestr(zi, data)

    def add_file(zf, full, arcname):
        zi = zipfile.ZipInfo(arcname, date_time=zdate)
        zi.compress_type = zipfile.ZIP_DEFLATED
        with open(full, "rb") as f:
            zf.writestr(zi, f.read())

    # collect restool scripts (Python extraction scripts for the web builder Pyodide worker)
    restool_members = []
    restool_dirs = [
        ("external/smw/assets", "restools/smw"),
        ("external/zelda3/tables", "restools/zelda3"),
    ]
    for src_dir, dest_dir in restool_dirs:
        if os.path.exists(src_dir):
            for root, _, files in os.walk(src_dir):
                for fn in sorted(files):
                    if fn.endswith(".py"):
                        full = os.path.join(root, fn)
                        rel = os.path.relpath(full, src_dir)
                        arc = os.path.join(dest_dir, rel).replace(os.sep, "/")
                        restool_members.append((full, arc))

    with zipfile.ZipFile(zip_path, "w") as zf:
        for bank, fname, path, _addr, elf_path in banks:
            add_file(zf, path, fname)
            if bank in elf_arcnames:
                add_file(zf, elf_path, elf_arcnames[bank])
        for full, arc in members:
            add_file(zf, full, arc)
        for full, arc in restool_members:
            add_file(zf, full, arc)
        add_bytes(zf, "manifest.json", json.dumps(manifest, indent=2).encode())

    manifest["assetBytes"] = os.path.getsize(zip_path)
    manifest["assetSha256"] = sha256(zip_path)
    with open(os.path.join(args.out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)

    print(f"{ZIP_NAME}: {manifest['assetBytes']} B ({manifest['fileCount']} content files)")
    print(f"cores: {', '.join(cores)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
