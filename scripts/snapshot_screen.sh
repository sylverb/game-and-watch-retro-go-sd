#!/usr/bin/env bash
# Dump the G&W LCD framebuffer via gnwmanager gdbserver + arm-none-eabi-gdb
# and convert RGB565 → PNG. Writes both the active (currently displayed)
# and inactive (just-drawn, may not have been swapped yet) buffers plus a
# JSON-ish summary of the SNES side (PC, frames, brightness, etc).
#
# Usage:  scripts/snapshot_screen.sh [<out_prefix>]
#         Default prefix is /tmp/ff4_screen — produces:
#           <prefix>_active.png
#           <prefix>_inactive.png
#           <prefix>_state.txt
#         Halt is non-destructive: GDB detaches without restarting the device.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELF="${REPO_ROOT}/build/gw_retro_go.elf"
PREFIX="${1:-/tmp/ff4_screen}"

ACTIVE_RAW="${PREFIX}_active.raw"
INACTIVE_RAW="${PREFIX}_inactive.raw"
ACTIVE_PNG="${PREFIX}_active.png"
INACTIVE_PNG="${PREFIX}_inactive.png"
STATE_TXT="${PREFIX}_state.txt"
GDB_LOG="${PREFIX}_gdb.log"
GDB_SCRIPT="$(mktemp -t snapshot_screen.gdb.XXXXXX)"

if [[ ! -f "$ELF" ]]; then
    echo "[error] ELF not found: $ELF" >&2
    echo "        (build the firmware first)" >&2
    exit 2
fi

cleanup() {
    if [[ -n "${GDBSERVER_PID:-}" ]] && kill -0 "$GDBSERVER_PID" 2>/dev/null; then
        kill "$GDBSERVER_PID" 2>/dev/null || true
        wait "$GDBSERVER_PID" 2>/dev/null || true
    fi
    pkill -f "gnwmanager gdbserver" 2>/dev/null || true
    rm -f "$GDB_SCRIPT"
}
trap cleanup EXIT

cat > "$GDB_SCRIPT" <<EOF
set pagination off
set confirm off

target extended-remote :3333
monitor halt

# Active framebuffer = the one currently being scanned out by the LTDC.
# Inactive = the one ff4_blit_to_lcd is writing into right now.
set \$fb_active   = active_framebuffer ? framebuffer2 : framebuffer1
set \$fb_inactive = active_framebuffer ? framebuffer1 : framebuffer2

# 320 * 240 * 2 = 153600 = 0x25800 bytes (RGB565 little-endian).
dump binary memory $ACTIVE_RAW   \$fb_active   ((char*)\$fb_active)   + 0x25800
dump binary memory $INACTIVE_RAW \$fb_inactive ((char*)\$fb_inactive) + 0x25800

# Capture the SNES side for sanity / correlation.
echo --STATE--\n
printf "active_framebuffer=%d\n", active_framebuffer
printf "fb_active=%p fb_inactive=%p\n", \$fb_active, \$fb_inactive
printf "cpu_k=%02X cpu_pc=%04X\n", ff4_snes->cpu->k, ff4_snes->cpu->pc
printf "frames=%lu\n", (unsigned long)ff4_snes->frames
printf "ppu_forcedBlank=%d brightness=%d mode=%d\n", \
       ff4_snes->ppu->forcedBlank, ff4_snes->ppu->brightness, ff4_snes->ppu->mode
printf "nmiEnabled=%d inVblank=%d\n", \
       ff4_snes->nmiEnabled, ff4_snes->inVblank
printf "apu_in=%02X %02X %02X %02X\n", \
       ff4_snes->apu->inPorts[0], ff4_snes->apu->inPorts[1], \
       ff4_snes->apu->inPorts[2], ff4_snes->apu->inPorts[3]
printf "apu_out=%02X %02X %02X %02X\n", \
       ff4_snes->apu->outPorts[0], ff4_snes->apu->outPorts[1], \
       ff4_snes->apu->outPorts[2], ff4_snes->apu->outPorts[3]
printf "input1_state=%04X portA0=%04X autoJoy=%d\n", \
       ff4_snes->input1->currentState, ff4_snes->portAutoRead[0], \
       (int)ff4_snes->autoJoyRead

detach
quit
EOF

echo "[snap] launching gnwmanager gdbserver…"
gnwmanager gdbserver >"${PREFIX}_gdbserver.log" 2>&1 &
GDBSERVER_PID=$!

# Give openocd ~4 s to settle before we connect.
sleep 4

echo "[snap] dumping framebuffer via GDB…"
arm-none-eabi-gdb -batch -x "$GDB_SCRIPT" "$ELF" >"$GDB_LOG" 2>&1

# Extract the state section into its own file for easy reading.
sed -n '/^--STATE--$/,/^\[Inferior/p' "$GDB_LOG" \
    | sed '1d;$d' \
    > "$STATE_TXT" || true

if [[ ! -s "$ACTIVE_RAW" || ! -s "$INACTIVE_RAW" ]]; then
    echo "[error] framebuffer dump failed — see $GDB_LOG" >&2
    tail -20 "$GDB_LOG" >&2
    exit 3
fi

echo "[snap] converting to PNG…"
python3 "${REPO_ROOT}/scripts/fb_rgb565_to_png.py" "$ACTIVE_RAW"   "$ACTIVE_PNG"
python3 "${REPO_ROOT}/scripts/fb_rgb565_to_png.py" "$INACTIVE_RAW" "$INACTIVE_PNG"

# Hash both buffers so the caller can quickly tell whether they differ
# (= animation in progress) or are identical (= static screen).
md5_active=$(md5 -q "$ACTIVE_RAW" 2>/dev/null \
            || md5sum "$ACTIVE_RAW" | cut -d' ' -f1)
md5_inactive=$(md5 -q "$INACTIVE_RAW" 2>/dev/null \
              || md5sum "$INACTIVE_RAW" | cut -d' ' -f1)

echo "[snap] done."
echo "[snap] active PNG    : $ACTIVE_PNG   (md5 $md5_active)"
echo "[snap] inactive PNG  : $INACTIVE_PNG (md5 $md5_inactive)"
if [[ "$md5_active" == "$md5_inactive" ]]; then
    echo "[snap] buffers identical → static screen (no in-flight redraw)."
else
    echo "[snap] buffers DIFFER → animation/swap in progress."
fi
echo "[snap] SNES state    : $STATE_TXT"
echo
sed 's/^/[state] /' "$STATE_TXT"
