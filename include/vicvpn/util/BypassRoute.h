#pragma once

#include <QString>
#include <QtGlobal>

namespace vicvpn {

struct BypassRoute {
    quint32 ifIndex = 0;
    quint32 nextHop = 0;
    bool valid = false;
    QString ifaceName;
    QString gateway;
};

} // namespace vicvpn
