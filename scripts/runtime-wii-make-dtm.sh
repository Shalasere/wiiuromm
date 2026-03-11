#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/run/tas/wii_deterministic_smoke.dtm}"

mkdir -p "$(dirname "$OUT")"

PYTHON_CMD=()
if command -v python3 >/dev/null 2>&1; then
  PYTHON_CMD=(python3)
elif command -v python >/dev/null 2>&1; then
  PYTHON_CMD=(python)
elif command -v py >/dev/null 2>&1; then
  PYTHON_CMD=(py -3)
else
  echo "[fail] missing python interpreter (python3/python/py)" >&2
  exit 1
fi

"${PYTHON_CMD[@]}" - "$OUT" <<'PY'
import pathlib
import struct
import sys

out = pathlib.Path(sys.argv[1])

# Homebrew DOL launched directly is shown as game ID 0000000 in Dolphin.
# DTM stores only 6 bytes of game ID, so we use six zeroes.
game_id = b"000000"

header = bytearray(256)
header[0:4] = b"DTM\x1A"
header[4:10] = game_id
header[10] = 1          # bWii
header[11] = 0x10       # controllers: Wiimote 1 enabled
header[12] = 0          # bFromSaveState

payload = bytearray()

def frame_idle() -> None:
    payload.extend((1, 0x00))

def frame_buttons(buttons: int, dpad: int = 0) -> None:
    payload.extend((3, 0x01, buttons & 0x7F, dpad & 0x0F))

def emit_idle(n: int) -> None:
    for _ in range(n):
        frame_idle()

def emit_buttons(n: int, buttons: int, dpad: int = 0) -> None:
    for _ in range(n):
        frame_buttons(buttons, dpad)

# Deterministic sequence:
# boot settle -> open list (A) -> navigate down -> confirm (A) -> quit (HOME+PLUS)
emit_idle(180)
emit_buttons(8, 0x01)    # A
emit_idle(24)
emit_buttons(6, 0x00, 0x02)  # D-Pad down
emit_idle(12)
emit_buttons(8, 0x01)    # A
emit_idle(24)
emit_buttons(120, 0x44)  # HOME + PLUS to force clean app quit

frames = 180 + 8 + 24 + 6 + 12 + 8 + 24 + 120
struct.pack_into("<Q", header, 13, frames)  # frameCount
struct.pack_into("<Q", header, 21, frames)  # inputCount
struct.pack_into("<Q", header, 29, 0)       # lagCount
struct.pack_into("<Q", header, 37, 0)       # uniqueID
struct.pack_into("<I", header, 45, 0)       # numRerecords
struct.pack_into("<Q", header, 109, 0)      # recordingStartTime

out.write_bytes(header + payload)
print(f"[ok] wrote {out} ({len(header) + len(payload)} bytes, {frames} frames)")
PY
