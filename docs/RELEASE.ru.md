# VicVPN beta — публикация на GitHub

## Артефакты

| Платформа | Файл |
|-----------|------|
| Windows installer | `dist/VicVPN-windows-0.1.0-beta-setup.exe` |
| Windows portable | `dist/VicVPN-windows-0.1.0-beta-portable.zip` |
| Linux x64 | `dist/VicVPN-linux-x64-0.1.0-beta.tar.gz` (собирается на Linux / CI) |

## Сборка локально

**Windows:**
```bat
build-release.bat
```

**Linux (Ubuntu 24.04):**
```bash
sudo apt install cmake g++ qt6-base-dev qt6-svg-dev libsqlite3-dev nlohmann-json3-dev
./tools/package-linux.sh
```

## GitHub

```bash
git init
git add .
git commit -m "VicVPN 0.1.0-beta: Windows + Linux"
git branch -M main
git remote add origin https://github.com/YOUR_USER/VicVPN.git
git push -u origin main
git tag v0.1.0-beta
git push origin v0.1.0-beta
```

Тег `v0.1.0-beta` запускает workflow `.github/workflows/release.yml` — сборка Windows + Linux и GitHub Release.

## Linux — проверка после установки

```bash
sudo ./install.sh
pkexec /opt/vicvpn/VicVPN
```

TUN требует root (pkexec). VLESS/VMess через sing-box native TUN.

## Дальше

- macOS: Qt6 + sing-box TUN (Network Extension)
- Android: отдельный проект (Kotlin + sing-box lib)
