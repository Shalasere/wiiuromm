#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="wiiuromm"
BUILD_OUT="$ROOT/wii/$APP.dol"
RUN_DIR="$ROOT/run"
USER_DIR="$RUN_DIR/dolphin_user"
LOG_DIR="$RUN_DIR/logs"
VIDEO_OUT="$LOG_DIR/wii-visible-control.mp4"
DOLPHIN_LOG="$LOG_DIR/wii-visible-control-dolphin.log"
CAPTURE_LOG="$LOG_DIR/wii-visible-control-capture.log"
CAPTURE_SECONDS="${CAPTURE_SECONDS:-12}"
DISPLAY_ID="${DISPLAY:-:1}"

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

require_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[fail] missing required tool: $1" >&2
    exit 1
  fi
}

require_tool xte
require_tool ffmpeg
require_tool compare

DOLPHIN_BIN="$(find_dolphin || true)"
if [[ -z "$DOLPHIN_BIN" ]]; then
  echo "[fail] dolphin not found" >&2
  exit 1
fi

CFG_DIR="$USER_DIR/Config"
mkdir -p "$CFG_DIR"

# Force deterministic keyboard-driven GC controller input and allow input when
# launcher terminal keeps focus.
cat > "$CFG_DIR/GCPadNew.ini" <<'EOF'
[GCPad1]
Device = XInput2/0/Virtual core keyboard
Buttons/A = `X`
Buttons/B = `Z`
Buttons/X = `C`
Buttons/Y = `S`
Buttons/Z = `D`
Buttons/Start = `Return`
Main Stick/Up = `Up`
Main Stick/Down = `Down`
Main Stick/Left = `Left`
Main Stick/Right = `Right`
Main Stick/Modifier = `Shift`
[GCPad2]
Device = XInput2/0/Virtual core keyboard
[GCPad3]
Device = XInput2/0/Virtual core keyboard
[GCPad4]
Device = XInput2/0/Virtual core keyboard
EOF

cat >> "$CFG_DIR/Dolphin.ini" <<'EOF'
[Core]
SIDevice0 = 6
SIDevice1 = 0
SIDevice2 = 0
SIDevice3 = 0
[Interface]
BackgroundInput = True
EOF

cleanup() {
  if [[ -n "${FFMPEG_PID:-}" ]]; then kill "$FFMPEG_PID" >/dev/null 2>&1 || true; fi
  if [[ -n "${DOLPHIN_PID:-}" ]]; then kill "$DOLPHIN_PID" >/dev/null 2>&1 || true; fi
}
trap cleanup EXIT

echo "[build] Wii target"
make -C "$ROOT/wii"
if [[ ! -f "$BUILD_OUT" ]]; then
  echo "[fail] missing output: $BUILD_OUT" >&2
  exit 1
fi

rm -f "$VIDEO_OUT" "$DOLPHIN_LOG" "$CAPTURE_LOG"

echo "[run] Dolphin visible on DISPLAY=$DISPLAY_ID"
DISPLAY="$DISPLAY_ID" env -u WAYLAND_DISPLAY -u WAYLAND_SOCKET QT_QPA_PLATFORM=xcb \
  "$DOLPHIN_BIN" --user "$USER_DIR" --exec "$BUILD_OUT" \
  >"$DOLPHIN_LOG" 2>&1 &
DOLPHIN_PID=$!
sleep 5

echo "[capture] recording $CAPTURE_SECONDS s to $VIDEO_OUT"
DISPLAY="$DISPLAY_ID" ffmpeg -y -f x11grab -i "$DISPLAY_ID" -t "$CAPTURE_SECONDS" \
  "$VIDEO_OUT" >"$CAPTURE_LOG" 2>&1 &
FFMPEG_PID=$!
sleep 1

# Focus the Dolphin window area and drive deterministic inputs:
# x => GC A (Select), Down => move selection, x => open detail.
DISPLAY="$DISPLAY_ID" xte "mousemove 640 360" "mouseclick 1"
sleep 1
DISPLAY="$DISPLAY_ID" xte "key x"
sleep 1
DISPLAY="$DISPLAY_ID" xte "key Down"
sleep 1
DISPLAY="$DISPLAY_ID" xte "key x"

wait "$FFMPEG_PID"
unset FFMPEG_PID

kill "$DOLPHIN_PID" >/dev/null 2>&1 || true
wait "$DOLPHIN_PID" >/dev/null 2>&1 || true
unset DOLPHIN_PID

if [[ ! -s "$VIDEO_OUT" ]]; then
  echo "[fail] video capture missing or empty: $VIDEO_OUT" >&2
  exit 1
fi

# Verify the scene changed after injected inputs. Near-zero change indicates
# key injection is not reaching the emulator session.
FRAME1="$LOG_DIR/wii-visible-control-f1.png"
FRAME2="$LOG_DIR/wii-visible-control-f2.png"
rm -f "$FRAME1" "$FRAME2"
ffmpeg -y -i "$VIDEO_OUT" -ss 1 -vframes 1 "$FRAME1" >/dev/null 2>&1
ffmpeg -y -i "$VIDEO_OUT" -ss 9 -vframes 1 "$FRAME2" >/dev/null 2>&1
DIFF_RAW="$(compare -metric AE "$FRAME1" "$FRAME2" null: 2>&1 || true)"
CHANGED_PIXELS="$(printf '%s\n' "$DIFF_RAW" | awk '{print $1}')"
if [[ -z "$CHANGED_PIXELS" ]]; then
  CHANGED_PIXELS=0
fi
if [[ "$CHANGED_PIXELS" -lt 1000 ]]; then
  echo "[fail] input injection likely not reaching emulator (changed_pixels=$CHANGED_PIXELS)." >&2
  echo "[hint] Wayland/Xwayland focus+synthetic-input restrictions are likely active." >&2
  exit 1
fi

echo "[pass] visible control run complete"
echo "video: $VIDEO_OUT"
echo "logs : $DOLPHIN_LOG ; $CAPTURE_LOG"
