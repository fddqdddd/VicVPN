#include "vicvpn/config/ConfigBuilders.h"
#include "vicvpn/util/AppPaths.h"
#include "vicvpn/util/NetworkRoute.h"
#include "vicvpn/util/TunPlatform.h"
#include <QFileInfo>
#include <QUrl>

namespace vicvpn {

static bool hasTunInbound(const nlohmann::json& cfg) {
    if (!cfg.contains("inbounds") || !cfg["inbounds"].is_array())
        return false;
    for (const auto& ib : cfg["inbounds"]) {
        if (ib.value("protocol", "") == "tun")
            return true;
    }
    return false;
}

static nlohmann::json extractXrayProxyOutbound(const nlohmann::json& cfg) {
    if (!cfg.contains("outbounds") || !cfg["outbounds"].is_array())
        return {};
    for (const auto& ob : cfg["outbounds"]) {
        const std::string proto = ob.value("protocol", "");
        if (proto.empty() || proto == "freedom" || proto == "blackhole" || proto == "dns")
            continue;
        return ob;
    }
    return {};
}

static QString extractProxyHost(const nlohmann::json& outbound) {
    if (!outbound.contains("settings"))
        return {};
    const auto& st = outbound["settings"];
    if (st.contains("vnext") && st["vnext"].is_array() && !st["vnext"].empty())
        return QString::fromStdString(st["vnext"][0].value("address", ""));
    if (st.contains("servers") && st["servers"].is_array() && !st["servers"].empty())
        return QString::fromStdString(st["servers"][0].value("address", ""));
    return {};
}

static int jsonPortFromOutbound(const nlohmann::json& outbound) {
    if (!outbound.contains("settings"))
        return 443;
    const auto& st = outbound["settings"];
    if (st.contains("vnext") && st["vnext"].is_array() && !st["vnext"].empty())
        return st["vnext"][0].value("port", 443);
    if (st.contains("servers") && st["servers"].is_array() && !st["servers"].empty())
        return st["servers"][0].value("port", 443);
    return 443;
}

QString XrayConfigBuilder::buildPassthrough(const nlohmann::json& config) {
    return QString::fromStdString(config.dump(2));
}

static void applySendThrough(nlohmann::json& outbound, const QString& localIp) {
    if (!localIp.isEmpty())
        outbound["sendThrough"] = localIp.toStdString();
}

static void migrateLegacyReality(nlohmann::json& ss) {
    if (!ss.contains("tlsSettings") || !ss["tlsSettings"].is_object())
        return;
    auto& tls = ss["tlsSettings"];
    if (!tls.contains("realitySettings") || !tls["realitySettings"].is_object())
        return;
    if (!ss.contains("realitySettings"))
        ss["realitySettings"] = tls["realitySettings"];
    ss["security"] = "reality";
    ss.erase("tlsSettings");
}

static void normalizeProxyOutbound(nlohmann::json& proxy) {
    if (!proxy.contains("streamSettings") || !proxy["streamSettings"].is_object())
        return;
    auto& ss = proxy["streamSettings"];
    migrateLegacyReality(ss);
    if (ss.value("security", "") != "reality")
        return;

    if (!ss.contains("realitySettings") || !ss["realitySettings"].is_object())
        ss["realitySettings"] = nlohmann::json::object();
    auto& rs = ss["realitySettings"];

    if (rs.contains("publicKey") && rs["publicKey"].is_string()) {
        const std::string pk = rs["publicKey"].get<std::string>();
        if (!pk.empty() && (!rs.contains("password") || rs["password"].get<std::string>().empty()))
            rs["password"] = pk;
    }
    if (rs.contains("password") && rs["password"].is_string()) {
        const std::string pw = rs["password"].get<std::string>();
        if (!pw.empty() && (!rs.contains("publicKey") || rs["publicKey"].get<std::string>().empty()))
            rs["publicKey"] = pw;
    }
    if (!rs.contains("fingerprint"))
        rs["fingerprint"] = "chrome";

    const auto key = rs.contains("password") && rs["password"].is_string()
                         ? rs["password"].get<std::string>()
                         : std::string{};
    const auto pub = rs.contains("publicKey") && rs["publicKey"].is_string()
                         ? rs["publicKey"].get<std::string>()
                         : std::string{};
    if (key.empty() && pub.empty()) {
        ss["security"] = "none";
        ss.erase("realitySettings");
    }
}

static void applyFastOutboundTuning(nlohmann::json& proxy, const AppSettings& settings) {
    if (!settings.fastTunnel)
        return;
    if (!proxy.contains("streamSettings"))
        proxy["streamSettings"] = nlohmann::json::object();
    proxy["streamSettings"]["sockopt"]["tcpNoDelay"] = true;
}

static nlohmann::json lanBypassRule() {
    return {{"type", "field"},
            {"ip", nlohmann::json::array({"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "127.0.0.0/8"})},
            {"outboundTag", "direct"}};
}

QString XrayConfigBuilder::build(const ServerProfile& server, const AppSettings& settings,
                                   const QString& tunName, const BypassRoute& bypassRoute) {
    const QString tun = tunName.isEmpty() ? TunPlatform::sessionTunName() : tunName;
    if (server.passthroughJson) {
        const auto& cfg = server.passthroughConfig;
        if (auto ob = extractXrayProxyOutbound(cfg); !ob.is_null()) {
            ServerProfile tunReady = server;
            tunReady.passthroughJson = false;
            tunReady.xrayOutbound = ob;
            return build(tunReady, settings, tun, bypassRoute);
        }
        return buildPassthrough(cfg);
    }

    nlohmann::json cfg;
    const std::string errorLog = (AppPaths::runtimeDir() + "/xray-error.log").toStdString();
    cfg["log"] = nlohmann::json{
        {"loglevel", "error"},
        {"access", ""},
        {"error", errorLog}
    };

    if (settings.remoteDns && !settings.fastTunnel) {
        cfg["dns"] = {
            {"servers", nlohmann::json::array({
                "1.1.1.1",
                "8.8.8.8"
            })},
            {"queryStrategy", "UseIPv4"}
        };
    }

    if (settings.fastTunnel) {
        cfg["policy"] = {
            {"levels", nlohmann::json::object({
                {"0", nlohmann::json::object({
                    {"handshake", 4},
                    {"connIdle", 300},
                    {"bufferSize", 2048}
                })}
            })},
            {"system", nlohmann::json::object({
                {"statsOutboundUplink", false},
                {"statsOutboundDownlink", false}
            })}
        };
    }

    nlohmann::json proxy = server.xrayOutbound;
    if (!proxy.contains("tag")) proxy["tag"] = "proxy";
    normalizeProxyOutbound(proxy);
    applyFastOutboundTuning(proxy, settings);

    const QString proxyHost = extractProxyHost(proxy);
    const int proxyPort = jsonPortFromOutbound(proxy);
    QString bindIp;
    if (bypassRoute.valid && bypassRoute.ifIndex > 0) {
        if (!proxyHost.isEmpty())
            bindIp = NetworkRoute::localIpv4ForHost(proxyHost, proxyPort);
    } else if (!proxyHost.isEmpty()) {
        bindIp = NetworkRoute::localIpv4ForHost(proxyHost, proxyPort);
    }
    applySendThrough(proxy, bindIp);

    nlohmann::json tunInbound;
    tunInbound["tag"] = "tun-in";
    tunInbound["protocol"] = "tun";
    tunInbound["port"] = 0;
    tunInbound["settings"] = {
        {"name", tun.toStdString()},
        {"mtu", settings.mtu},
        {"gateway", nlohmann::json::array({"10.10.0.1/24"})},
        {"autoOutboundsInterface", "auto"}
    };
    tunInbound["sniffing"] = {
        {"enabled", true},
        {"destOverride", nlohmann::json::array({"http", "tls", "quic"})},
        {"metadataOnly", true}
    };
    cfg["inbounds"] = nlohmann::json::array({tunInbound});

    nlohmann::json direct = {{"protocol", "freedom"}, {"tag", "direct"}};
    nlohmann::json block = {{"protocol", "blackhole"}, {"tag", "block"}};
    cfg["outbounds"] = nlohmann::json::array({proxy, direct, block});

    nlohmann::json rules = nlohmann::json::array();
    // Proxy host bypass is handled by OS route (/32 via physical IF), not Xray routing.
    // A "direct" rule here breaks Shadowsocks dial to the server.
    if (settings.bypassLan) {
        rules.push_back(settings.fastTunnel ? lanBypassRule()
                                            : nlohmann::json{{"type", "field"},
                                                             {"ip", nlohmann::json::array({"geoip:private"})},
                                                             {"outboundTag", "direct"}});
    }
    rules.push_back({{"type", "field"},
                     {"inboundTag", nlohmann::json::array({"tun-in"})},
                     {"outboundTag", "proxy"}});
    if (settings.blockIpv6) {
        rules.push_back({{"type", "field"},
                         {"ip", nlohmann::json::array({"::/0"})},
                         {"outboundTag", "block"}});
    }
    cfg["routing"] = {
        {"domainStrategy", "AsIs"},
        {"rules", rules}
    };

    return QString::fromStdString(cfg.dump(2));
}

bool XrayConfigBuilder::supportsDirectShadowsocksTunnel(const ServerProfile& server,
                                                        const AppSettings& settings) {
    if (!settings.fastTunnel || server.core == CoreType::Hysteria2)
        return false;
    ServerProfile probe = server;
    if (server.passthroughJson) {
        const auto ob = extractXrayProxyOutbound(server.passthroughConfig);
        if (ob.is_null() || ob.value("protocol", "") != "shadowsocks")
            return false;
        probe.passthroughJson = false;
        probe.xrayOutbound = ob;
    } else if (server.protocol != Protocol::Shadowsocks &&
               server.xrayOutbound.value("protocol", "") != "shadowsocks") {
        return false;
    }
    return !buildShadowsocksProxyUrl(probe).isEmpty();
}

QString XrayConfigBuilder::buildShadowsocksProxyUrl(const ServerProfile& server) {
    nlohmann::json ob = server.xrayOutbound;
    if (server.passthroughJson) {
        ob = extractXrayProxyOutbound(server.passthroughConfig);
        if (ob.is_null())
            return {};
    }
    if (ob.value("protocol", "") != "shadowsocks" || !ob.contains("settings"))
        return {};

    const auto& st = ob["settings"];
    if (!st.contains("servers") || !st["servers"].is_array() || st["servers"].empty())
        return {};

    const auto& s = st["servers"][0];
    const QString method = QString::fromStdString(s.value("method", ""));
    const QString password = QString::fromStdString(s.value("password", ""));
    const QString host = QString::fromStdString(s.value("address", ""));
    const int port = s.value("port", 8388);
    if (method.isEmpty() || password.isEmpty() || host.isEmpty() || port <= 0)
        return {};

    // SIP002: ss://BASE64(method:password)@host:port — tun2socks rejects percent-encoded userinfo.
    QByteArray userinfo = (method + ":" + password).toUtf8();
    QString b64 = QString::fromUtf8(userinfo.toBase64(QByteArray::Base64Encoding));
    while (b64.endsWith('='))
        b64.chop(1);
    QString url = QString("ss://%1@%2:%3").arg(b64, host).arg(port);
    if (s.contains("prefix")) {
        const QString prefix = QString::fromStdString(s["prefix"].get<std::string>());
        if (!prefix.isEmpty())
            url += "/?outline=1&prefix=" + QString::fromUtf8(QUrl::toPercentEncoding(prefix));
    }
    return url;
}

QString XrayConfigBuilder::buildSocksGateway(const ServerProfile& server,
                                             const AppSettings& settings,
                                             const BypassRoute& bypassRoute, int socksPort) {
    if (server.passthroughJson) {
        const auto& cfg = server.passthroughConfig;
        if (auto ob = extractXrayProxyOutbound(cfg); !ob.is_null()) {
            ServerProfile tunReady = server;
            tunReady.passthroughJson = false;
            tunReady.xrayOutbound = ob;
            return buildSocksGateway(tunReady, settings, bypassRoute, socksPort);
        }
    }

    nlohmann::json cfg;
    const std::string errorLog = (AppPaths::runtimeDir() + "/xray-error.log").toStdString();
    cfg["log"] = nlohmann::json{
        {"loglevel", "error"},
        {"access", ""},
        {"error", errorLog}
    };

    cfg["policy"] = {
        {"levels", nlohmann::json::object({
            {"0", nlohmann::json::object({
                {"handshake", 4},
                {"connIdle", 300},
                {"uplinkOnly", 2},
                {"downlinkOnly", 5},
                {"bufferSize", 8192}
            })}
        })},
        {"system", nlohmann::json::object({
            {"statsOutboundUplink", false},
            {"statsOutboundDownlink", false}
        })}
    };

    nlohmann::json proxy = server.xrayOutbound;
    if (!proxy.contains("tag")) proxy["tag"] = "proxy";
    normalizeProxyOutbound(proxy);
    applyFastOutboundTuning(proxy, settings);

    const QString proxyHost = extractProxyHost(proxy);
    const int proxyPort = jsonPortFromOutbound(proxy);
    QString bindIp;
    if (bypassRoute.valid && bypassRoute.ifIndex > 0) {
        if (!proxyHost.isEmpty())
            bindIp = NetworkRoute::localIpv4ForHost(proxyHost, proxyPort);
    } else if (!proxyHost.isEmpty()) {
        bindIp = NetworkRoute::localIpv4ForHost(proxyHost, proxyPort);
    }
    applySendThrough(proxy, bindIp);

    nlohmann::json socksInbound = {
        {"tag", "socks-in"},
        {"listen", "127.0.0.1"},
        {"port", socksPort},
        {"protocol", "socks"},
        {"settings", {{"auth", "noauth"}, {"udp", true}}},
        {"sniffing", {{"enabled", false}}}
    };
    cfg["inbounds"] = nlohmann::json::array({socksInbound});

    nlohmann::json direct = {{"protocol", "freedom"}, {"tag", "direct"}};
    cfg["outbounds"] = nlohmann::json::array({proxy, direct});

    cfg["routing"] = {
        {"domainStrategy", "AsIs"},
        {"rules", nlohmann::json::array({
            {{"type", "field"},
             {"inboundTag", nlohmann::json::array({"socks-in"})},
             {"outboundTag", "proxy"}}
        })}
    };

    return QString::fromStdString(cfg.dump(2));
}

QString Hy2ConfigBuilder::buildYaml(const ServerProfile& server, const AppSettings& settings) {
    const auto& c = server.hy2Config;
    QString yaml;
    auto line = [&](const QString& k, const QString& v) {
        if (!v.isEmpty()) yaml += k + ": " + v + "\n";
    };

    if (c.value("realm_mode", false)) {
        line("server", QString::fromStdString(c.value("rendezvous", "")));
        line("auth", QString::fromStdString(c.value("auth", "")));
    } else {
        line("server", QString::fromStdString(c.value("server", "")));
        line("auth", QString::fromStdString(c.value("auth", "")));
    }

    if (c.contains("sni"))
        line("sni", QString::fromStdString(c["sni"].get<std::string>()));
    if (c.value("insecure", false))
        yaml += "tls:\n  insecure: true\n";
    if (c.contains("obfs")) {
        yaml += "obfs:\n  type: " + QString::fromStdString(c["obfs"].get<std::string>()) + "\n";
        if (c.contains("obfs-password"))
            yaml += "  salamander:\n    password: " +
                    QString::fromStdString(c["obfs-password"].get<std::string>()) + "\n";
    }

    yaml += "tun:\n";
    yaml += "  name: vicvpn-tun\n";
    yaml += QString("  mtu: %1\n").arg(settings.mtu);
    yaml += "  address:\n    ipv4: 100.100.100.101/30\n";
    if (!settings.blockIpv6)
        yaml += "    ipv6: 2001::ffff:ffff:ffff:fff1/126\n";
    yaml += "  route:\n";
    yaml += "    ipv4: [0.0.0.0/0]\n";
    if (!settings.blockIpv6)
        yaml += "    ipv6: [\"::/0\"]\n";

    const QString serverAddr = QString::fromStdString(
        c.contains("server") ? c["server"].get<std::string>() : c.value("rendezvous", ""));
    const QString host = serverAddr.split(':').first();
    if (!host.isEmpty())
        yaml += QString("    ipv4Exclude: [%1/32]\n").arg(host);

    yaml += "dns:\n  type: udp\n  udp:\n    addr: 1.1.1.1:53\n";
    return yaml;
}

} // namespace vicvpn
