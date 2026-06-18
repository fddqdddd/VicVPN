#include "vicvpn/util/PlatformAutostart.h"
#include "vicvpn/util/WindowsAutostart.h"

namespace vicvpn {

bool PlatformAutostart::isEnabled() {
    return WindowsAutostart::isEnabled();
}

bool PlatformAutostart::setEnabled(bool enabled, const QString& exePath) {
    return WindowsAutostart::setEnabled(enabled, exePath);
}

} // namespace vicvpn
