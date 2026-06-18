#pragma once

#include "vicvpn/model/ServerProfile.h"
#include "vicvpn/app/Settings.h"
#include <QString>

namespace vicvpn {

class SingboxConfigBuilder {
public:
    static QString build(const ServerProfile& server, const AppSettings& settings,
                         const QString& bindInterface = QString(),
                         const QString& bindLocalIp = QString());
    /** Local SOCKS gateway (used with tun2socks — Outline-style). */
    static QString buildSocksGateway(const ServerProfile& server, const AppSettings& settings,
                                     const QString& bindInterface, const QString& bindLocalIp,
                                     int socksPort);
};

} // namespace vicvpn
