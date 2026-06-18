#pragma once

#include <QString>

namespace vicvpn {

class WindowsAutostart {
public:
    static bool isEnabled();
    static bool setEnabled(bool enabled, const QString& exePath);
};

} // namespace vicvpn
