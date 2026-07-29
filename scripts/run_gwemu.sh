#!/usr/bin/env bash
set -e

if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    echo "Usage: ./scripts/run_gwemu.sh [OPTIONS] [QEMU_ARGS]"
    echo ""
    echo "A wrapper script to launch gwemu (Game & Watch QEMU) with retro-go."
    echo ""
    echo "Options:"
    echo "  --docker                   Run QEMU inside a headless Docker container."
    echo "                             (Downloads and builds 'slashproc/gwemu-headless' image)"
    echo "  --gdb                      Attach an interactive GDB session instead of the default"
    echo "                             batch log/fault forwarder."
    echo "  --gdb-script <file.gdb>    Run <file.gdb> under batch GDB instead of the default"
    echo "                             forwarder. For one-off diagnostics only -- the default"
    echo "                             already forwards logs and traps exceptions."
    echo "  --log-file <path>          Write the session log here (default: ./gwemu.log,"
    echo "                             overwritten each run). Output still goes to stdout."
    echo "  --record <file.tl>         Record a sub-frame accurate input timeline to the specified file."
    echo "                             (Fails if --docker is used since recording requires a local SDL GUI)."
    echo "  --timeline <file.tl>       Playback an existing timeline file."
    echo "  --video <file.mp4>         Export a synced MP4 video (Requires --docker and --timeline)."
    echo "  --update                   Check for and download the latest gwemu release before running."
    echo "  --qmp <port>               Expose QMP (QEMU Monitor Protocol) on the given port."
    echo "  --help, -h                 Show this help message."
    echo ""
    exit 0
fi
cd "$(dirname "$0")/.."

GWEMU_PID=""
DOCKER_CONTAINER=""
cleanup() {
    if [ -n "$DOCKER_CONTAINER" ]; then
        docker stop "$DOCKER_CONTAINER" >/dev/null 2>&1 || true
    fi
    if [ -n "$GWEMU_PID" ]; then
        kill $GWEMU_PID 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

USE_DOCKER=0
USE_GDB=0
USE_RESET=0
USE_UPDATE=0
TIMELINE_FILE=""
RECORD_FILE=""
VIDEO_FILE=""
QMP_PORT=""
GDB_SCRIPT=""
LOG_FILE=""
PASSTHROUGH_ARGS=()

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --docker) USE_DOCKER=1; shift ;;
        --gdb) USE_GDB=1; shift ;;
        --gdb-script) GDB_SCRIPT="$2"; shift 2 ;;
        --log-file) LOG_FILE="$2"; shift 2 ;;
        --reset) USE_RESET=1; shift ;;
        --update) USE_UPDATE=1; shift ;;
        --timeline) TIMELINE_FILE="$2"; shift 2 ;;
        --record) RECORD_FILE="$2"; shift 2 ;;
        --video) VIDEO_FILE="$2"; shift 2 ;;
        --qmp) QMP_PORT="$2"; shift 2 ;;
        --) 
            shift
            PASSTHROUGH_ARGS=("$@")
            break
            ;;
        *) echo "Unknown parameter: $1"; exit 1 ;;
    esac
done

# Every run is logged. The whole script's output -- emulator stderr, forwarded
# retro-go log buffer, and any fault report -- goes to both stdout and the file.
# Kept at the repo root, NOT under build/, so `make clean` does not wipe the log of
# the run you are trying to read. Gitignored via *.log.
LOG_FILE="${LOG_FILE:-gwemu.log}"
mkdir -p "$(dirname "$LOG_FILE")"
exec > >(tee "$LOG_FILE") 2>&1
echo "[run_gwemu] logging to $LOG_FILE"

if [ "$USE_UPDATE" = "1" ]; then
    # gwemu is still moving fast; --update pulls the newest release before running.
    make gwemu_download GWEMU_UPDATE=1
fi

if [ "$USE_RESET" = "1" ]; then
    echo "Resetting emulator state..."
    rm -f build/sdcard.img build/extflash.bin build/qemu_bank1.bin build/qemu_bank2.bin
    make gwemu_release
fi

# Always start suspended. A GDB session always attaches -- forwarding the log
# buffer and trapping exceptions is the entire point of running under emulation,
# so it is not optional and not mode-dependent. Without -S the forwarder attaches
# a second or two into the boot and silently misses everything retro-go printed
# on the way up, which reads as "logging is broken".
EXTRA_ARGS="-s -S"

if [ "$USE_DOCKER" = "1" ]; then
    if [ -n "$RECORD_FILE" ]; then
        echo "Error: Recording timelines in Docker is not supported because it runs headless. Please record locally without --docker."
        exit 1
    fi
    mkdir -p out
    # Cached for an hour -- this used to hit the GitHub API on every single run.
    LATEST_TAG=$(./scripts/gwemu_latest_tag.sh)
    VERSION=${LATEST_TAG#v}
    IMAGE_NAME="${DOCKER_IMAGE:-slashproc/gwemu-headless:$VERSION}"

    DOCKER_ARGS=()
    ENTRY_ARGS=()
    
    if [ -n "$TIMELINE_FILE" ]; then
        DOCKER_ARGS+=("-v" "$PWD/$TIMELINE_FILE:/demo.tl:ro")
        ENTRY_ARGS+=("--timeline" "/demo.tl")
    fi
    
    if [ -n "$RECORD_FILE" ]; then
        # Map record file output into the /out volume
        REC_BASENAME=$(basename "$RECORD_FILE")
        ENTRY_ARGS+=("--record-timeline" "/out/$REC_BASENAME")
    fi

    if [ -n "$VIDEO_FILE" ]; then
        VID_BASENAME=$(basename "$VIDEO_FILE")
        ENTRY_ARGS+=("--record" "$VID_BASENAME")
    fi

    if [ -n "$QMP_PORT" ]; then
        DOCKER_ARGS+=("-p" "$QMP_PORT:$QMP_PORT")
        ENTRY_ARGS+=("--qmp-port" "$QMP_PORT")
    fi

    # GDB always attaches, so the port is always needed.
    DOCKER_ARGS+=("-p" "1234:1234")

    DOCKER_CONTAINER="gwemu-run-$$"
    docker run --rm --name "$DOCKER_CONTAINER" \
      -v $PWD/build:/images:ro \
      -v $PWD/out:/out \
      "${DOCKER_ARGS[@]}" \
      "$IMAGE_NAME" \
        --bank1 /images/qemu_bank1.bin \
        --bank2 /images/qemu_bank2.bin \
        --extflash /images/extflash.bin \
        $([ -f build/sdcard.img ] && echo "--sd /images/sdcard.img") \
        "${ENTRY_ARGS[@]}" \
        -- $EXTRA_ARGS "${PASSTHROUGH_ARGS[@]}" &
    GWEMU_PID=$!
    sleep 2
    

else
    NATIVE_ARGS=()
    if [ -n "$QMP_PORT" ]; then
        NATIVE_ARGS+=("-qmp" "tcp:localhost:$QMP_PORT,server,nowait")
    fi
    # Native QEMU doesn't have a built-in --timeline argument like the Docker python wrapper does,
    # but we map QMP equally to both environments.
    
    if [ -n "$RECORD_FILE" ]; then
        export GNW_TIMELINE_RECORD="$PWD/$RECORD_FILE"
    fi
    if [ -n "$TIMELINE_FILE" ]; then
        export GNW_TIMELINE="$PWD/$TIMELINE_FILE"
    fi

    # SD_CARD=0 (FrogFS) builds have no SD image - the filesystem lives in
    # external flash. Only attach a card when one was actually produced.
    SD_ARGS=()
    if [ -f build/sdcard.img ]; then
        SD_ARGS=("-drive" "if=sd,file=build/sdcard.img")
    else
        echo "No build/sdcard.img - running without an SD card (SD_CARD=0 build)."
    fi

    ./gwemu_bin -M gnw-h7b0 \
        -global gnw-h7b0-soc.bank1-image=build/qemu_bank1.bin \
        -global gnw-h7b0-soc.bank2-image=build/qemu_bank2.bin \
        -global gnw-h7b0-soc.extflash-image=build/extflash.bin \
        "${SD_ARGS[@]}" \
        -audiodev sdl3,id=snd0 -global gnw-h7b0-sai1.audiodev=snd0 \
        -display gwemu \
        "${NATIVE_ARGS[@]}" \
        $EXTRA_ARGS "${PASSTHROUGH_ARGS[@]}" &
    GWEMU_PID=$!
    sleep 1
fi

# Fallback to gdb-multiarch if arm-none-eabi-gdb isn't found in env
GDB_CMD=${GDB:-arm-none-eabi-gdb}

# A GDB session ALWAYS attaches, in every mode including headless Docker. Live
# log-buffer forwarding and exception capture are the reason for running under
# emulation at all, so they are never skipped. scripts/gwemu_log.gdb is the
# default; --gdb-script only replaces it for one-off diagnostics, and --gdb
# swaps the batch forwarder for an interactive session.
GDB_RC=0
if [ "$USE_GDB" = "1" ]; then
    $GDB_CMD build/gw_retro_go.elf -ex "target extended-remote :1234" || GDB_RC=$?
else
    $GDB_CMD build/gw_retro_go.elf -batch -x "${GDB_SCRIPT:-scripts/gwemu_log.gdb}" || GDB_RC=$?
fi

if [ "$USE_DOCKER" = "1" ]; then
    # Let the container finish (timeline playback / ffmpeg), then collect artifacts.
    wait $GWEMU_PID 2>/dev/null || true
    if [ -n "$RECORD_FILE" ]; then
        cp -f "out/$(basename "$RECORD_FILE")" "$RECORD_FILE" 2>/dev/null || true
    fi
    if [ -n "$VIDEO_FILE" ]; then
        cp -f "out/$(basename "$VIDEO_FILE")" "$VIDEO_FILE" 2>/dev/null || true
    fi
fi

# gwemu_log.gdb exits non-zero when it trapped a fault or a failed assertion, so
# a caller (or CI) can detect a crashed run without parsing the log.
if [ "$GDB_RC" -ne 0 ]; then
    echo "[run_gwemu] session ended with an exception (see $LOG_FILE)"
fi
exit $GDB_RC
