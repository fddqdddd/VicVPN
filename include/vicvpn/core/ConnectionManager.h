#pragma once

#include "vicvpn/model/ServerProfile.h"
#include "vicvpn/core/CoreManagers.h"
#include "vicvpn/util/BypassRoute.h"
#include <QObject>
#include <QTimer>

namespace vicvpn {

enum class ConnectionState { Disconnected, Connecting, Connected, Error };

enum class CoreEngine { Singbox, Hysteria2, XrayLegacy };

class ConnectionManager : public QObject {
    Q_OBJECT
public:
    explicit ConnectionManager(QObject* parent = nullptr);
    ConnectionState state() const { return state_; }
    QString statusText() const { return statusText_; }
    bool connectServer(const ServerProfile& server);
    void disconnect();
    qint64 connectedServerId() const { return connectedId_; }

signals:
    void stateChanged(int state, const QString& text);
    void logLine(const QString& line);

private:
    bool writeConfig(const ServerProfile& server, QString* configPath);
    void setState(ConnectionState s, const QString& text);
    void scheduleProxyVerify(int delayMs);
    void scheduleConnectVerify(int delayMs);
    void startTunnelPhase();
    void abortConnection(const QString& message);

    SingboxCoreManager singbox_;
    XrayCoreManager xray_;
    HysteriaCoreManager hy2_;
    Tun2socksManager tun2socks_;
    CoreEngine engine_ = CoreEngine::Singbox;
    static constexpr int kSocksPort = 10808;
    ConnectionState state_ = ConnectionState::Disconnected;
    QString statusText_;
    qint64 connectedId_ = 0;
    quint64 connectAttempt_ = 0;
    quint64 verifyAttempt_ = 0;
    int verifyRetriesLeft_ = 0;
    int proxyVerifyRetriesLeft_ = 0;
    bool tunnelStarted_ = false;
    bool useDirectSs_ = false;
    QString directSsProxy_;
    QString activeTunName_;
    QString bypassHost_;
    BypassRoute bypassRoute_;
    QString lastSingboxConfig_;
    QTimer* bypassTimer_ = nullptr;
    QTimer* proxyWatchdog_ = nullptr;
};

} // namespace vicvpn
