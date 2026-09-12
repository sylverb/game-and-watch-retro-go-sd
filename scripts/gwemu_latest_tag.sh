#!/usr/bin/env bash
# Resolve the latest gwemu release tag, caching the answer for an hour.
#
# GitHub rate-limits unauthenticated API calls (60/hour/IP). Both the Makefile's
# gwemu_download and run_gwemu.sh's Docker path need the latest tag, and the Docker
# path asked on *every* run, so a handful of iterations could exhaust the budget and
# start returning nothing -- which shows up as a mysterious empty tag rather than an
# obvious error.
#
# The cache is a plain file whose mtime is the timestamp; `find -mmin` is used for the
# age test because it behaves the same on GNU and BSD/macOS, unlike `stat`.
#
# Usage:  scripts/gwemu_latest_tag.sh [--force]
# Prints the tag (e.g. "v0.0.19") on stdout, or nothing if it could not be determined.
#
#   GWEMU_TAG=v0.0.18   pin a tag; no network access at all
#   GWEMU_RELEASE_TTL   cache lifetime in minutes (default 60; 0 disables the cache)
#   --force             ignore the cache for this call (use right after a new release)

set -e
cd "$(dirname "$0")/.."

CACHE=".gwemu_release_cache"
TTL="${GWEMU_RELEASE_TTL:-60}"
FORCE=0
[ "$1" = "--force" ] && FORCE=1

# An explicit pin wins and never touches the network.
if [ -n "$GWEMU_TAG" ]; then
    printf '%s\n' "$GWEMU_TAG"
    exit 0
fi

# Fresh cache? Use it.
if [ "$FORCE" = "0" ] && [ "$TTL" != "0" ] && [ -s "$CACHE" ] \
   && [ -n "$(find "$CACHE" -mmin "-$TTL" 2>/dev/null)" ]; then
    cat "$CACHE"
    exit 0
fi

TAG=$(curl -s https://api.github.com/repos/slash-proc/gwemu/releases \
      | grep -o '"tag_name": "[^"]*"' | head -n 1 | cut -d '"' -f 4)

if [ -n "$TAG" ]; then
    printf '%s\n' "$TAG" > "$CACHE"
    printf '%s\n' "$TAG"
    exit 0
fi

# Query failed (offline, or rate-limited). Fall back to a stale cache rather than
# returning nothing, and say so on stderr so it is not mistaken for success.
if [ -s "$CACHE" ]; then
    echo "[gwemu] release lookup failed; using cached tag $(cat "$CACHE")" >&2
    cat "$CACHE"
    exit 0
fi

echo "[gwemu] could not determine the latest release tag (offline or rate-limited)." >&2
echo "[gwemu] set GWEMU_TAG=vX.Y.Z to pin one explicitly." >&2
exit 1
