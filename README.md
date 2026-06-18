# VicVPN (beta)

VPN/proxy client inspired by Happ. **Windows** and **Linux** (beta). Cores: sing-box, Xray, Hysteria2.

## Downloads (beta)

| Platform | File |
|----------|------|
| Windows | `VicVPN-windows-0.1.0-beta-setup.exe` |
| Windows portable | `VicVPN-windows-0.1.0-beta-portable.zip` |
| Linux x64 | `VicVPN-linux-x64-0.1.0-beta.tar.gz` |

macOS and Android — planned.

## Windows

Requirements: Windows 10+ x64, admin for TUN.

```bat
build-mingw.bat
build-release.bat
```

Output: `dist\VicVPN-windows-*-setup.exe`

## Linux

Requirements: Qt6, CMake, root/pkexec for TUN.

```bash
sudo apt install cmake g++ qt6-base-dev qt6-svg-dev libsqlite3-dev nlohmann-json3-dev
./build-linux.sh
./tools/package-linux.sh
```

Install:

```bash
tar -xzf dist/VicVPN-linux-x64-0.1.0-beta.tar.gz
cd VicVPN-linux-x64
sudo ./install.sh
```

Run: `pkexec /opt/vicvpn/VicVPN` or `vicvpn` from app menu.

## GitHub release

```bash
git tag v0.1.0-beta
git push origin v0.1.0-beta
```

CI builds Windows + Linux artifacts and publishes a GitHub Release.

## License

GPL-3.0 — see [LICENSE](LICENSE).
