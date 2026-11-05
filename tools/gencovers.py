import os
import argparse
import struct
from pathlib import Path
from PIL import Image

def compare_versions(version1, version2):
    """
    Compare two versions represented as strings.
    Returns:
        -1 if version1 < version2
         0 if version1 == version2
         1 if version1 > version2
    """
    v1_parts = [int(part) for part in version1.split('.')]
    v2_parts = [int(part) for part in version2.split('.')]
    
    for v1, v2 in zip(v1_parts, v2_parts):
        if v1 < v2:
            return -1
        elif v1 > v2:
            return 1
    
    if len(v1_parts) < len(v2_parts):
        return -1
    elif len(v1_parts) > len(v2_parts):
        return 1
    else:
        return 0

def calculate_new_size(img, target_width=None, target_height=None):
    """
    Calculate the new size maintaining the aspect ratio.
    Ensures the entire image fits within the specified dimensions.
    """
    MAX_WIDTH, MAX_HEIGHT = 186, 100
    original_width, original_height = img.size
    
    # Set target dimensions with respect to the max constraints
    if target_width is None:
        target_width = MAX_WIDTH
    if target_height is None:
        target_height = MAX_HEIGHT
    
    target_width = min(target_width, MAX_WIDTH)
    target_height = min(target_height, MAX_HEIGHT)
    
    scale_w = target_width / original_width
    scale_h = target_height / original_height
    scale = min(scale_w, scale_h)  # Use the smallest scale to fit entirely
    
    new_width = int(original_width * scale)
    new_height = int(original_height * scale)
    
    return new_width, new_height

def write_thumbnail(srcfile, output_file, target_width, target_height, jpg_quality):
    """
    Create a thumbnail for the given image and save it as a JPEG, keeping aspect ratio if needed.
    """
    if compare_versions(Image.__version__, '7.0') >= 0:
        resample = Image.Resampling.LANCZOS
    else:
        resample = Image.ANTIALIAS
    
    img = Image.open(srcfile).convert(mode="RGB")
    new_size = calculate_new_size(img, target_width, target_height)
    img = img.resize(new_size, resample)
    img.save(output_file, format="JPEG", optimize=True, quality=jpg_quality)

def process_single_image(image_file, output_file, width=None, height=None, jpg_quality=85):
    """
    Process a single image file and create a thumbnail.
    """
    img_path = Path(image_file)
    if not img_path.exists():
        print(f"Error: Image file {image_file} does not exist.")
        return False
    
    if img_path.suffix.lower() not in ['.png', '.jpg', '.jpeg', '.bmp']:
        print(f"Error: Unsupported image format. Supported formats: .png, .jpg, .jpeg, .bmp")
        return False
    
    output_path = Path(output_file)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    if output_path.exists():
        print(f"Thumbnail already exists at {output_path}, skipping...")
        return True
    
    print(f"Creating thumbnail for {img_path} and saving to {output_path}...")
    write_thumbnail(img_path, output_path, width, height, jpg_quality)
    return True

def process_images_in_roms(roms_directory, covers_directory, width=None, height=None, jpg_quality=85):
    """
    Process all images in subdirectories of the roms directory and create thumbnails
    in the corresponding subdirectories under covers_directory, only if they don't exist.
    """
    for subdir, _, files in os.walk(roms_directory):
        for file in files:
            img_path = Path(subdir) / file
            if img_path.suffix.lower() in ['.png', '.jpg', '.jpeg', '.bmp']:
                relative_path = img_path.relative_to(roms_directory)
                output_subdir = Path(covers_directory) / relative_path.parent
                output_subdir.mkdir(parents=True, exist_ok=True)
                output_file = output_subdir / (img_path.stem + ".img")
                
                if output_file.exists():
                    print(f"Thumbnail already exists for {img_path}, skipping...")
                    continue
                
                print(f"Creating thumbnail for {img_path} and saving to {output_file}...")
                write_thumbnail(img_path, output_file, width, height, jpg_quality)

# --- Cover packs: <covers>/<system>.pak (read by Core/Src/retro-go/gui.c) ---
#
# Layout (little endian):
#   "GWCP" | u16 version (1) | u16 reserved | u32 count
#   count * { u32 hash1, u32 hash2, u32 offset, u32 size }  sorted by (hash1, hash2)
#   cover data
# Key: cover path relative to <covers>/<system>/ without ".img", UTF-8,
# hashed with FNV-1a (ASCII case folded) using two seeds.

PAK_MAGIC = b"GWCP"
PAK_VERSION = 1
PAK_SEED1 = 0x811C9DC5
PAK_SEED2 = 0x9747B28C
COVER_MAX_BYTES = 10 * 1024  # COVER_SIZE in gui.c


def _pak_hash(key: bytes, h: int) -> int:
    for c in key:
        if 0x41 <= c <= 0x5A:
            c += 0x20
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h


def pack_covers(covers_directory) -> None:
    """Build one <system>.pak per system folder found in covers_directory."""
    root = Path(covers_directory)
    if not root.is_dir():
        print(f"Covers directory not found: {root}")
        return

    for sysdir in sorted(p for p in root.iterdir() if p.is_dir()):
        entries = {}
        for img in sorted(sysdir.rglob("*.img")):
            key = img.relative_to(sysdir).with_suffix("").as_posix().encode("utf-8")
            data = img.read_bytes()
            if not data or len(data) > COVER_MAX_BYTES:
                print(f"Skipping {img}: {len(data)} bytes (max {COVER_MAX_BYTES})")
                continue
            hashes = (_pak_hash(key, PAK_SEED1), _pak_hash(key, PAK_SEED2))
            if hashes in entries:
                print(f"Skipping {img}: same key as {entries[hashes][0].decode()}")
                continue
            entries[hashes] = (key, data)

        pak_file = root / f"{sysdir.name}.pak"
        if not entries:
            if pak_file.exists():
                pak_file.unlink()
            continue

        ordered = sorted(entries.items())
        header = PAK_MAGIC + struct.pack("<HHI", PAK_VERSION, 0, len(ordered))
        offset = len(header) + 16 * len(ordered)
        index = bytearray()
        blob = bytearray()
        for (h1, h2), (_, data) in ordered:
            index += struct.pack("<IIII", h1, h2, offset + len(blob), len(data))
            blob += data

        pak_file.write_bytes(header + index + blob)
        print(f"Packed {len(ordered)} covers into {pak_file} ({pak_file.stat().st_size} bytes)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate thumbnails for images.")
    parser.add_argument("--src", type=str, default="roms", help="Path to the source directory")
    parser.add_argument("--dst", type=str, default="covers", help="Path to the dest covers directory")
    parser.add_argument("--width", type=int, default=128, help="Thumbnail width (set to None to only use height-based scaling)")
    parser.add_argument("--height", type=int, default=None, help="Thumbnail height (set to None to only use width-based scaling)")
    parser.add_argument("--jpg_quality", type=int, default=85, help="JPEG quality (0-100)")
    parser.add_argument("--image", type=str, help="Path to a single image file to process")
    parser.add_argument("--output", type=str, help="Output path for the single image (only used with --image)")
    parser.add_argument(
        "--pack",
        action="store_true",
        help="After generating, build <dst>/<system>.pak cover packs (faster lookup on the console)",
    )
    parser.add_argument(
        "--pack-only",
        action="store_true",
        help="Only build <dst>/<system>.pak from the existing .img files in --dst",
    )
    
    args = parser.parse_args()
    
    if args.pack_only:
        pack_covers(args.dst)
        raise SystemExit(0)
    
    # If --image is specified, process a single image
    if args.image:
        if not args.output:
            # If no output specified, create output filename based on input
            input_path = Path(args.image)
            args.output = input_path.parent / (input_path.stem + ".img")
        
        success = process_single_image(args.image, args.output, args.width, args.height, args.jpg_quality)
        if success:
            print(f"Successfully created thumbnail: {args.output}")
        else:
            print("Failed to create thumbnail.")
    else:
        # Process all images in the source directory
        process_images_in_roms(args.src, args.dst, args.width, args.height, args.jpg_quality)
        if args.pack:
            pack_covers(args.dst)
