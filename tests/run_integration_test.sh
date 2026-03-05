#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="${MOCK_ROMM_PORT:-18080}"
BASE_URL="http://127.0.0.1:${PORT}"
LOG_DIR="${ROOT}/../run/logs"
LOG_FILE="${LOG_DIR}/mock_romm_server.log"

mkdir -p "$LOG_DIR"

if ! command -v python3 >/dev/null 2>&1; then
  echo "[skip] python3 missing; integration test skipped"
  exit 0
fi
if ! command -v curl >/dev/null 2>&1; then
  echo "[skip] curl missing; integration test skipped"
  exit 0
fi

python3 "$ROOT/mock_romm_server.py" --port "$PORT" >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
cleanup() {
  kill "$SERVER_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

ready=0
for _ in $(seq 1 50); do
  if curl -fsS "${BASE_URL}/health" >/dev/null 2>&1; then
    ready=1
    break
  fi
  sleep 0.1
done

if [[ "$ready" != "1" ]]; then
  echo "[fail] mock server failed to start; see ${LOG_FILE}" >&2
  exit 1
fi

MOCK_BASE_URL="$BASE_URL" "$ROOT/integration_test"
