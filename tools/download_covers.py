#!/usr/bin/env python3
import os
import sys
import json
import urllib.request
import urllib.parse
import difflib
import argparse
from pathlib import Path

SYSTEM_MAP = {
    "nes": "Nintendo_-_Nintendo_Entertainment_System",
    # "snes": "Nintendo_-_Super_Nintendo_Entertainment_System",
    "gb": "Nintendo_-_Game_Boy",
    "gbc": "Nintendo_-_Game_Boy_Color",
    # "gba": "Nintendo_-_Game_Boy_Advance",
    "genesis": "Sega_-_Mega_Drive_-_Genesis",
    "megadrive": "Sega_-_Mega_Drive_-_Genesis",
    "md": "Sega_-_Mega_Drive_-_Genesis",
    "sms": "Sega_-_Master_System_-_Mark_III",
    "gg": "Sega_-_Game_Gear",
    "sg1000": "Sega_-_SG-1000",
    "sg": "Sega_-_SG-1000",
    # "ngp": "SNK_-_Neo_Geo_Pocket",
    # "ngpc": "SNK_-_Neo_Geo_Pocket_Color",
    "pce": "NEC_-_PC_Engine_-_TurboGrafx_16",
    "tg16": "NEC_-_PC_Engine_-_TurboGrafx_16",
    "wswan": "Bandai_-_WonderSwan",
    "wswanc": "Bandai_-_WonderSwan_Color",
    "wsv": "Bandai_-_WonderSwan",
    "a2600": "Atari_-_2600",
    "a7800": "Atari_-_7800",
    "amstrad": "Amstrad_-_CPC",
    "col": "Coleco_-_ColecoVision",
    "msx": "Microsoft_-_MSX",
    "videopac": "Magnavox_-_Odyssey2",
}

EXCLUDED_EXTENSIONS = {
    ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".img",
    ".keep", ".txt", ".crc", ".sav", ".state", ".sram",
    ".bak", ".cfg", ".config", ".db", ".dat", ".lnk", ".zip"
}

def clean_name(filename):
    # Remove extension
    name = Path(filename).stem
    # Remove bracketed metadata like (USA), (Europe), [!], [t1]
    cleaned = []
    in_paren = 0
    in_bracket = 0
    for char in name:
        if char == '(':
            in_paren += 1
        elif char == ')':
            in_paren = max(0, in_paren - 1)
        elif char == '[':
            in_bracket += 1
        elif char == ']':
            in_bracket = max(0, in_bracket - 1)
        elif in_paren == 0 and in_bracket == 0:
            cleaned.append(char)
    
    return "".join(cleaned).strip()

def get_libretro_tree(system_repo, token=None):
    url = f"https://api.github.com/repos/libretro-thumbnails/{system_repo}/git/trees/master?recursive=1"
    req = urllib.request.Request(url, headers={"User-Agent": "Retro-Go-Cover-Scraper"})
    if token:
        req.add_header("Authorization", f"token {token}")
        
    try:
        with urllib.request.urlopen(req) as response:
            data = json.loads(response.read().decode('utf-8'))
            return data.get("tree", [])
    except urllib.error.HTTPError as e:
        if e.code == 403:
            print("Error: GitHub API rate limit exceeded. Try again in an hour, or supply a GitHub token with --token.")
        else:
            print(f"Error fetching repository tree for {system_repo}: {e}")
        return None
    except Exception as e:
        print(f"Error fetching repository tree for {system_repo}: {e}")
        return None

def main():
    parser = argparse.ArgumentParser(description="Scrape cover art from the Libretro thumbnails database and save it next to ROMs.")
    parser.add_argument("--roms", type=str, default="roms", help="Path to your local ROM directory (default: 'roms')")
    parser.add_argument("--system", type=str, choices=list(SYSTEM_MAP.keys()), help="Optional system shortcode (e.g. nes, gba) to scan only one system")
    parser.add_argument("--token", type=str, help="Optional GitHub Personal Access Token to avoid rate limiting")
    parser.add_argument("--match-ratio", type=float, default=0.6, help="Fuzzy match ratio threshold (0.0 to 1.0, default 0.6)")

    args = parser.parse_args()

    roms_dir = Path(args.roms)
    if not roms_dir.exists() or not roms_dir.is_dir():
        print(f"Error: ROMs directory '{args.roms}' does not exist.")
        sys.exit(1)

    print(f"Scanning ROMs directory: {roms_dir}")

    # Determine which systems to scan
    if args.system:
        systems_to_scan = [args.system]
    else:
        # Scan all directories in roms_dir that match keys in SYSTEM_MAP
        systems_to_scan = []
        for p in roms_dir.iterdir():
            if p.is_dir() and p.name.lower() in SYSTEM_MAP:
                systems_to_scan.append(p.name.lower())
        
        # Sort systems for consistent run order
        systems_to_scan.sort()

    if not systems_to_scan:
        print("No subdirectories matching known systems were found.")
        sys.exit(0)

    total_downloaded = 0
    total_skipped = 0
    total_failed = 0

    for system in systems_to_scan:
        system_dir = roms_dir / system
        system_repo = SYSTEM_MAP[system]

        print(f"\n[{system.upper()}] Scanning ROMs in {system_dir}...")
        
        # Scan files in system_dir
        rom_files = []
        for f in system_dir.iterdir():
            if f.is_file() and not f.name.startswith(".") and f.suffix.lower() not in EXCLUDED_EXTENSIONS:
                rom_files.append(f)

        if not rom_files:
            print(f"[{system.upper()}] No ROM files found. Skipping.")
            continue

        # Check which ROMs are missing cover art next to the ROM
        missing_roms = []
        for rom in rom_files:
            has_local_image = False
            for ext in [".png", ".jpg", ".jpeg", ".bmp"]:
                if (system_dir / (rom.stem + ext)).exists():
                    has_local_image = True
                    break
            
            if not has_local_image:
                missing_roms.append(rom)
            else:
                total_skipped += 1

        if not missing_roms:
            print(f"[{system.upper()}] All {len(rom_files)} ROMs already have cover art. Skipping.")
            continue

        print(f"[{system.upper()}] Found {len(missing_roms)} ROM(s) missing cover art. Fetching Libretro thumbnails database...")
        tree = get_libretro_tree(system_repo, token=args.token)
        if not tree:
            print(f"[{system.upper()}] Failed to fetch repository list. Skipping system.")
            total_failed += len(missing_roms)
            continue

        # Filter for files in Named_Boxarts
        boxarts = {}
        for entry in tree:
            path = entry.get("path", "")
            if path.startswith("Named_Boxarts/") and path.endswith(".png"):
                filename = Path(path).name
                clean_db_name = clean_name(filename).lower()
                boxarts[clean_db_name] = path

        if not boxarts:
            print(f"[{system.upper()}] Warning: No boxarts found in 'Named_Boxarts' directory of the repository. Skipping system.")
            total_failed += len(missing_roms)
            continue

        db_clean_names = list(boxarts.keys())

        for rom in missing_roms:
            rom_clean = clean_name(rom.name).lower()
            
            match = None
            if rom_clean in boxarts:
                match = rom_clean
            else:
                close_matches = difflib.get_close_matches(rom_clean, db_clean_names, n=1, cutoff=args.match_ratio)
                if close_matches:
                    match = close_matches[0]

            if not match:
                print(f"  [-] No match found for: {rom.name}")
                total_failed += 1
                continue

            repo_path = boxarts[match]
            encoded_path = urllib.parse.quote(repo_path)
            download_url = f"https://raw.githubusercontent.com/libretro-thumbnails/{system_repo}/master/{encoded_path}"

            print(f"  [+] Match found! Downloading cover for {rom.name}...")
            try:
                req = urllib.request.Request(download_url, headers={"User-Agent": "Retro-Go-Cover-Scraper"})
                if args.token:
                    req.add_header("Authorization", f"token {args.token}")
                
                with urllib.request.urlopen(req) as response:
                    png_bytes = response.read()

                # Save raw PNG next to the ROM file
                dest_png = system_dir / (rom.stem + ".png")
                with open(dest_png, "wb") as f:
                    f.write(png_bytes)
                print(f"    Saved PNG: {dest_png.name}")

                total_downloaded += 1

            except Exception as e:
                print(f"    [!] Failed to download cover: {e}")
                total_failed += 1

    print("\nScraping complete!")
    print(f"Downloaded/Processed: {total_downloaded}")
    print(f"Skipped (already exists): {total_skipped}")
    print(f"Failed/No match: {total_failed}")

if __name__ == "__main__":
    main()
