#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-build-linux}"
CORE_DIR="$BUILD_DIR/core"

echo "==> VicVPN Linux build (beta)"

if ! command -v cmake >/dev/null; then
  echo "Install: sudo apt install cmake g++ qt6-base-dev qt6-svg-dev libsqlite3-dev nlohmann-json3-dev"
  exit 1
fi

cmake -DCMAKE_BUILD_TYPE=Release -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" -j"$(nproc)"

if [[ ! -f "$BUILD_DIR/VicVPN" ]]; then
  echo "Build failed: VicVPN binary not found"
  exit 1
fi

if [[ ! -f "$CORE_DIR/sing-box" ]]; then
  echo "==> Fetching cores..."
  bash "$ROOT/tools/fetch-core-linux.sh" "$CORE_DIR"
fi

echo "[OK] $BUILD_DIR/VicVPN"
echo "Run as root for TUN: sudo $BUILD_DIR/VicVPN"
