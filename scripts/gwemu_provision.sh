#!/usr/bin/env bash
# Provision a git worktree so it can build and run gwemu.
#
# A fresh `git worktree add` cannot run gwemu without four fixes, and everyone
# who has tried has rediscovered them by hand:
#
#   1. Submodules cannot clone. The parent's submodules are local paths, and git
#      refuses `file://` transport by default (`transport 'file' not allowed`);
#      -c protocol.file.allow and GIT_ALLOW_PROTOCOL are both refused under the
#      worktree guard. Copying the parent's already-checked-out trees is the way.
#   2. gwemu_bin is not in the worktree, and a copied one without its .version
#      file makes `make gwemu_download` skip the update -- v0.0.20 cannot mount
#      an SD card image at all, so SD tests fail for a reason that looks like a
#      firmware bug.
#   3. roms/ is untracked, so a worktree has none. ROMs reach an SD image only
#      through sd_content/roms/<system>/; for flash they come from roms/.
#   4. The worktree may come up on a different commit than requested.
#
# Usage:
#   scripts/gwemu_provision.sh [--roms <system>[,<system>...]] [--expect <sha>]
#
# Run it from inside the worktree. Safe to re-run.
set -euo pipefail

ROMS=""
EXPECT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --roms)   ROMS="$2"; shift 2 ;;
        --expect) EXPECT="$2"; shift 2 ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

cd "$(dirname "$0")/.."
WT="$PWD"
PARENT="$(git rev-parse --path-format=absolute --git-common-dir)"
PARENT="$(cd "$(dirname "$PARENT")" && pwd)"

if [ "$PARENT" = "$WT" ]; then
    echo "gwemu_provision: this IS the main checkout; nothing to provision." >&2
    exit 0
fi
echo "worktree: $WT"
echo "parent:   $PARENT"

if [ -n "$EXPECT" ]; then
    HAVE="$(git rev-parse HEAD)"
    case "$HAVE" in
        "$EXPECT"*) echo "commit:   $HAVE (as expected)" ;;
        *) echo "commit:   $HAVE -- expected $EXPECT; resetting" >&2
           git reset --hard "$EXPECT" >/dev/null
           echo "commit:   $(git rev-parse HEAD) (reset)" ;;
    esac
fi

# 1. submodules: copy the parent's checked-out trees
n=0
while read -r m; do
    [ -n "$m" ] || continue
    if [ -e "$PARENT/$m/.git" ] || [ -d "$PARENT/$m" ]; then
        mkdir -p "$WT/$m"
        rsync -a --delete "$PARENT/$m/" "$WT/$m/" 2>/dev/null && n=$((n+1))
    fi
done < <(git config -f .gitmodules --get-regexp '^submodule\..*\.path$' | awk '{print $2}')
echo "submodules populated: $n"

# 2. gwemu binary, at the version the Makefile wants
if [ -f "$PARENT/gwemu_bin" ] && [ -f "$PARENT/gwemu_bin.version" ]; then
    cp -a "$PARENT/gwemu_bin" "$PARENT/gwemu_bin.version" "$WT/" 2>/dev/null || true
fi
make gwemu_download GWEMU_UPDATE=1 >/dev/null 2>&1 || \
    make gwemu_download >/dev/null 2>&1 || true
echo "gwemu:    $(cat gwemu_bin.version 2>/dev/null || echo '(unknown)')"

# 3. ROMs, for both the flash tree and the SD content tree
if [ -n "$ROMS" ]; then
    IFS=','; for s in $ROMS; do unset IFS
        if [ -d "$PARENT/roms/$s" ]; then
            mkdir -p "$WT/roms/$s" "$WT/sd_content/roms/$s"
            cp -a "$PARENT/roms/$s/." "$WT/roms/$s/"
            cp -a "$PARENT/roms/$s/." "$WT/sd_content/roms/$s/"
            echo "roms/$s:  $(ls -1 "$WT/roms/$s" | wc -l) file(s)"
        else
            echo "roms/$s:  MISSING in parent" >&2
        fi
    done
fi

echo "gwemu_provision: ready. Remember --reset after any rebuild, and make clean between SD_CARD variants."
