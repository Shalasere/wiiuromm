#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="wiiuromm"
BUILD_OUT="$ROOT/wii/$APP.dol"
RUN_DIR="$ROOT/run"
USER_DIR="$RUN_DIR/dolphin_user"
LOG_DIR="$RUN_DIR/logs"
LOG_FILE="$LOG_DIR/runtime-wii.log"
TIMEOUT_SECONDS="${RUNTIME_TIMEOUT_SECONDS:-12}"
HEADLESS="${RUNTIME_HEADLESS:-1}"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"

mkdir -p "$RUN_DIR" "$USER_DIR" "$LOG_DIR"

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
  echo "[skip] dolphin not found; skipping Wii runtime smoke" >&2
  exit 0
fi

echo "[build] Wii target"
make -C "$ROOT/wii"

if [[ ! -f "$BUILD_OUT" ]]; then
  echo "[fail] Missing output: $BUILD_OUT" >&2
  exit 1
fi

if [[ "$HEADLESS" == "0" ]]; then
  if [[ -z "${DISPLAY:-}" ]]; then
    echo "[fail] RUNTIME_HEADLESS=0 requires DISPLAY to be set." >&2
    exit 1
  fi
  echo "[run] Wii smoke via Dolphin (visible) for ${TIMEOUT_SECONDS}s"
  RUNNER=("$DOLPHIN_BIN" --user "$USER_DIR" --exec "$BUILD_OUT")
else
  echo "[run] Wii smoke via Dolphin (headless xvfb) for ${TIMEOUT_SECONDS}s"
  RUNNER=(xvfb-run -a "$DOLPHIN_BIN" --batch --user "$USER_DIR" --exec "$BUILD_OUT")
fi

set +e
timeout "${TIMEOUT_SECONDS}s" \
  "${RUNNER[@]}" \
  >"$LOG_FILE" 2>&1
rc=$?
set -e

if [[ $rc -eq 124 ]]; then
  echo "[pass] Wii runtime smoke passed (process alive until timeout)"
  exit 0
fi

echo "[fail] Wii runtime smoke failed (rc=$rc). Log: $LOG_FILE" >&2
tail -n 60 "$LOG_FILE" >&2 || true
exit "$rc"
