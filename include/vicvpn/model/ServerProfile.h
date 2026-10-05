#pragma once

#include <QString>
#include <QDateTime>
#include <nlohmann/json.hpp>

namespace vicvpn {

enum class CoreType { Xray, Hysteria2 };
enum class Protocol {
    Vless, Vmess, Shadowsocks, Trojan, Socks,
    Hysteria2, Ssh, Unknown
};

struct SshConfig {
    QString host;
    int port = 22;
    QString user;
    QString password;
    QString privateKeyPath;
    QString passphrase;
};

struct ServerProfile {
    qint64 id = 0;
    QString name;
    QString remark;
    Protocol protocol = Protocol::Unknown;
    CoreType core = CoreType::Xray;
    QString rawUri;
    nlohmann::json xrayOutbound = nlohmann::json::object();
    nlohmann::json hy2Config = nlohmann::json::object();
    SshConfig ssh;
    bool passthroughJson = false;
    nlohmann::json passthroughConfig = nlohmann::json::object();
    QString subscriptionUrl;
    QString countryCode;
    int latencyMs = -1;
    QDateTime updatedAt;

    QString protocolLabel() const;
    QString coreLabel() const;
};

} // namespace vicvpn
