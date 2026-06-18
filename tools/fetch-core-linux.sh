#!/usr/bin/env bash
set -euo pipefail

OUT_DIR="${1:-build-linux/core}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUNDLED="$ROOT/third_party/cores/linux-amd64"

if [[ -d "$BUNDLED" && -f "$BUNDLED/sing-box" ]]; then
  echo "[OK] Using bundled cores from third_party/cores/linux-amd64"
  mkdir -p "$OUT_DIR"
  cp -a "$BUNDLED/." "$OUT_DIR/"
  chmod +x "$OUT_DIR"/sing-box "$OUT_DIR"/xray "$OUT_DIR"/hysteria "$OUT_DIR"/tun2socks 2>/dev/null || true
  ls -la "$OUT_DIR"
  exit 0
fi

OUT_DIR="${1:-build-linux/core}"
mkdir -p "$OUT_DIR"

fetch_github() {
  local repo="$1"
  local pattern="$2"
  local dest_name="$3"
  local dest="$OUT_DIR/$dest_name"
  if [[ -f "$dest" ]]; then
    echo "[skip] $dest_name"
    return
  fi
  echo "[fetch] $repo -> $dest_name"
  local url
  url=$(curl -fsSL "https://api.github.com/repos/$repo/releases/latest" \
    | grep -o "https://[^\"]*$pattern" | head -1)
  if [[ -z "$url" ]]; then
    echo "Asset not found: $repo $pattern" >&2
    exit 1
  fi
  tmp=$(mktemp)
  curl -fsSL "$url" -o "$tmp"
  if [[ "$url" == *.zip ]]; then
    unzip -q -j "$tmp" -d "$OUT_DIR"
    rm -f "$tmp"
  else
    mv "$tmp" "$dest"
    chmod +x "$dest"
  fi
}

fetch_github "SagerNet/sing-box" "linux-amd64" "sing-box"
fetch_github "XTLS/Xray-core" "linux-64.zip" "xray"
fetch_github "apernet/hysteria" "hysteria-linux-amd64" "hysteria"
fetch_github "xjasonlyu/tun2socks" "tun2socks-linux-amd64" "tun2socks"

# Normalize names after zip extract
for f in "$OUT_DIR"/sing-box*; do
  [[ -f "$f" ]] && [[ "$f" != *sing-box ]] && mv "$f" "$OUT_DIR/sing-box" && chmod +x "$OUT_DIR/sing-box"
done
for f in "$OUT_DIR"/xray*; do
  [[ -f "$f" ]] && [[ "$f" != *xray ]] && mv "$f" "$OUT_DIR/xray" && chmod +x "$OUT_DIR/xray"
done
for f in "$OUT_DIR"/tun2socks*; do
  [[ -f "$f" ]] && [[ "$f" != *tun2socks ]] && mv "$f" "$OUT_DIR/tun2socks" && chmod +x "$OUT_DIR/tun2socks"
done
for f in "$OUT_DIR"/hysteria*; do
  [[ -f "$f" ]] && [[ "$f" != *hysteria ]] && mv "$f" "$OUT_DIR/hysteria" && chmod +x "$OUT_DIR/hysteria"
done

if [[ ! -f "$OUT_DIR/geoip.dat" ]]; then
  curl -fsSL "https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/geoip.dat" \
    -o "$OUT_DIR/geoip.dat"
fi
if [[ ! -f "$OUT_DIR/geosite.dat" ]]; then
  curl -fsSL "https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/geosite.dat" \
    -o "$OUT_DIR/geosite.dat"
fi

chmod +x "$OUT_DIR"/sing-box "$OUT_DIR"/xray "$OUT_DIR"/hysteria "$OUT_DIR"/tun2socks 2>/dev/null || true
echo "[OK] cores in $OUT_DIR"
ls -la "$OUT_DIR"
