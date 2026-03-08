#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 /path/to/input.dtm [--visible]" >&2
  exit 2
fi

MOVIE="$1"
VISIBLE="${2:-}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="wiiuromm"
BUILD_OUT="$ROOT/wii/$APP.dol"
RUN_DIR="$ROOT/run"
USER_DIR="$RUN_DIR/dolphin_user"
LOG_DIR="$RUN_DIR/logs"
LOG_FILE="$LOG_DIR/runtime-wii-tas.log"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"

mkdir -p "$RUN_DIR" "$USER_DIR" "$LOG_DIR"

if [[ ! -f "$MOVIE" ]]; then
  echo "[fail] movie not found: $MOVIE" >&2
  exit 1
fi

if command -v dolphin-emu >/dev/null 2>&1; then
  DOLPHIN_BIN="$(command -v dolphin-emu)"
elif command -v dolphin >/dev/null 2>&1; then
  DOLPHIN_BIN="$(command -v dolphin)"
else
  echo "[fail] dolphin not found" >&2
  exit 1
fi

echo "[build] Wii target"
make -C "$ROOT/wii"

if [[ ! -f "$BUILD_OUT" ]]; then
  echo "[fail] missing output: $BUILD_OUT" >&2
  exit 1
fi

if [[ "$VISIBLE" == "--visible" ]]; then
  echo "[run] TAS playback (visible) movie=$MOVIE"
  DISPLAY="${DISPLAY:-:1}" "$DOLPHIN_BIN" --user "$USER_DIR" \
    --exec "$BUILD_OUT" --movie "$MOVIE" >"$LOG_FILE" 2>&1
else
  echo "[run] TAS playback (batch) movie=$MOVIE"
  "$DOLPHIN_BIN" --batch --user "$USER_DIR" \
    --exec "$BUILD_OUT" --movie "$MOVIE" >"$LOG_FILE" 2>&1
fi

echo "[ok] TAS playback finished"
echo "log: $LOG_FILE"
