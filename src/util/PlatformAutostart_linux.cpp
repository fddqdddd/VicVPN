#include "vicvpn/util/PlatformAutostart.h"
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace vicvpn {

static QString desktopFilePath() {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart";
    QDir().mkpath(dir);
    return dir + "/vicvpn.desktop";
}

bool PlatformAutostart::isEnabled() {
    return QFile::exists(desktopFilePath());
}

bool PlatformAutostart::setEnabled(bool enabled, const QString& exePath) {
    const QString path = desktopFilePath();
    if (!enabled) {
        QFile::remove(path);
        return true;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QTextStream out(&f);
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=VicVPN\n";
    out << "Exec=" << exePath << "\n";
    out << "X-GNOME-Autostart-enabled=true\n";
    return true;
}

} // namespace vicvpn
