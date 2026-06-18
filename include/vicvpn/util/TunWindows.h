#pragma once

#include "vicvpn/util/BypassRoute.h"
#include <QString>

namespace vicvpn {

class TunWindows {
public:
    static QString sessionTunName();
    /** Fixed Wintun pool name used by sing-box TUN inbound. */
    static QString singboxTunName();
    /** Remove stale .wintun pool files (safe before sing-box start). */
    static void purgeWintunPool();
    static void cleanupStaleDevices();
    /** Resolve physical route to proxy host (call before Xray starts). */
    static BypassRoute resolveBypassRoute(const QString& bypassHost);
    /** /32 to VPN server via physical IF — call before tun2socks dials the server. */
    static bool ensureProxyBypass(const QString& bypassHost, const BypassRoute& preResolvedBypass = {},
                                  const QString& activeTunName = {});
    /** Add 0.0.0.0/0 via TUN and direct /32 to proxy host on physical IF. */
    static bool ensureRoutes(const QString& tunName, const QString& bypassHost,
                             const BypassRoute& preResolvedBypass = {}, bool diagnostic = false);
    static void removeRoutes(const QString& tunName, const QString& bypassHost,
                             const BypassRoute& preResolvedBypass = {});
    static int tunInterfaceIndex(const QString& tunName);
    static void logNetworkAdapters();
};

} // namespace vicvpn
