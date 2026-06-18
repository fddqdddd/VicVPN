#include "vicvpn/core/ConnectionManager.h"
#include "vicvpn/config/ConfigBuilders.h"
#include "vicvpn/config/SingboxConfigBuilder.h"
#include "vicvpn/app/Settings.h"
#include "vicvpn/app/I18n.h"
#include "vicvpn/core/CoreHealth.h"
#include "vicvpn/parser/UriParser.h"
#include "vicvpn/parser/SsconfCountry.h"
#include "vicvpn/parser/ImportService.h"
#include "vicvpn/parser/SingboxConverter.h"
#include "vicvpn/util/AdminElevation.h"
#include "vicvpn/util/AppPaths.h"
#include "vicvpn/util/TunPlatform.h"
#include "vicvpn/util/NetworkRoute.h"
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTimer>

namespace vicvpn {

static bool usesSingboxNativeTun() {
#if defined(Q_OS_WIN)
    return false;
#else
    return true;
#endif
}

static QString proxyHostFromOutbound(const nlohmann::json& ob) {
    if (!ob.contains("settings"))
        return {};
    const auto& st = ob["settings"];
    if (st.contains("vnext") && st["vnext"].is_array() && !st["vnext"].empty())
        return QString::fromStdString(st["vnext"][0].value("address", ""));
    if (st.contains("servers") && st["servers"].is_array() && !st["servers"].empty())
        return QString::fromStdString(st["servers"][0].value("address", ""));
    return {};
}

static QString proxyHostFromSingbox(const nlohmann::json& j) {
    if (!j.contains("outbounds") || !j["outbounds"].is_array())
        return {};
    for (const auto& ob : j["outbounds"]) {
        const std::string type = ob.value("type", "");
        if (type == "direct" || type == "block" || type == "dns" || type == "selector" ||
            type == "urltest" || type == "tun")
            continue;
        if (ob.contains("server"))
            return QString::fromStdString(ob["server"].get<std::string>());
    }
    return {};
}

static QString proxyHostFromServer(const ServerProfile& server) {
    if (server.core == CoreType::Hysteria2)
        return {};

    QString host = proxyHostFromOutbound(server.xrayOutbound);
    if (!host.isEmpty())
        return host;

    if (server.passthroughJson && SingboxConverter::isSingbox(server.passthroughConfig)) {
        host = proxyHostFromSingbox(server.passthroughConfig);
        if (!host.isEmpty())
            return host;
    }

    const QString raw = server.rawUri.trimmed();
    if (raw.startsWith('{')) {
        try {
            const auto j = nlohmann::json::parse(raw.toStdString());
            if (j.contains("server"))
                return QString::fromStdString(j["server"].get<std::string>());
            if (SingboxConverter::isSingbox(j))
                return proxyHostFromSingbox(j);
        } catch (...) {
        }
    }
    return {};
}

static int proxyPortFromOutbound(const nlohmann::json& ob) {
    if (!ob.contains("settings"))
        return 443;
    const auto& st = ob["settings"];
    if (st.contains("servers") && st["servers"].is_array() && !st["servers"].empty())
        return st["servers"][0].value("port", 443);
    if (st.contains("vnext") && st["vnext"].is_array() && !st["vnext"].empty())
        return st["vnext"][0].value("port", 443);
    return 443;
}

static int proxyPortFromServer(const ServerProfile& server) {
    const int fromXray = proxyPortFromOutbound(server.xrayOutbound);
    if (server.xrayOutbound.contains("settings"))
        return fromXray;

    if (server.passthroughJson && SingboxConverter::isSingbox(server.passthroughConfig)) {
        for (const auto& ob : server.passthroughConfig["outbounds"]) {
            const std::string type = ob.value("type", "");
            if (type == "direct" || type == "block" || type == "dns")
                continue;
            if (ob.contains("server_port")) {
                if (ob["server_port"].is_number_integer())
                    return ob["server_port"].get<int>();
                if (ob["server_port"].is_string()) {
                    try {
                        return std::stoi(ob["server_port"].get<std::string>());
                    } catch (...) {
                    }
                }
            }
        }
    }

    const QString raw = server.rawUri.trimmed();
    if (raw.startsWith('{')) {
        try {
            const auto j = nlohmann::json::parse(raw.toStdString());
            if (j.contains("server_port")) {
                if (j["server_port"].is_number_integer())
                    return j["server_port"].get<int>();
            }
        } catch (...) {
        }
    }
    return fromXray;
}

static ServerProfile freshServerProfile(const ServerProfile& server) {
    ServerProfile active = server;
    if (server.core == CoreType::Hysteria2)
        return active;

    const QString raw = server.rawUri.trimmed();
    if (raw.startsWith("vless://", Qt::CaseInsensitive) || raw.startsWith("vmess://", Qt::CaseInsensitive) ||
        raw.startsWith("ss://", Qt::CaseInsensitive) || raw.startsWith("trojan://", Qt::CaseInsensitive) ||
        raw.startsWith("socks://", Qt::CaseInsensitive) || raw.startsWith("socks5://", Qt::CaseInsensitive)) {
        if (auto parsed = UriParser::parse(raw)) {
            active.xrayOutbound = parsed->xrayOutbound;
            active.protocol = parsed->protocol;
            active.passthroughJson = parsed->passthroughJson;
            active.passthroughConfig = parsed->passthroughConfig;
            active.core = parsed->core;
            if (active.name.isEmpty())
                active.name = parsed->name;
        }
    }
    return active;
}

static ServerProfile resolveActiveServer(const ServerProfile& server, QString* error) {
    if (SsconfCountry::isSsconfUri(server.subscriptionUrl)) {
        auto refreshed =
            SsconfResolver::resolve(server.subscriptionUrl, error, server.countryCode);
        if (refreshed.empty())
            return {};
        ServerProfile active = SsconfResolver::pickByCountry(refreshed, server.countryCode);
        active.id = server.id;
        active.subscriptionUrl = server.subscriptionUrl;
        active.countryCode = server.countryCode;
        return active;
    }
    return freshServerProfile(server);
}

static bool hasOutlinePrefix(const ServerProfile& server) {
    const auto& ob = server.xrayOutbound;
    if (!ob.contains("settings"))
        return false;
    const auto& st = ob["settings"];
    if (!st.contains("servers") || !st["servers"].is_array() || st["servers"].empty())
        return false;
    return st["servers"][0].contains("prefix");
}

static bool hasProxyConfig(const ServerProfile& server) {
    if (server.core == CoreType::Hysteria2)
        return !server.hy2Config.empty();
    if (!server.xrayOutbound.empty())
        return true;
    return server.passthroughJson && !server.passthroughConfig.empty();
}

static CoreEngine resolveEngine(const ServerProfile& server, const AppSettings& settings) {
    if (server.core == CoreType::Hysteria2)
        return CoreEngine::Hysteria2;
    if (settings.useLegacyCore)
        return CoreEngine::XrayLegacy;
    return CoreEngine::Singbox;
}

ConnectionManager::ConnectionManager(QObject* parent) : QObject(parent) {
    bypassTimer_ = new QTimer(this);
    bypassTimer_->setInterval(3000);
    connect(bypassTimer_, &QTimer::timeout, this, [this]() {
        if (state_ != ConnectionState::Connected || bypassHost_.isEmpty())
            return;
        const QString tun = activeTunName_.isEmpty() ? TunPlatform::singboxTunName() : activeTunName_;
        TunPlatform::ensureProxyBypass(bypassHost_, bypassRoute_, tun);
    });

    proxyWatchdog_ = new QTimer(this);
    proxyWatchdog_->setInterval(2500);
    connect(proxyWatchdog_, &QTimer::timeout, this, [this]() {
        if (state_ != ConnectionState::Connected || !tunnelStarted_)
            return;
        QString log;
        if (engine_ == CoreEngine::Singbox)
            log = singbox_.recentLog() + tun2socks_.recentLog();
        else if (engine_ == CoreEngine::XrayLegacy)
            log = xray_.recentLog() + tun2socks_.recentLog();
        else
            return;
        if (CoreHealth::proxyGatewayFailed(log)) {
            QString msg = VTR("error.server_unreachable");
            if (!bypassHost_.isEmpty())
                msg += QString(" (%1)").arg(bypassHost_);
            abortConnection(msg);
        }
    });

    connect(&singbox_, &SingboxCoreManager::logLine, this, &ConnectionManager::logLine);
    connect(&xray_, &XrayCoreManager::logLine, this, &ConnectionManager::logLine);
    connect(&hy2_, &HysteriaCoreManager::logLine, this, &ConnectionManager::logLine);
    connect(&tun2socks_, &Tun2socksManager::logLine, this, &ConnectionManager::logLine);

    auto onStopped = [this](const QString& prefix, int code) {
        if (state_ != ConnectionState::Connected && state_ != ConnectionState::Connecting)
            return;
        abortConnection(prefix + QString::number(code));
    };
    connect(&singbox_, &SingboxCoreManager::stopped, this, [this](int code) {
        if (state_ != ConnectionState::Connected && state_ != ConnectionState::Connecting)
            return;
        QString msg = CoreHealth::singboxErrorMessage(singbox_.recentLog());
        if (msg.isEmpty())
            msg = VTR("error.core_failed") + QStringLiteral(" sing-box ") + QString::number(code);
        abortConnection(msg);
    });
    connect(&xray_, &XrayCoreManager::stopped, this,
            [onStopped](int code) { onStopped(QStringLiteral("xray "), code); });
    connect(&hy2_, &HysteriaCoreManager::stopped, this,
            [onStopped](int code) { onStopped(QStringLiteral("hysteria "), code); });
    connect(&tun2socks_, &Tun2socksManager::stopped, this, [this](int code) {
        if (!tunnelStarted_)
            return;
        if (state_ != ConnectionState::Connected && state_ != ConnectionState::Connecting)
            return;
        if (engine_ != CoreEngine::Singbox && engine_ != CoreEngine::XrayLegacy)
            return;
        QString msg = CoreHealth::tun2socksErrorMessage(tun2socks_.recentLog());
        if (msg.isEmpty())
            msg = VTR("error.core_failed") + " tun2socks " + QString::number(code);
        abortConnection(msg);
    });
}

void ConnectionManager::abortConnection(const QString& message) {
    ++connectAttempt_;
    setState(ConnectionState::Error, message);
    if (!activeTunName_.isEmpty())
        TunPlatform::removeRoutes(activeTunName_, bypassHost_, bypassRoute_);
    const bool wasSingbox = engine_ == CoreEngine::Singbox;
    singbox_.stop();
    tun2socks_.stop();
    xray_.stop();
    hy2_.stop();
    if (wasSingbox) {
        QThread::msleep(400);
        TunPlatform::purgeWintunPool();
    }
    if (!bypassHost_.isEmpty() && bypassRoute_.valid)
        TunPlatform::removeRoutes(QString(), bypassHost_, bypassRoute_);
    connectedId_ = 0;
    activeTunName_.clear();
    bypassHost_.clear();
    bypassRoute_ = {};
    tunnelStarted_ = false;
    useDirectSs_ = false;
    directSsProxy_.clear();
    engine_ = CoreEngine::Singbox;
}

void ConnectionManager::setState(ConnectionState s, const QString& text) {
    state_ = s;
    statusText_ = text;
    if (s == ConnectionState::Connected && engine_ == CoreEngine::Singbox)
        bypassTimer_->start();
    else
        bypassTimer_->stop();
    if (s == ConnectionState::Connected && tunnelStarted_ &&
        (engine_ == CoreEngine::Singbox || engine_ == CoreEngine::XrayLegacy))
        proxyWatchdog_->start();
    else
        proxyWatchdog_->stop();
    emit stateChanged(static_cast<int>(s), text);
}

void ConnectionManager::startTunnelPhase() {
    if (tunnelStarted_)
        return;
    tunnelStarted_ = true;

    const int mtu = Settings::instance().get().mtu;
    QString proxy;
    if (engine_ == CoreEngine::Singbox && useDirectSs_ && !directSsProxy_.isEmpty()) {
        singbox_.stop();
        proxy = directSsProxy_;
    } else if (engine_ == CoreEngine::Singbox || engine_ == CoreEngine::XrayLegacy) {
        proxy = QString("socks5://127.0.0.1:%1").arg(kSocksPort);
    }

    if (engine_ == CoreEngine::Singbox || engine_ == CoreEngine::XrayLegacy) {
        if (!tun2socks_.start(proxy, activeTunName_, {}, mtu)) {
            tunnelStarted_ = false;
            if (engine_ == CoreEngine::Singbox)
                singbox_.stop();
            else
                xray_.stop();
            setState(ConnectionState::Error, VTR("error.core_start_failed") + " " + tun2socks_.lastError());
            return;
        }
    }

    verifyRetriesLeft_ = 15;
    scheduleConnectVerify(2000);
}

void ConnectionManager::scheduleProxyVerify(int delayMs) {
    QTimer::singleShot(delayMs, this, [this]() {
        if (connectAttempt_ != verifyAttempt_)
            return;
        if (state_ != ConnectionState::Connecting)
            return;

        if (engine_ == CoreEngine::Singbox && !singbox_.isRunning()) {
            QString msg = CoreHealth::singboxErrorMessage(singbox_.recentLog());
            if (msg.isEmpty())
                msg = VTR("error.core_start_failed");
            abortConnection(msg);
            return;
        }
        if (engine_ == CoreEngine::XrayLegacy && !xray_.isRunning()) {
            abortConnection(VTR("error.core_start_failed"));
            return;
        }

        const QString log = engine_ == CoreEngine::Singbox ? singbox_.recentLog() : xray_.recentLog();
        if (CoreHealth::proxyGatewayFailed(log)) {
            QString msg = VTR("error.server_unreachable");
            if (!bypassHost_.isEmpty())
                msg += QString(" (%1)").arg(bypassHost_);
            abortConnection(msg);
            return;
        }

        if (!CoreHealth::testSocks5Connect(kSocksPort, QStringLiteral("1.1.1.1"), 443)) {
            if (proxyVerifyRetriesLeft_ > 0) {
                --proxyVerifyRetriesLeft_;
                scheduleProxyVerify(1500);
                return;
            }
            QString msg = VTR("error.server_unreachable");
            if (!bypassHost_.isEmpty())
                msg += QString(" (%1)").arg(bypassHost_);
            abortConnection(msg);
            return;
        }

        startTunnelPhase();
    });
}

bool ConnectionManager::writeConfig(const ServerProfile& server, QString* configPath) {
    const QString dir = AppPaths::runtimeDir();
    const AppSettings& settings = Settings::instance().get();

    if (engine_ == CoreEngine::Hysteria2) {
        const QString path = dir + "/hy2.yaml";
        const QString yaml = Hy2ConfigBuilder::buildYaml(server, settings);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(yaml.toUtf8());
        *configPath = path;
        return true;
    }

    if (engine_ == CoreEngine::Singbox) {
        const QString path = dir + "/sing-box.json";
        QString bindIface;
        QString bindLocalIp;
        if (bypassRoute_.valid) {
#if defined(Q_OS_WIN)
            bindIface = NetworkRoute::interfaceNameForIfIndex(static_cast<int>(bypassRoute_.ifIndex));
#else
            bindIface = bypassRoute_.ifaceName;
#endif
            bindLocalIp = NetworkRoute::localIpv4ForHost(bypassHost_, 443);
        } else if (!bypassHost_.isEmpty()) {
            bindLocalIp = NetworkRoute::localIpv4ForHost(bypassHost_, 443);
            bindIface = NetworkRoute::interfaceNameForLocalIpv4(bindLocalIp);
        }
        const QString json = usesSingboxNativeTun()
                                 ? SingboxConfigBuilder::build(server, settings, bindIface, bindLocalIp)
                                 : SingboxConfigBuilder::buildSocksGateway(server, settings, bindIface,
                                                                           bindLocalIp, kSocksPort);
        if (json.isEmpty())
            return false;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(json.toUtf8());
        *configPath = path;
        return true;
    }

    const QString path = dir + "/xray.json";
    const QString json = XrayConfigBuilder::buildSocksGateway(server, settings, bypassRoute_, kSocksPort);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(json.toUtf8());
    *configPath = path;
    return true;
}

void ConnectionManager::scheduleConnectVerify(int delayMs) {
    QTimer::singleShot(delayMs, this, [this]() {
        if (connectAttempt_ != verifyAttempt_)
            return;

        bool running = false;
        QString log;
        if (engine_ == CoreEngine::Hysteria2) {
            running = hy2_.isRunning();
            log = hy2_.recentLog();
        } else if (engine_ == CoreEngine::Singbox) {
            if (usesSingboxNativeTun())
                running = singbox_.isRunning();
            else if (useDirectSs_ && !singbox_.isRunning())
                running = tun2socks_.isRunning();
            else
                running = singbox_.isRunning() && tun2socks_.isRunning();
            log = singbox_.recentLog() + tun2socks_.recentLog();
        } else {
            running = xray_.isRunning() && tun2socks_.isRunning();
            log = xray_.recentLog() + tun2socks_.recentLog();
        }

        if (!running) {
            QString msg;
            if (engine_ == CoreEngine::Singbox)
                msg = CoreHealth::singboxErrorMessage(log);
            if (msg.isEmpty())
                msg = CoreHealth::xrayHasConfigError(log) ? VTR("error.core_config")
                                                          : VTR("error.core_start_failed");
            const QString tail = CoreHealth::formatLogTail(log);
            if (!tail.isEmpty())
                msg += "\n" + tail;
            abortConnection(msg);
            return;
        }

        if (engine_ == CoreEngine::Singbox || engine_ == CoreEngine::XrayLegacy) {
            if (!tunnelStarted_)
                return;
            if (!bypassHost_.isEmpty())
                TunPlatform::ensureProxyBypass(bypassHost_, bypassRoute_, activeTunName_);
            const bool lastTry = verifyRetriesLeft_ == 0;
            if (!TunPlatform::ensureRoutes(activeTunName_, bypassHost_, bypassRoute_, lastTry)) {
                if (verifyRetriesLeft_ > 0) {
                    --verifyRetriesLeft_;
                    scheduleConnectVerify(600);
                    return;
                }
                abortConnection(VTR("error.tun_routes_no_traffic"));
                return;
            }
        }

        if ((engine_ == CoreEngine::Singbox || engine_ == CoreEngine::XrayLegacy) &&
            CoreHealth::proxyGatewayFailed(log)) {
            QString msg = VTR("error.server_unreachable");
            if (!bypassHost_.isEmpty())
                msg += QString(" (%1)").arg(bypassHost_);
            abortConnection(msg);
            return;
        }

        setState(ConnectionState::Connected, VTR("status.connected"));
    });
}

bool ConnectionManager::connectServer(const ServerProfile& server) {
    QString resolveError;
    const ServerProfile active = resolveActiveServer(server, &resolveError);
    if (!hasProxyConfig(active)) {
        QString msg = resolveError;
        if (SsconfCountry::isSsconfUri(server.subscriptionUrl)) {
            if (msg.isEmpty())
                msg = VTR("error.ssconf_fetch");
            else
                msg = VTR("error.ssconf_fetch") + "\n" + msg;
        } else if (msg.isEmpty()) {
            msg = VTR("error.core_config");
        }
        setState(ConnectionState::Error, msg);
        return false;
    }
    disconnect();
    AppPaths::clearRuntimeLogs();

    if (!isRunningAsAdmin()) {
        setState(ConnectionState::Error, VTR("admin.required"));
        return false;
    }

    engine_ = resolveEngine(active, Settings::instance().get());
    useDirectSs_ = false;
    directSsProxy_.clear();
    const AppSettings& appSettings = Settings::instance().get();
#if defined(Q_OS_WIN)
    if (engine_ == CoreEngine::Singbox &&
        (XrayConfigBuilder::supportsDirectShadowsocksTunnel(active, appSettings) ||
         hasOutlinePrefix(active))) {
        useDirectSs_ = true;
        directSsProxy_ = XrayConfigBuilder::buildShadowsocksProxyUrl(active);
    }
#endif

    bypassHost_.clear();
    bypassRoute_ = {};
    if (engine_ != CoreEngine::Hysteria2) {
        bypassHost_ = proxyHostFromServer(active);
        if (!bypassHost_.isEmpty())
            bypassRoute_ = TunPlatform::resolveBypassRoute(bypassHost_);
    }

    if (!bypassHost_.isEmpty() && engine_ != CoreEngine::Hysteria2) {
        const int proxyPort = proxyPortFromServer(active);
        if (!CoreHealth::testTcpConnect(bypassHost_, proxyPort, 5000)) {
            QString msg = VTR("error.server_unreachable");
            msg += QString(" (%1:%2)").arg(bypassHost_).arg(proxyPort);
            setState(ConnectionState::Error, msg);
            return false;
        }
    }

    if (engine_ == CoreEngine::Hysteria2) {
        if (!QFileInfo::exists(AppPaths::hysteriaExe())) {
            setState(ConnectionState::Error, VTR("error.no_core"));
            return false;
        }
    } else if (engine_ == CoreEngine::Singbox) {
        if (!QFileInfo::exists(AppPaths::singboxExe())) {
            setState(ConnectionState::Error, VTR("error.no_singbox"));
            return false;
        }
#if defined(Q_OS_WIN)
        if (!QFileInfo::exists(AppPaths::tun2socksExe())) {
            setState(ConnectionState::Error, VTR("error.no_tun2socks"));
            return false;
        }
        AppPaths::ensureWintunDll();
        TunPlatform::purgeWintunPool();
        activeTunName_ = TunPlatform::sessionTunName();
#else
        activeTunName_ = TunPlatform::singboxTunName();
#endif
        if (!bypassHost_.isEmpty())
            TunPlatform::ensureProxyBypass(bypassHost_, bypassRoute_);
    } else {
        if (!QFileInfo::exists(AppPaths::xrayExe()) || !QFileInfo::exists(AppPaths::tun2socksExe())) {
            setState(ConnectionState::Error, VTR("error.no_core"));
            return false;
        }
        AppPaths::ensureWintunDll();
        activeTunName_ = TunPlatform::sessionTunName();
        if (!bypassHost_.isEmpty())
            TunPlatform::ensureProxyBypass(bypassHost_, bypassRoute_);
    }

    QString configPath;
    if (!writeConfig(active, &configPath)) {
        setState(ConnectionState::Error, "Config write failed");
        return false;
    }

    setState(ConnectionState::Connecting, VTR("status.connecting"));
    connectedId_ = active.id;

    if (engine_ == CoreEngine::Hysteria2) {
        if (!hy2_.start(configPath)) {
            setState(ConnectionState::Error, VTR("error.core_start_failed") + " " + hy2_.lastError());
            return false;
        }
        tunnelStarted_ = false;
        verifyAttempt_ = ++connectAttempt_;
        verifyRetriesLeft_ = 0;
        scheduleConnectVerify(2000);
        return true;
    }
    if (engine_ == CoreEngine::Singbox) {
        lastSingboxConfig_ = configPath;
#if defined(Q_OS_WIN)
        if (useDirectSs_ && hasOutlinePrefix(active) && !directSsProxy_.isEmpty()) {
            tunnelStarted_ = false;
            verifyAttempt_ = ++connectAttempt_;
            verifyRetriesLeft_ = 15;
            startTunnelPhase();
            return true;
        }
#endif
        if (!singbox_.start(configPath)) {
            setState(ConnectionState::Error, VTR("error.core_start_failed") + " " + singbox_.lastError());
            return false;
        }
        verifyAttempt_ = ++connectAttempt_;
        if (usesSingboxNativeTun()) {
            tunnelStarted_ = true;
            if (!bypassHost_.isEmpty())
                TunPlatform::ensureProxyBypass(bypassHost_, bypassRoute_, activeTunName_);
            verifyRetriesLeft_ = 10;
            scheduleConnectVerify(2000);
            return true;
        }
        tunnelStarted_ = false;
        proxyVerifyRetriesLeft_ = 10;
        scheduleProxyVerify(1200);
        return true;
    }
    if (!xray_.start(configPath)) {
        setState(ConnectionState::Error, VTR("error.core_start_failed") + " " + xray_.lastError());
        return false;
    }
    QThread::msleep(350);
    tunnelStarted_ = false;
    verifyAttempt_ = ++connectAttempt_;
    proxyVerifyRetriesLeft_ = 10;
    scheduleProxyVerify(1200);
    return true;
}

void ConnectionManager::disconnect() {
    ++connectAttempt_;
    if (!activeTunName_.isEmpty())
        TunPlatform::removeRoutes(activeTunName_, bypassHost_, bypassRoute_);
    singbox_.stop();
    tun2socks_.stop();
    xray_.stop();
    hy2_.stop();
    if (!bypassHost_.isEmpty() && bypassRoute_.valid)
        TunPlatform::removeRoutes(QString(), bypassHost_, bypassRoute_);
    QThread::msleep(300);
    TunPlatform::purgeWintunPool();
    connectedId_ = 0;
    activeTunName_.clear();
    bypassHost_.clear();
    bypassRoute_ = {};
    tunnelStarted_ = false;
    useDirectSs_ = false;
    directSsProxy_.clear();
    engine_ = CoreEngine::Singbox;
    setState(ConnectionState::Disconnected, VTR("status.disconnected"));
}

} // namespace vicvpn
