#pragma once

#include <QString>

namespace vicvpn {

class PlatformAutostart {
public:
    static bool isEnabled();
    static bool setEnabled(bool enabled, const QString& exePath);
};

} // namespace vicvpn
