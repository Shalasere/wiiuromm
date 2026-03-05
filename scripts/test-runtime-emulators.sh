#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "[runtime] Wii U (Cemu)"
"$ROOT/scripts/runtime-wiiu-smoke.sh"

echo "[runtime] Wii (Dolphin)"
"$ROOT/scripts/runtime-wii-smoke.sh"

echo "[ok] emulator runtime smoke checks complete"
