#!/usr/bin/env python3
"""Assemble a web-flasher artifact from a SD_CARD=0 build, published as a GitHub
Release asset (NOT for end users — end users want retro-go_update.bin).

Outputs into <out>/:
  web-artifacts.zip  - gw_retro_go_intflash.bin (superblock blob) + sd_content/
                       (default content: cores, bios, fonts, lang, logo, homebrew;
                       covers/ and cheats/ EXCLUDED for now) + manifest.json.
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
    ap.add_argument("--blob", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--id", required=True)
    ap.add_argument("--ref", required=True)
    ap.add_argument("--sha", required=True)
    ap.add_argument("--model", required=True)
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
            full = os.path.join(root, fn)
            arc = os.path.join("sd_content", os.path.relpath(full, args.sd_content))
            members.append((full, arc.replace(os.sep, "/")))

    manifest = {
        "id": args.id,
        "ref": args.ref,
        "sha": args.sha,
        "model": args.model,
        "sdCard": 0,
        "superblock": True,
        "builtAt": args.built_at,
        "asset": ZIP_NAME,
        "blob": "gw_retro_go_intflash.bin",
        "cores": cores,
        "fileCount": len(members),
        "blobBytes": os.path.getsize(args.blob),
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

    with zipfile.ZipFile(zip_path, "w") as zf:
        add_file(zf, args.blob, "gw_retro_go_intflash.bin")
        for full, arc in members:
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
