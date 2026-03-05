#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="wiiuromm"
BUILD_OUT="$ROOT/$APP.rpx"
RUN_DIR="$ROOT/run"
MLC_DIR="$RUN_DIR/cemu_mlc"

# Default toolchain env for non-interactive shells.
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"

mkdir -p "$RUN_DIR"
mkdir -p "$MLC_DIR"

find_cemu() {
  if command -v cemu >/dev/null 2>&1; then
    command -v cemu
    return 0
  fi
  if [[ -x "$HOME/.local/bin/cemu" ]]; then
    printf '%s\n' "$HOME/.local/bin/cemu"
    return 0
  fi
  return 1
}

CEMU_BIN="$(find_cemu || true)"
if [[ -z "$CEMU_BIN" ]]; then
  echo "[error] cemu not found in PATH." >&2
  echo "Install local Cemu first (no sudo): scripts/install-cemu-local.sh" >&2
  exit 1
fi

echo "[build] Wii U target"
make -C "$ROOT"

if [[ ! -f "$BUILD_OUT" ]]; then
  echo "[error] Missing output: $BUILD_OUT" >&2
  exit 1
fi

echo "[run] Launching Cemu with $BUILD_OUT (mlc: $MLC_DIR)"
exec "$CEMU_BIN" --mlc "$MLC_DIR" -g "$BUILD_OUT"
