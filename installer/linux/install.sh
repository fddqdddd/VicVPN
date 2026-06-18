#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID:-0}" -ne 0 ]]; then
  echo "Run as root: sudo ./install.sh"
  exec sudo bash "$0" "$@"
fi

SRC="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${PREFIX:-/opt/vicvpn}"
DESKTOP_DIR="/usr/share/applications"

echo "Installing VicVPN to $PREFIX"
install -d "$PREFIX/core" "$PREFIX/styles"
install -m 755 "$SRC/VicVPN" "$PREFIX/"
cp -a "$SRC/core/." "$PREFIX/core/"
cp -a "$SRC/styles/." "$PREFIX/styles/"
[[ -f "$SRC/LICENSE" ]] && install -m 644 "$SRC/LICENSE" "$PREFIX/"

cat > "$DESKTOP_DIR/vicvpn.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=VicVPN
Comment=VPN client (beta)
Exec=pkexec $PREFIX/VicVPN
Icon=network-vpn
Terminal=false
Categories=Network;
EOF

ln -sf "$PREFIX/VicVPN" /usr/local/bin/vicvpn 2>/dev/null || true

echo "[OK] Installed. Launch: pkexec $PREFIX/VicVPN"
echo "     or: vicvpn (via pkexec in .desktop)"
