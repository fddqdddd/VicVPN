#pragma once

#include "vicvpn/model/ServerProfile.h"
#include "vicvpn/app/Settings.h"
#include "vicvpn/util/BypassRoute.h"
#include <QString>

namespace vicvpn {

class XrayConfigBuilder {
public:
    static QString build(const ServerProfile& server, const AppSettings& settings,
                         const QString& tunName = QString(),
                         const BypassRoute& bypassRoute = {});
    /** Outline-style: Xray SOCKS gateway only (no TUN). Pair with tun2socks. */
    static QString buildSocksGateway(const ServerProfile& server, const AppSettings& settings,
                                     const BypassRoute& bypassRoute = {},
                                     int socksPort = 10808);
    /** Shadowsocks URL for tun2socks direct proxy (fastest path). */
    static QString buildShadowsocksProxyUrl(const ServerProfile& server);
    static bool supportsDirectShadowsocksTunnel(const ServerProfile& server,
                                                const AppSettings& settings);
    static QString buildPassthrough(const nlohmann::json& config);
};

class Hy2ConfigBuilder {
public:
    static QString buildYaml(const ServerProfile& server, const AppSettings& settings);
};

} // namespace vicvpn
