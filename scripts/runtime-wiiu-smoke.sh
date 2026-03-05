#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="wiiuromm"
BUILD_OUT="$ROOT/$APP.rpx"
RUN_DIR="$ROOT/run"
MLC_DIR="$RUN_DIR/cemu_mlc"
LOG_DIR="$RUN_DIR/logs"
LOG_FILE="$LOG_DIR/runtime-wiiu.log"
TIMEOUT_SECONDS="${RUNTIME_TIMEOUT_SECONDS:-12}"
HEADLESS="${RUNTIME_HEADLESS:-1}"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"

mkdir -p "$RUN_DIR" "$MLC_DIR" "$LOG_DIR"

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
  echo "[skip] cemu not found; skipping Wii U runtime smoke" >&2
  exit 0
fi

echo "[build] Wii U target"
make -C "$ROOT"

if [[ ! -f "$BUILD_OUT" ]]; then
  echo "[fail] Missing output: $BUILD_OUT" >&2
  exit 1
fi

if [[ "$HEADLESS" == "0" ]]; then
  if [[ -z "${DISPLAY:-}" ]]; then
    echo "[fail] RUNTIME_HEADLESS=0 requires DISPLAY to be set." >&2
    exit 1
  fi
  echo "[run] Wii U smoke via Cemu (visible) for ${TIMEOUT_SECONDS}s"
  RUNNER=("$CEMU_BIN" --mlc "$MLC_DIR" -g "$BUILD_OUT")
else
  echo "[run] Wii U smoke via Cemu (headless xvfb) for ${TIMEOUT_SECONDS}s"
  RUNNER=(xvfb-run -a "$CEMU_BIN" --mlc "$MLC_DIR" -g "$BUILD_OUT")
fi

set +e
timeout "${TIMEOUT_SECONDS}s" \
  "${RUNNER[@]}" \
  >"$LOG_FILE" 2>&1
rc=$?
set -e

if [[ $rc -eq 124 ]]; then
  echo "[pass] Wii U runtime smoke passed (process alive until timeout)"
  exit 0
fi

echo "[fail] Wii U runtime smoke failed (rc=$rc). Log: $LOG_FILE" >&2
tail -n 60 "$LOG_FILE" >&2 || true
exit "$rc"
