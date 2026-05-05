#!/bin/bash
set -e

CHOICES_FILE=".build_choices"
declare -A PREV

[[ -f "$CHOICES_FILE" ]] && while IFS= read -r line; do
    PREV["${line%%=*}"]="${line#*=}"
done < "$CHOICES_FILE"

execute() { [[ "${DRY_RUN:-0}" -eq 1 ]] && echo "[DRY-RUN] $*" || { echo "$*"; "$@" ; } ; }

validate_hash() {
    echo -n "Validating $(basename "$1")... "
    local actual; actual=$(sha256sum "$1" | awk '{print $1}')
    [[ "$actual" == "$2" ]] && echo "OK!" \
        || { echo "FAILED!"; echo "Expected: $2"; echo "Actual:   $actual"; exit 1; }
}

# prompt VARNAME "text" [fallback_default]
prompt() {
    local -n _r=$1; local def="${PREV[$1]:-$3}"
    read -p "$2${def:+ (default: $def)}: " _r
    _r="${_r:-$def}"; PREV[$1]="$_r"
}

# prompt_yn VARNAME "text" [fallback_default: yes|no]
prompt_yn() {
    local -n _r=$1; local def="${PREV[$1]:-$3}"
    while true; do
        read -p "$2 [y/n]${def:+ (default: ${def:0:1})}: " _r
        _r="${_r:-$def}"
        case "${_r,,}" in
            y|yes) _r=yes; PREV[$1]=yes; return ;;
            n|no)  _r=no;  PREV[$1]=no;  return ;;
            *) echo "Invalid: $_r" ;;
        esac
    done
}

echo
echo "### Welcome to the Retro-Go firmware helper script! ###"
echo
# One-liner intentional: if script changes after git pull it must not resume mid-execution
printf "Checking for updates... "
REPO_UPDATE="$(git pull; git submodule update --init --recursive)"
[[ "$REPO_UPDATE" == "Already up to date." ]] || { echo "Update found — restart required."; exit 1; }
echo "OK"

# --- Variant ---
echo
prompt GNW_VARIANT "Mario or Zelda Game & Watch? (m/z)"
case "${GNW_VARIANT,,}" in
    m|mario) GNW_VARIANT=mario ;;
    z|zelda) GNW_VARIANT=zelda ;;
    *) echo "Invalid variant: ${GNW_VARIANT}. Quitting."; exit 1 ;;
esac
PREV[GNW_VARIANT]="$GNW_VARIANT"

# --- Flash size ---
declare -A _DEF_FLASH=([mario]=1 [zelda]=4)
declare -A _OFFSET=([mario]=1048576 [zelda]=$((1048576 * 4)))
prompt FLASH_SIZE "Flash size in MB" "${_DEF_FLASH[$GNW_VARIANT]}"
FLASH_SIZE_BYTES=$(( FLASH_SIZE * 1024 * 1024 ))
OFFSET_SIZE_BYTES="${_OFFSET[$GNW_VARIANT]}"
OFFSET_SIZE=$(( OFFSET_SIZE_BYTES / 1024 / 1024 ))

# --- Covers ---
COVERFLOW=0; COVERS_SIZE_BYTES=0
mkdir -p covers
COVER_COUNT=$(find covers/ -maxdepth 1 -type f -iname "*.img" | wc -l)
if (( COVER_COUNT > 0 )); then
    COVERFLOW=1
    COVERS_SIZE_BYTES=$(du -bc covers/*.img | awk '/total/{print $1}')
    prompt JPG_QUALITY "Cover quality (1-100)" "85"
    [[ "$JPG_QUALITY" =~ ^[0-9]+$ ]] && (( JPG_QUALITY >= 1 && JPG_QUALITY <= 100 )) \
        || { echo "Invalid quality. Must be 1-100. Quitting."; exit 1; }
fi

# --- Options ---
echo
prompt_yn FLASH_LOCALLY "Flash on this machine?" "no"

prompt IMG_TYPE "Storage type (sdcard/flash)" "flash"
case "${IMG_TYPE,,}" in
    s|sd|sdcard) IMG_TYPE=sdcard; SD_CARD=1; RETRO_GO_MINIMUM_BUILD_SIZE=2 ;;
    f|flash)     IMG_TYPE=flash;  SD_CARD=0; RETRO_GO_MINIMUM_BUILD_SIZE=4 ;;
    *) echo "Invalid type: ${IMG_TYPE}. Quitting."; exit 1 ;;
esac
PREV[IMG_TYPE]="$IMG_TYPE"

prompt_yn DUALBOOT "Dual-boot stock + retro-go?" "yes"

# --- Fonts ---
echo
LANGUAGE_ARGS=""
prompt_yn ADDITIONAL_FONTS "Install additional fonts?" "yes"
if [[ "$ADDITIONAL_FONTS" == "yes" ]]; then
    prompt_yn FONT_KO "Install Korean fonts?"  "yes"
    prompt_yn FONT_JP "Install Japanese fonts?" "yes"
    prompt_yn FONT_ZH "Install Chinese fonts?"  "yes"
    [[ "$FONT_KO" == "yes" ]] && LANGUAGE_ARGS+=" KO_KR=1"
    [[ "$FONT_JP" == "yes" ]] && LANGUAGE_ARGS+=" JP_JP=1"
    [[ "$FONT_ZH" == "yes" ]] && LANGUAGE_ARGS+=" ZH_CN=1 ZH_TW=1"
else
    LANGUAGE_ARGS="SINGLE_FONT=1"
fi

# --- ROM migration ---
mkdir -p roms
OLD_REPO=$(find ../ -maxdepth 1 -type d -name "game-and-watch-retro-go")
if [[ -n "$OLD_REPO" ]]; then
    echo; echo "Found old repo at ${OLD_REPO}"
    prompt_yn MIGRATE_ROMS "Migrate ROMs?" "no"
    if [[ "$MIGRATE_ROMS" == "yes" ]]; then
        echo "Copying old ROMs to ./roms/"
        cp -r "${OLD_REPO}/roms/"* roms/
    fi
    mkdir -p roms/homebrew
    for rom in "roms/smw/smw.sfc" "roms/zelda3/zelda3.sfc"; do
        if [[ -f "$rom" ]]; then
            echo "Found $(basename "$rom")"
            mv "$rom" roms/homebrew/
            rm -r "$(dirname "$rom")"
        fi
    done
fi
ROMS_SIZE_BYTES=$(du -bc -d2 roms/ | awk '/total/{print $1}')

# --- Space check ---
RETRO_GO_MINIMUM_BUILD_SIZE_BYTES=$(( RETRO_GO_MINIMUM_BUILD_SIZE * 1024 * 1024 + COVERS_SIZE_BYTES ))
if (( SD_CARD == 0 )); then
    TOTAL_USED_SPACE_BYTES=$(( RETRO_GO_MINIMUM_BUILD_SIZE_BYTES + COVERS_SIZE_BYTES + ROMS_SIZE_BYTES ))
else
    TOTAL_USED_SPACE_BYTES=$RETRO_GO_MINIMUM_BUILD_SIZE_BYTES
fi
[[ "$DUALBOOT" == "yes" ]] && TOTAL_USED_SPACE_BYTES=$(( TOTAL_USED_SPACE_BYTES + OFFSET_SIZE_BYTES ))

if   (( TOTAL_USED_SPACE_BYTES > FLASH_SIZE_BYTES )); then
    echo "Flash too small — need $(( TOTAL_USED_SPACE_BYTES/1024/1024 ))MB minimum. Quitting."; exit 1
elif (( FLASH_SIZE < _DEF_FLASH[$GNW_VARIANT] )); then
    echo "Flash too small for ${GNW_VARIANT^}. Quitting."; exit 1
elif (( FLASH_SIZE > 256 )); then
    echo "Flash > 256MB — are you using MiB not bits? Quitting."; exit 1
fi

# --- Backup files ---
echo
prompt BACKUP_DIR "Backup directory" "./"
BACKUP_DIR="${BACKUP_DIR/#\~/$HOME}"

INT_FLASH_BIN=$(find "$BACKUP_DIR" -maxdepth 2 -name "internal_flash_backup_${GNW_VARIANT}.bin" -size 131072c -print -quit)
EXT_FLASH_BIN=$(find "$BACKUP_DIR" -maxdepth 2 -name "flash_backup_${GNW_VARIANT}.bin" -size "${OFFSET_SIZE_BYTES}c" -print -quit)
if [[ -z "$INT_FLASH_BIN" || -z "$EXT_FLASH_BIN" ]]; then
    echo "Error: backup file(s) missing in $BACKUP_DIR:"
    echo "  internal_flash_backup_${GNW_VARIANT}.bin"
    echo "  flash_backup_${GNW_VARIANT}.bin"
    exit 1
fi

echo "Backup files in $(dirname "$INT_FLASH_BIN"). Verifying hashes..."
declare -A _INT_SHA256=(
    [zelda]="ab37ba03bc33682c091b4e7caffd7d3102b83e675377e06d6d3046b9bf483bb6"
    [mario]="b1f10bde11490dcf922524fcba1592ffbc47d2b9a95cda58358ab8934c747e5b"
)
declare -A _EXT_SHA256=(
    [zelda]="cad7e32b0783250c29b4684fda0c8cd5b1a11e6c000ca8dd5633fd32ebffcaed"
    [mario]="2cb99ef457b6495a99514f296a0ef07316e2b376c7c6c38309d4fff9e176b387"
)
validate_hash "$INT_FLASH_BIN" "${_INT_SHA256[$GNW_VARIANT]}"
validate_hash "$EXT_FLASH_BIN" "${_EXT_SHA256[$GNW_VARIANT]}"

# --- Summary ---
echo
echo "Summary:"
printf "  %-18s %s\n" "Variant:"      "$GNW_VARIANT"
printf "  %-18s %s\n" "Flash:"        "${FLASH_SIZE}MB (${IMG_TYPE})"
printf "  %-18s %s\n" "Dual-boot:"    "$DUALBOOT"
printf "  %-18s %s\n" "Flash locally:" "$FLASH_LOCALLY"
printf "  %-18s %s\n" "ROMs:"         "$(( ROMS_SIZE_BYTES/1024/1024 ))MB"
printf "  %-18s %s\n" "Covers:"       "${COVERS_SIZE_BYTES}B"
printf "  %-18s %s\n" "Total used:"   "$(( TOTAL_USED_SPACE_BYTES/1024/1024 ))MB / ${FLASH_SIZE}MB"
printf "  %-18s %s\n" "Build args:"   "${LANGUAGE_ARGS# }"

echo
read -p "Continue? (y/N) " ANS_CONTINUE
[[ "${ANS_CONTINUE,,}" =~ ^y(es)?$ ]] || { echo "Quitting."; exit; }

# Persist choices
for k in "${!PREV[@]}"; do printf '%s=%s\n' "$k" "${PREV[$k]}"; done > "$CHOICES_FILE"

# --- gnwmanager ---
if [[ "$FLASH_LOCALLY" == "yes" ]]; then
    PIP_OUTPUT="$(pip index versions gnwmanager)"
    LATEST=$(awk    '/LATEST/{print $2}'    <<< "$PIP_OUTPUT")
    INSTALLED=$(awk '/INSTALLED/{print $2}' <<< "$PIP_OUTPUT")
    if [[ -z "$INSTALLED" ]]; then
        echo "Installing gnwmanager..."
        execute pip install gnwmanager -q --break-system-packages
    elif [[ "$INSTALLED" != "$LATEST" ]]; then
        prompt_yn UPGRADE_GNWMANAGER "Update gnwmanager ($INSTALLED → $LATEST)?" "yes"
        [[ "$UPGRADE_GNWMANAGER" == "yes" ]] && execute pip install --upgrade gnwmanager -q --break-system-packages
    fi
fi

# --- Build ---
[[ -d build ]] && { echo "Cleaning old build..."; make clean; }

INTFLASH_BANK=1; [[ "$DUALBOOT" == "yes" ]] && INTFLASH_BANK=2
BUILD_ARGS="CHECK_DIRTY_SUBMODULE=0 COVERFLOW=${COVERFLOW} SHARED_HIBERNATE_SAVESTATE=1 DISABLE_SPLASH_SCREEN=1 INTFLASH_BANK=${INTFLASH_BANK} ${LANGUAGE_ARGS# } SD_CARD=${SD_CARD}"
[[ "$DUALBOOT" == "yes" ]] && BUILD_ARGS+=" EXTFLASH_OFFSET=${OFFSET_SIZE_BYTES} EXTFLASH_SIZE_MB=$(( FLASH_SIZE - OFFSET_SIZE ))"
BUILD_ARGS+=" $*"

if [[ "$FLASH_LOCALLY" == "yes" ]]; then
    if [[ "$DUALBOOT" == "yes" ]]; then
        prompt_yn FLASH_BOOTLOADER "Flash patched bootloader (skip if already done)?" "yes"
        [[ "$FLASH_BOOTLOADER" == "yes" ]] && execute gnwmanager flash-patch "${GNW_VARIANT}" "${INT_FLASH_BIN}" "${EXT_FLASH_BIN}" --bootloader
    fi
    execute make -j$(nproc) $BUILD_ARGS flash_extflash
else
    execute make -j$(nproc) $BUILD_ARGS --no-print-directory frogfs_image littlefs_image
fi
