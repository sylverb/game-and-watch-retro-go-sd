#!/bin/bash
#
# Convert an Atari Lynx ROM (.lnx / .lyx) into loaded_lynx_rom.c for the
# linux host build.
#
# Usage:
#   ./update_lynx_rom.sh /path/to/game.lnx

set -e

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 /path/to/game.lnx"
    exit 1
fi

cd "$(dirname "$0")"

INFILE="$1"
OUTFILE=loaded_lynx_rom.c

if [[ ! -f "$INFILE" ]]; then
    echo "File not found: $INFILE"
    exit 1
fi

SIZE=$(wc -c "$INFILE" | awk '{print $1}')
extension="${INFILE##*.}"

echo "const unsigned char ROM_DATA[] = {" > "$OUTFILE"
xxd -i < "$INFILE" >> "$OUTFILE"
echo "};" >> "$OUTFILE"
echo "unsigned int ROM_DATA_LENGTH = $SIZE;" >> "$OUTFILE"
echo "unsigned int cart_rom_len = $SIZE;" >> "$OUTFILE"
echo "const char *ROM_EXT = \"$extension\";" >> "$OUTFILE"
echo "const char lynx_embedded_rom_source[] = \"$(basename "$INFILE")\";" >> "$OUTFILE"

echo "Done! Wrote $OUTFILE from $INFILE ($SIZE bytes)"
