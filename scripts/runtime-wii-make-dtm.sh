#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/run/tas/wii_deterministic_smoke.dtm}"

mkdir -p "$(dirname "$OUT")"

python3 - "$OUT" <<'PY'
import pathlib
import struct
import sys

out = pathlib.Path(sys.argv[1])

# Dolphin sets executable game IDs as "ID-<filename-without-ext>".
# For wiiuromm.dol this becomes "ID-wiiuromm", and DTM stores only 6 bytes.
game_id = b"ID-wii"

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
