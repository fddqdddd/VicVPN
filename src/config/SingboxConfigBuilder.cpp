#include "vicvpn/config/SingboxConfigBuilder.h"
#include "vicvpn/util/TunPlatform.h"
#include "vicvpn/parser/SingboxConverter.h"
#include "vicvpn/util/NetworkRoute.h"
#include <optional>

namespace vicvpn {

static bool isIpv4Literal(const QString& host) {
    const auto parts = host.split('.');
    if (parts.size() != 4)
        return false;
    for (const auto& p : parts) {
        bool ok = false;
        const int v = p.toInt(&ok);
        if (!ok || v < 0 || v > 255)
            return false;
    }
    return true;
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

static nlohmann::json singboxTlsFromXrayStream(const nlohmann::json& ss) {
    nlohmann::json tls;
    const std::string security = ss.value("security", "none");
    if (security == "reality" && ss.contains("realitySettings")) {
        const auto& rs = ss["realitySettings"];
        tls["enabled"] = true;
        if (rs.contains("serverName"))
            tls["server_name"] = rs["serverName"];
        const std::string pk = rs.contains("publicKey") ? rs["publicKey"].get<std::string>()
                            : rs.contains("password") ? rs["password"].get<std::string>()
                                                      : std::string{};
        nlohmann::json reality;
        reality["enabled"] = true;
        reality["public_key"] = pk;
        reality["short_id"] = rs.value("shortId", "");
        tls["reality"] = reality;
        const std::string fp = rs.value("fingerprint", "chrome");
        tls["utls"] = {{"enabled", true}, {"fingerprint", fp}};
        return tls;
    }
    if (security == "tls" && ss.contains("tlsSettings")) {
        const auto& xt = ss["tlsSettings"];
        tls["enabled"] = true;
        if (xt.contains("serverName"))
            tls["server_name"] = xt["serverName"];
        if (xt.value("allowInsecure", false))
            tls["insecure"] = true;
        if (xt.contains("fingerprint"))
            tls["utls"] = {{"enabled", true}, {"fingerprint", xt["fingerprint"]}};
        return tls;
    }
    return tls;
}

static void applyXrayTransport(nlohmann::json& out, const nlohmann::json& ss) {
    const std::string net = ss.value("network", "tcp");
    if (net == "ws" && ss.contains("wsSettings")) {
        const auto& ws = ss["wsSettings"];
        nlohmann::json transport;
        transport["type"] = "ws";
        transport["path"] = ws.value("path", "");
        if (ws.contains("headers") && ws["headers"].contains("Host"))
            transport["headers"] = {{"Host", ws["headers"]["Host"]}};
        out["transport"] = transport;
    } else if (net == "grpc" && ss.contains("grpcSettings")) {
        out["transport"] = {{"type", "grpc"},
                            {"service_name", ss["grpcSettings"].value("serviceName", "")}};
    }
}

static void finalizeProxyOutbound(nlohmann::json& out, const QString& bindInterface,
                                  const QString& bindLocalIp) {
    const std::string type = out.value("type", "");
    if (type != "shadowsocks") {
        if (!bindInterface.isEmpty())
            out["bind_interface"] = bindInterface.toStdString();
        if (!bindLocalIp.isEmpty())
            out["inet4_bind_address"] = bindLocalIp.toStdString();
    }

    if (type == "shadowsocks")
        out["multiplex"] = {{"enabled", false}};

    if (type == "vless") {
        const std::string flow = out.value("flow", "");
        if (flow == "xtls-rprx-vision") {
            out["network"] = "tcp";
            out["multiplex"] = {{"enabled", false}};
        }
        if (out.contains("tls") && out["tls"].is_object()) {
            auto& tls = out["tls"];
            if (tls.contains("reality") && tls["reality"].is_object()) {
                auto& reality = tls["reality"];
                if (!reality.contains("short_id"))
                    reality["short_id"] = "";
            }
        }
    }
}

static std::optional<nlohmann::json> xrayOutboundToSingbox(const nlohmann::json& ob) {
    const std::string proto = ob.value("protocol", "");
    if (proto == "shadowsocks" && ob.contains("settings")) {
        const auto& st = ob["settings"];
        if (!st.contains("servers") || st["servers"].empty())
            return std::nullopt;
        const auto& s = st["servers"][0];
        return nlohmann::json{
            {"type", "shadowsocks"},
            {"tag", "proxy"},
            {"server", s.value("address", "")},
            {"server_port", s.value("port", 8388)},
            {"method", s.value("method", "")},
            {"password", s.value("password", "")},
            {"multiplex", {{"enabled", false}}}
        };
    }

    if (proto == "vless" && ob.contains("settings")) {
        const auto& st = ob["settings"];
        if (!st.contains("vnext") || st["vnext"].empty())
            return std::nullopt;
        const auto& vn = st["vnext"][0];
        if (!vn.contains("users") || vn["users"].empty())
            return std::nullopt;
        const auto& user = vn["users"][0];
        nlohmann::json out;
        out["type"] = "vless";
        out["tag"] = "proxy";
        out["server"] = vn.value("address", "");
        out["server_port"] = vn.value("port", 443);
        out["uuid"] = user.value("id", "");
        if (user.contains("flow"))
            out["flow"] = user["flow"];
        if (ob.contains("streamSettings")) {
            const auto tls = singboxTlsFromXrayStream(ob["streamSettings"]);
            if (!tls.empty())
                out["tls"] = tls;
            applyXrayTransport(out, ob["streamSettings"]);
        }
        return out;
    }

    if (proto == "vmess" && ob.contains("settings")) {
        const auto& st = ob["settings"];
        if (!st.contains("vnext") || st["vnext"].empty())
            return std::nullopt;
        const auto& vn = st["vnext"][0];
        const auto& user = vn["users"][0];
        nlohmann::json out;
        out["type"] = "vmess";
        out["tag"] = "proxy";
        out["server"] = vn.value("address", "");
        out["server_port"] = vn.value("port", 443);
        out["uuid"] = user.value("id", "");
        out["security"] = user.value("security", "auto");
        out["alter_id"] = user.value("alterId", 0);
        if (ob.contains("streamSettings")) {
            const auto tls = singboxTlsFromXrayStream(ob["streamSettings"]);
            if (!tls.empty())
                out["tls"] = tls;
            applyXrayTransport(out, ob["streamSettings"]);
        }
        return out;
    }

    if (proto == "trojan" && ob.contains("settings")) {
        const auto& st = ob["settings"];
        if (!st.contains("servers") || st["servers"].empty())
            return std::nullopt;
        const auto& s = st["servers"][0];
        nlohmann::json out;
        out["type"] = "trojan";
        out["tag"] = "proxy";
        out["server"] = s.value("address", "");
        out["server_port"] = s.value("port", 443);
        out["password"] = s.value("password", "");
        if (ob.contains("streamSettings")) {
            const auto tls = singboxTlsFromXrayStream(ob["streamSettings"]);
            if (!tls.empty())
                out["tls"] = tls;
            applyXrayTransport(out, ob["streamSettings"]);
        }
        return out;
    }

    return std::nullopt;
}

static std::optional<nlohmann::json> buildProxyOutbound(const ServerProfile& server) {
    if (server.passthroughJson && SingboxConverter::isSingbox(server.passthroughConfig)) {
        for (const auto& ob : server.passthroughConfig["outbounds"]) {
            const std::string type = ob.value("type", "");
            if (type == "direct" || type == "block" || type == "dns" || type == "selector" ||
                type == "urltest" || type == "tun")
                continue;
            nlohmann::json proxyOb = ob;
            if (!proxyOb.contains("tag"))
                proxyOb["tag"] = "proxy";
            return proxyOb;
        }
        return std::nullopt;
    }
    return xrayOutboundToSingbox(server.xrayOutbound);
}

QString SingboxConfigBuilder::buildSocksGateway(const ServerProfile& server,
                                                const AppSettings& settings,
                                                const QString& bindInterface,
                                                const QString& bindLocalIp, int socksPort) {
    Q_UNUSED(settings);
    auto proxyOb = buildProxyOutbound(server);
    if (!proxyOb)
        return {};

    finalizeProxyOutbound(*proxyOb, bindInterface, bindLocalIp);

    nlohmann::json cfg;
    cfg["log"] = {{"level", "warn"}, {"timestamp", true}};
    cfg["inbounds"] = nlohmann::json::array({
        nlohmann::json{
            {"type", "mixed"},
            {"tag", "socks-in"},
            {"listen", "127.0.0.1"},
            {"listen_port", socksPort}
        }
    });

    nlohmann::json direct = {{"type", "direct"}, {"tag", "direct"}};
    if (!bindInterface.isEmpty())
        direct["bind_interface"] = bindInterface.toStdString();
    cfg["outbounds"] = nlohmann::json::array({*proxyOb, direct});

    nlohmann::json route = {
        {"rules", nlohmann::json::array({nlohmann::json{{"inbound", "socks-in"}, {"outbound", "proxy"}}})},
        {"final", "direct"},
        {"auto_detect_interface", true}
    };
    if (!bindInterface.isEmpty())
        route["default_interface"] = bindInterface.toStdString();
    cfg["route"] = route;

    return QString::fromStdString(cfg.dump(2));
}

QString SingboxConfigBuilder::build(const ServerProfile& server, const AppSettings& settings,
                                    const QString& bindInterface, const QString& bindLocalIp) {
    nlohmann::json proxyOb;
    if (auto built = buildProxyOutbound(server))
        proxyOb = *built;
    else
        return {};

    finalizeProxyOutbound(proxyOb, bindInterface, bindLocalIp);

    const QString proxyHost = !server.xrayOutbound.empty()
                                  ? proxyHostFromOutbound(server.xrayOutbound)
                                  : QString::fromStdString(proxyOb.value("server", ""));

    const int mtu = settings.mtu > 1400 ? 1400 : (settings.mtu >= 1280 ? settings.mtu : 1400);

    nlohmann::json tunInbound = {
        {"type", "tun"},
        {"tag", "tun-in"},
        {"interface_name", TunPlatform::singboxTunName().toStdString()},
        {"address", nlohmann::json::array({"172.19.0.1/30"})},
        {"mtu", mtu},
        {"auto_route", true},
        {"strict_route", false},
        {"stack", "mixed"},
        {"endpoint_independent_nat", true},
        {"udp_timeout", "5m"}
    };
    if (!proxyHost.isEmpty() && isIpv4Literal(proxyHost))
        tunInbound["route_exclude_address"] =
            nlohmann::json::array({proxyHost.toStdString() + "/32"});

    nlohmann::json cfg;
    cfg["log"] = {{"level", "warn"}, {"timestamp", true}};

    nlohmann::json dnsRules = nlohmann::json::array();
    if (!proxyHost.isEmpty()) {
        if (isIpv4Literal(proxyHost)) {
            dnsRules.push_back({{"ip_cidr", nlohmann::json::array({proxyHost.toStdString() + "/32"})},
                                {"server", "dns-local"}});
        } else {
            dnsRules.push_back({{"domain", nlohmann::json::array({proxyHost.toStdString()})},
                                {"server", "dns-local"}});
        }
    }
    dnsRules.push_back({{"query_type", nlohmann::json::array({"A", "AAAA"})},
                        {"server", "dns-fakeip"}});

    cfg["dns"] = {
        {"servers", nlohmann::json::array({
            nlohmann::json{{"type", "udp"}, {"tag", "dns-remote"}, {"server", "1.1.1.1"},
                           {"detour", "proxy"}},
            nlohmann::json{{"type", "udp"}, {"tag", "dns-remote2"}, {"server", "8.8.8.8"},
                           {"detour", "proxy"}},
            nlohmann::json{{"type", "local"}, {"tag", "dns-local"}},
            nlohmann::json{{"type", "fakeip"}, {"tag", "dns-fakeip"},
                           {"inet4_range", "198.18.0.0/15"}}
        })},
        {"rules", dnsRules},
        {"final", "dns-remote"},
        {"strategy", "ipv4_only"},
        {"reverse_mapping", true}
    };

    cfg["inbounds"] = nlohmann::json::array({tunInbound});

    nlohmann::json outbounds = nlohmann::json::array({proxyOb});
    nlohmann::json direct = {{"type", "direct"}, {"tag", "direct"}};
    if (!bindInterface.isEmpty())
        direct["bind_interface"] = bindInterface.toStdString();
    outbounds.push_back(direct);
    cfg["outbounds"] = outbounds;

    nlohmann::json rules = nlohmann::json::array();
    rules.push_back({{"inbound", "tun-in"}, {"action", "sniff"}});
    rules.push_back({{"protocol", "dns"}, {"action", "hijack-dns"}});
    if (settings.bypassLan)
        rules.push_back({{"ip_is_private", true}, {"outbound", "direct"}});

    nlohmann::json route = {{"rules", rules},
                            {"final", "proxy"},
                            {"auto_detect_interface", true},
                            {"default_domain_resolver", "dns-local"}};
    if (!bindInterface.isEmpty())
        route["default_interface"] = bindInterface.toStdString();
    cfg["route"] = route;

    return QString::fromStdString(cfg.dump(2));
}

} // namespace vicvpn
