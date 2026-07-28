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
    echo "  --gdb                      Start QEMU suspended (-S) and attach an interactive GDB session."
    echo "                             (Without this flag, a batch GDB connects to forward logs to stdout)."
    echo "  --record <file.tl>         Record a sub-frame accurate input timeline to the specified file."
    echo "                             (Fails if --docker is used since recording requires a local SDL GUI)."
    echo "  --timeline <file.tl>       Playback an existing timeline file."
    echo "  --video <file.mp4>         Export a synced MP4 video (Requires --docker and --timeline)."
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
TIMELINE_FILE=""
RECORD_FILE=""
VIDEO_FILE=""
QMP_PORT=""
PASSTHROUGH_ARGS=()

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --docker) USE_DOCKER=1; shift ;;
        --gdb) USE_GDB=1; shift ;;
        --reset) USE_RESET=1; shift ;;
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

if [ "$USE_RESET" = "1" ]; then
    echo "Resetting emulator state..."
    rm -f build/sdcard.img build/extflash.bin build/qemu_bank1.bin build/qemu_bank2.bin
    make gwemu_release
fi

EXTRA_ARGS="-s"
if [ "$USE_GDB" = "1" ]; then
    EXTRA_ARGS="-s -S"
fi

if [ "$USE_DOCKER" = "1" ]; then
    if [ -n "$RECORD_FILE" ]; then
        echo "Error: Recording timelines in Docker is not supported because it runs headless. Please record locally without --docker."
        exit 1
    fi
    mkdir -p out
    LATEST_TAG=$(curl -s https://api.github.com/repos/slash-proc/gwemu/releases | grep -o '"tag_name": "[^"]*"' | head -n 1 | cut -d '"' -f 4)
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

    if [ "$USE_GDB" = "1" ]; then
        DOCKER_ARGS+=("-p" "1234:1234")
    fi

    DOCKER_CONTAINER="gwemu-run-$$"
    docker run --rm --name "$DOCKER_CONTAINER" \
      -v $PWD/build:/images:ro \
      -v $PWD/out:/out \
      "${DOCKER_ARGS[@]}" \
      "$IMAGE_NAME" \
        --bank1 /images/qemu_bank1.bin \
        --bank2 /images/qemu_bank2.bin \
        --extflash /images/extflash.bin \
        --sd /images/sdcard.img \
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

    build/gwemu_bin -M gnw-h7b0 \
        -global gnw-h7b0-soc.bank1-image=build/qemu_bank1.bin \
        -global gnw-h7b0-soc.bank2-image=build/qemu_bank2.bin \
        -global gnw-h7b0-soc.extflash-image=build/extflash.bin \
        -drive if=sd,file=build/sdcard.img \
        -audiodev sdl3,id=snd0 -global gnw-h7b0-sai1.audiodev=snd0 \
        -display gwemu \
        "${NATIVE_ARGS[@]}" \
        $EXTRA_ARGS "${PASSTHROUGH_ARGS[@]}" &
    GWEMU_PID=$!
    sleep 1
fi

# Fallback to gdb-multiarch if arm-none-eabi-gdb isn't found in env
GDB_CMD=${GDB:-arm-none-eabi-gdb}

if [ "$USE_GDB" = "1" ]; then
    $GDB_CMD build/gw_retro_go.elf -ex "target extended-remote :1234"
elif [ "$USE_DOCKER" = "0" ]; then
    $GDB_CMD build/gw_retro_go.elf -batch -x scripts/gwemu_log.gdb
else
    # Headless docker container handles execution; wait for background container to finish
    wait $GWEMU_PID 2>/dev/null || true
    
    # Copy artifacts out after container exits and ffmpeg finishes
    if [ -n "$RECORD_FILE" ]; then
        cp -f "out/$(basename "$RECORD_FILE")" "$RECORD_FILE" 2>/dev/null || true
    fi
    if [ -n "$VIDEO_FILE" ]; then
        cp -f "out/$(basename "$VIDEO_FILE")" "$VIDEO_FILE" 2>/dev/null || true
    fi
fi
