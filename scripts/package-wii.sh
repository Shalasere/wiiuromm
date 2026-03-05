#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 /path/to/sdroot" >&2
  exit 1
fi

SDROOT="$1"
APP_NAME="wiiuromm"
APPDIR="$SDROOT/apps/$APP_NAME"

# Default toolchain env for non-interactive shells.
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"

mkdir -p "$APPDIR"

make -C wii

cp -f "wii/$APP_NAME.dol" "$APPDIR/boot.dol"
cp -f "wii/meta.xml" "$APPDIR/meta.xml"

if [[ -f wii/icon.png ]]; then
  cp -f wii/icon.png "$APPDIR/icon.png"
fi

echo "Packaged to: $APPDIR"
