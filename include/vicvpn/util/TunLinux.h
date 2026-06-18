#pragma once

#include "vicvpn/util/BypassRoute.h"
#include <QString>

namespace vicvpn {

class TunLinux {
public:
    static QString sessionTunName();
    static QString singboxTunName();
    static void purgeWintunPool();
    static void cleanupStaleDevices();
    static BypassRoute resolveBypassRoute(const QString& bypassHost);
    static bool ensureProxyBypass(const QString& bypassHost, const BypassRoute& preResolvedBypass = {},
                                  const QString& activeTunName = {});
    static bool ensureRoutes(const QString& tunName, const QString& bypassHost,
                             const BypassRoute& preResolvedBypass = {}, bool diagnostic = false);
    static void removeRoutes(const QString& tunName, const QString& bypassHost,
                             const BypassRoute& preResolvedBypass = {});
    static int tunInterfaceIndex(const QString& tunName);
    static void logNetworkAdapters();
};

} // namespace vicvpn
