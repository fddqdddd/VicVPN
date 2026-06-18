#include "vicvpn/util/WindowsAutostart.h"
#include <QSettings>

namespace vicvpn {

static const char* kRunKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const char* kAppName = "VicVPN";

bool WindowsAutostart::isEnabled() {
    QSettings settings("HKEY_CURRENT_USER\\" + QString(kRunKey), QSettings::NativeFormat);
    return settings.contains(kAppName);
}

bool WindowsAutostart::setEnabled(bool enabled, const QString& exePath) {
    QSettings settings("HKEY_CURRENT_USER\\" + QString(kRunKey), QSettings::NativeFormat);
    if (enabled) {
        if (exePath.isEmpty())
            return false;
        settings.setValue(kAppName, '"' + exePath + '"');
    } else {
        settings.remove(kAppName);
    }
    return true;
}

} // namespace vicvpn
