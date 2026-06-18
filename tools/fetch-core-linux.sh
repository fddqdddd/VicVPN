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

mkdir -p "$OUT_DIR"

curl_api() {
  local url="$1"
  local args=(-fsSL -H "User-Agent: VicVPN-CI")
  if [[ -n "${GITHUB_TOKEN:-}" ]]; then
    args+=(-H "Authorization: Bearer ${GITHUB_TOKEN}")
  fi
  curl "${args[@]}" "$url"
}

pick_asset_url() {
  local json="$1"
  local pattern="$2"
  PATTERN="$pattern" python3 -c '
import json, os, re, sys
pat = re.compile(os.environ["PATTERN"])
data = json.load(sys.stdin)
for asset in data.get("assets", []):
    url = asset.get("browser_download_url", "")
    if pat.search(url):
        print(url)
        break
' <<<"$json"
}

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
  local json url tmp
  json=$(curl_api "https://api.github.com/repos/$repo/releases/latest")
  url=$(pick_asset_url "$json" "$pattern")
  if [[ -z "$url" ]]; then
    echo "Asset not found: $repo / $pattern" >&2
    return 1
  fi
  tmp=$(mktemp)
  curl -fsSL "$url" -o "$tmp"
  if [[ "$url" == *.zip ]]; then
    unzip -q -j "$tmp" -d "$OUT_DIR"
    rm -f "$tmp"
  elif [[ "$url" == *.tar.gz ]] || [[ "$url" == *.tgz ]]; then
    local extract bin
    extract=$(mktemp -d)
    tar -xzf "$tmp" -C "$extract"
    rm -f "$tmp"
    bin=$(find "$extract" -type f -name "${dest_name}*" | head -1)
    if [[ -z "$bin" ]]; then
      echo "Binary not found in archive: $repo $pattern" >&2
      return 1
    fi
    cp "$bin" "$dest"
    chmod +x "$dest"
    rm -rf "$extract"
  else
    mv "$tmp" "$dest"
    chmod +x "$dest"
  fi
}

if ! fetch_github "SagerNet/sing-box" 'linux-amd64-glibc\.tar\.gz$' "sing-box"; then
  fetch_github "SagerNet/sing-box" 'linux-amd64\.tar\.gz$' "sing-box"
fi
fetch_github "XTLS/Xray-core" 'linux-64\.zip$' "xray"
fetch_github "apernet/hysteria" 'hysteria-linux-amd64$' "hysteria"
fetch_github "xjasonlyu/tun2socks" 'tun2socks-linux-amd64-v3\.zip$' "tun2socks"

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
