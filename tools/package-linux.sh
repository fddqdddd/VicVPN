#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="0.1.0-beta"
if command -v powershell >/dev/null 2>&1; then
  VERSION=$(powershell -NoProfile -File "$ROOT/tools/get-version.ps1" 2>/dev/null || echo "0.1.0-beta")
fi
BUILD_DIR="${BUILD_DIR:-$ROOT/build-linux}"
OUT="$ROOT/dist/VicVPN-linux-x64"
ARCHIVE="$ROOT/dist/VicVPN-linux-x64-${VERSION}.tar.gz"

bash "$ROOT/build-linux.sh"

rm -rf "$OUT"
mkdir -p "$OUT/core" "$OUT/styles"

cp "$BUILD_DIR/VicVPN" "$OUT/"
cp -r "$BUILD_DIR/core/"* "$OUT/core/" 2>/dev/null || cp -r "$BUILD_DIR/core/." "$OUT/core/"
cp -r "$ROOT/resources/styles/"* "$OUT/styles/"
cp "$ROOT/LICENSE" "$OUT/"
cp "$ROOT/docs/USER.ru.md" "$OUT/README.txt" 2>/dev/null || true
cp "$ROOT/installer/linux/install.sh" "$OUT/install.sh"
chmod +x "$OUT/install.sh" "$OUT/VicVPN" "$OUT/core/"* 2>/dev/null || true

rm -f "$ARCHIVE"
tar -czf "$ARCHIVE" -C "$ROOT/dist" "VicVPN-linux-x64"
echo "[OK] $ARCHIVE ($(du -h "$ARCHIVE" | cut -f1))"
