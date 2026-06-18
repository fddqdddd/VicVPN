#pragma once

#include <QString>

namespace vicvpn {

class NetworkRoute {
public:
    /** Local IPv4 that Windows would use to reach host:port (for sendThrough). */
    static QString localIpv4ForHost(const QString& host, int port = 443);
    /** Friendly adapter name for a local IPv4, e.g. "Ethernet". */
    static QString interfaceNameForLocalIpv4(const QString& localIp);
    /** Friendly adapter name by Windows ifIndex. */
    static QString interfaceNameForIfIndex(int ifIndex);
    /** True if a network adapter with this friendly name exists and is up. */
    static bool isTunAdapterPresent(const QString& name);
    static bool isTunAdapterPresent();
};

} // namespace vicvpn
