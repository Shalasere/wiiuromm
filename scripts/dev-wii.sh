#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="wiiuromm"
BUILD_OUT="$ROOT/wii/$APP.dol"

# Default toolchain env for non-interactive shells.
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"

find_dolphin() {
  if command -v dolphin-emu >/dev/null 2>&1; then
    command -v dolphin-emu
    return 0
  fi
  if command -v dolphin >/dev/null 2>&1; then
    command -v dolphin
    return 0
  fi
  return 1
}

DOLPHIN_BIN="$(find_dolphin || true)"
if [[ -z "$DOLPHIN_BIN" ]]; then
  echo "[error] Dolphin not found." >&2
  echo "Install with: sudo pacman -S dolphin-emu" >&2
  exit 1
fi

echo "[build] Wii target"
make -C "$ROOT/wii"

if [[ ! -f "$BUILD_OUT" ]]; then
  echo "[error] Missing output: $BUILD_OUT" >&2
  exit 1
fi

echo "[run] Launching Dolphin with $BUILD_OUT"
exec env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET QT_QPA_PLATFORM=xcb \
  "$DOLPHIN_BIN" -e "$BUILD_OUT"
