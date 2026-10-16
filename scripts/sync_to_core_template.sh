#!/usr/bin/env bash
# Push the vendored core/homebrew SDK snapshot into retro-go-sd-templates.
#
# Thin wrapper around the template's sync_from_firmware.sh (single source of
# truth for which headers, bridge files, linker scripts, and pack tools copy).
#
# Usage:
#   ./scripts/sync_to_core_template.sh [/path/to/retro-go-sd-templates]
#
# Default template path: ../retro-go-sd-templates (sibling of this repo).
set -euo pipefail

FW=$(cd "$(dirname "$0")/.." && pwd)
TEMPLATE=${1:-}

if [[ -z "$TEMPLATE" ]]; then
  for cand in \
    "$FW/../retro-go-sd-templates" \
    "$FW/../../retro-go-sd-templates"
  do
    if [[ -x "$cand/scripts/sync_from_firmware.sh" ]]; then
      TEMPLATE=$(cd "$cand" && pwd)
      break
    fi
  done
fi

if [[ -z "$TEMPLATE" || ! -x "$TEMPLATE/scripts/sync_from_firmware.sh" ]]; then
  echo "Usage: $0 [/path/to/retro-go-sd-templates]" >&2
  echo "Template repo must contain scripts/sync_from_firmware.sh" >&2
  exit 1
fi

exec "$TEMPLATE/scripts/sync_from_firmware.sh" "$FW"
