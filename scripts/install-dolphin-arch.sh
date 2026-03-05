#!/usr/bin/env bash
set -euo pipefail

if ! command -v pacman >/dev/null 2>&1; then
  echo "[error] pacman not found. This helper is for Arch-based systems." >&2
  exit 1
fi

echo "[install] dolphin-emu via pacman (requires sudo)"
exec sudo pacman -S --needed dolphin-emu
