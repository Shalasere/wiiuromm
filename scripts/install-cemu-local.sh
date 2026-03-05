#!/usr/bin/env bash
set -euo pipefail

INSTALL_ROOT="${HOME}/.local/opt/cemu"
BIN_DIR="${HOME}/.local/bin"

mkdir -p "$INSTALL_ROOT" "$BIN_DIR"

API_JSON="$(curl -fsSL https://api.github.com/repos/cemu-project/Cemu/releases/latest)"
TAG="$(printf '%s' "$API_JSON" | jq -r .tag_name)"
URL="$(printf '%s' "$API_JSON" | jq -r '.assets[] | select(.name|test("AppImage$")) | .browser_download_url' | head -n1)"
NAME="$(printf '%s' "$API_JSON" | jq -r '.assets[] | select(.name|test("AppImage$")) | .name' | head -n1)"

if [[ -z "$URL" || -z "$NAME" ]]; then
  echo "[error] Could not resolve AppImage from latest Cemu release." >&2
  exit 1
fi

DEST="$INSTALL_ROOT/$NAME"
if [[ ! -f "$DEST" ]]; then
  echo "[download] $URL"
  curl -fL "$URL" -o "$DEST"
fi

chmod +x "$DEST"
ln -sfn "$DEST" "$INSTALL_ROOT/cemu.AppImage"
ln -sfn "$INSTALL_ROOT/cemu.AppImage" "$BIN_DIR/cemu"

echo "[ok] Installed Cemu $TAG at $DEST"
echo "[ok] Symlink: $BIN_DIR/cemu"
