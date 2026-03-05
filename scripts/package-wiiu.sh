#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 /path/to/sdroot" >&2
  exit 1
fi

SDROOT="$1"
APP_NAME="wiiuromm"
APPDIR="$SDROOT/wiiu/apps/$APP_NAME"

# Default toolchain env for non-interactive shells.
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"

mkdir -p "$APPDIR"

make

cp -f "$APP_NAME.rpx" "$APPDIR/$APP_NAME.rpx"
cp -f meta.xml "$APPDIR/meta.xml"

if [[ -f icon.png ]]; then
  cp -f icon.png "$APPDIR/icon.png"
fi

echo "Packaged to: $APPDIR"
