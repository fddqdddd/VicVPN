#pragma once

#include <QString>

namespace vicvpn {

class CoreHealth {
public:
    static bool xrayHasFatalTunError(const QString& log);
    static bool xrayHasConfigError(const QString& log);
    static bool xrayIndicatesStarted(const QString& log);
    /** Xray alive and no fatal TUN errors in captured log. */
    static bool xrayLooksReady(const QString& log, bool running);
    static QString formatLogTail(const QString& log, int maxLines = 5);
    static bool tun2socksHasFatal(const QString& log);
    static QString tun2socksErrorMessage(const QString& log);
    static QString singboxErrorMessage(const QString& log);
    /** sing-box / SOCKS log shows proxy cannot reach the VPN server. */
    static bool proxyGatewayFailed(const QString& log);
    /** SOCKS5 CONNECT to target via local gateway (validates end-to-end proxy). */
    static bool testSocks5Connect(int socksPort, const QString& targetHost, int targetPort,
                                  int timeoutMs = 8000);
    /** Plain TCP connect to host:port (pre-flight before TUN). */
    static bool testTcpConnect(const QString& host, int port, int timeoutMs = 5000);
};

} // namespace vicvpn
