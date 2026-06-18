#include "vicvpn/parser/SingboxConverter.h"
#include <set>

namespace vicvpn {

static const std::set<std::string> kSkipOutboundTypes = {
    "direct", "block", "dns", "selector", "urltest", "tun", "icmp", "forward", "loopback"
};

bool SingboxConverter::isSingbox(const nlohmann::json& j) {
    if (!j.is_object()) return false;
    if (j.contains("$schema")) {
        const auto schema = j["$schema"].get<std::string>();
        if (schema.find("sing-box") != std::string::npos)
            return true;
    }
    if (!j.contains("outbounds") || !j["outbounds"].is_array())
        return false;
    for (const auto& ob : j["outbounds"]) {
        if (ob.contains("type") && !ob.contains("protocol"))
            return true;
    }
    return false;
}

static nlohmann::json singboxTlsToXrayStream(const nlohmann::json& tls) {
    nlohmann::json stream;
    if (!tls.value("enabled", false))
        return stream;

    stream["security"] = "tls";
    nlohmann::json tlsSettings;
    tlsSettings["enabled"] = true;
    if (tls.contains("server_name"))
        tlsSettings["serverName"] = tls["server_name"];
    if (tls.contains("utls") && tls["utls"].value("enabled", false)) {
        tlsSettings["fingerprint"] = tls["utls"].value("fingerprint", "chrome");
    }
    if (tls.contains("reality") && tls["reality"].value("enabled", false)) {
        stream["security"] = "reality";
        nlohmann::json reality;
        reality["enabled"] = true;
        reality["publicKey"] = tls["reality"].value("public_key", "");
        reality["shortId"] = tls["reality"].value("short_id", "");
        tlsSettings["realitySettings"] = reality;
    }
    stream["tlsSettings"] = tlsSettings;
    return stream;
}

static void applySingboxTransport(nlohmann::json& stream, const nlohmann::json& transport) {
    const std::string type = transport.value("type", "tcp");
    stream["network"] = type;
    if (type == "ws") {
        nlohmann::json ws;
        ws["path"] = transport.value("path", "");
        if (transport.contains("headers") && transport["headers"].is_object()) {
            nlohmann::json headers;
            for (auto it = transport["headers"].begin(); it != transport["headers"].end(); ++it)
                headers[it.key()] = it.value();
            ws["headers"] = headers;
        }
        stream["wsSettings"] = ws;
    } else if (type == "grpc") {
        stream["grpcSettings"] = {{"serviceName", transport.value("service_name", "")}};
    } else if (type == "http" || type == "h2") {
        stream["network"] = "h2";
        nlohmann::json h2;
        if (transport.contains("host") && transport["host"].is_array() && !transport["host"].empty())
            h2["host"] = transport["host"];
        h2["path"] = transport.value("path", "");
        stream["httpSettings"] = h2;
    }
}

static int jsonPort(const nlohmann::json& j, const char* key, int fallback) {
    if (!j.contains(key)) return fallback;
    const auto& v = j.at(key);
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number_unsigned()) return static_cast<int>(v.get<unsigned>());
    if (v.is_string()) {
        try { return std::stoi(v.get<std::string>()); } catch (...) {}
    }
    return fallback;
}

std::optional<nlohmann::json> SingboxConverter::toXrayOutbound(const nlohmann::json& ob) {
    const std::string type = ob.value("type", "");
    if (type.empty() || kSkipOutboundTypes.count(type))
        return std::nullopt;

    if (type == "shadowsocks") {
        return nlohmann::json{
            {"protocol", "shadowsocks"},
            {"tag", ob.value("tag", "proxy")},
            {"settings", {{"servers", nlohmann::json::array({{
                {"address", ob.value("server", "")},
                {"port", jsonPort(ob, "server_port", 8388)},
                {"method", ob.value("method", "")},
                {"password", ob.value("password", "")}
            }})}}}
        };
    }

    if (type == "vless") {
        nlohmann::json out;
        out["protocol"] = "vless";
        out["tag"] = ob.value("tag", "proxy");
        nlohmann::json user;
        user["id"] = ob.value("uuid", "");
        user["encryption"] = "none";
        if (ob.contains("flow"))
            user["flow"] = ob["flow"];
        out["settings"] = {{"vnext", nlohmann::json::array({{
            {"address", ob.value("server", "")},
            {"port", jsonPort(ob, "server_port", 443)},
            {"users", nlohmann::json::array({user})}
        }})}};
        nlohmann::json stream;
        stream["network"] = "tcp";
        if (ob.contains("tls"))
            stream = singboxTlsToXrayStream(ob["tls"]);
        if (ob.contains("transport"))
            applySingboxTransport(stream, ob["transport"]);
        out["streamSettings"] = stream;
        return out;
    }

    if (type == "vmess") {
        nlohmann::json out;
        out["protocol"] = "vmess";
        out["tag"] = ob.value("tag", "proxy");
        nlohmann::json user;
        user["id"] = ob.value("uuid", "");
        user["alterId"] = ob.value("alter_id", 0);
        user["security"] = ob.value("security", "auto");
        out["settings"] = {{"vnext", nlohmann::json::array({{
            {"address", ob.value("server", "")},
            {"port", jsonPort(ob, "server_port", 443)},
            {"users", nlohmann::json::array({user})}
        }})}};
        nlohmann::json stream;
        stream["network"] = "tcp";
        if (ob.contains("tls"))
            stream = singboxTlsToXrayStream(ob["tls"]);
        if (ob.contains("transport"))
            applySingboxTransport(stream, ob["transport"]);
        out["streamSettings"] = stream;
        return out;
    }

    if (type == "trojan") {
        nlohmann::json out;
        out["protocol"] = "trojan";
        out["tag"] = ob.value("tag", "proxy");
        out["settings"] = {{"servers", nlohmann::json::array({{
            {"address", ob.value("server", "")},
            {"port", jsonPort(ob, "server_port", 443)},
            {"password", ob.value("password", "")}
        }})}};
        nlohmann::json stream;
        stream["network"] = "tcp";
        if (ob.contains("tls"))
            stream = singboxTlsToXrayStream(ob["tls"]);
        if (ob.contains("transport"))
            applySingboxTransport(stream, ob["transport"]);
        out["streamSettings"] = stream;
        return out;
    }

    if (type == "socks") {
        nlohmann::json servers = nlohmann::json::array({{
            {"address", ob.value("server", "")},
            {"port", jsonPort(ob, "server_port", 1080)},
            {"users", nlohmann::json::array()}
        }});
        if (ob.contains("username") || ob.contains("password")) {
            servers[0]["users"] = nlohmann::json::array({{
                {"user", ob.value("username", "")},
                {"pass", ob.value("password", "")}
            }});
        }
        return nlohmann::json{
            {"protocol", "socks"},
            {"tag", ob.value("tag", "proxy")},
            {"settings", {{"servers", servers}}}
        };
    }

    return std::nullopt;
}

static ServerProfile profileFromXrayOutbound(const nlohmann::json& ob, const QString& raw) {
    ServerProfile p;
    p.rawUri = raw;
    p.xrayOutbound = ob;
    p.core = CoreType::Xray;
    const std::string proto = ob.value("protocol", "");
    if (proto == "vless") p.protocol = Protocol::Vless;
    else if (proto == "vmess") p.protocol = Protocol::Vmess;
    else if (proto == "shadowsocks") p.protocol = Protocol::Shadowsocks;
    else if (proto == "trojan") p.protocol = Protocol::Trojan;
    else if (proto == "socks") p.protocol = Protocol::Socks;
    p.name = QString::fromStdString(ob.value("tag", "imported"));
    p.remark = p.protocolLabel();
    return p;
}

static std::optional<ServerProfile> fromSingboxHysteria2(const nlohmann::json& ob, const QString& raw) {
    ServerProfile p;
    p.protocol = Protocol::Hysteria2;
    p.core = CoreType::Hysteria2;
    p.rawUri = raw;
    p.hy2Config["server"] = ob.value("server", std::string("")) + ":" +
                            std::to_string(ob.value("server_port", 443));
    if (ob.contains("password"))
        p.hy2Config["auth"] = ob["password"];
    if (ob.contains("tls") && ob["tls"].is_object()) {
        if (ob["tls"].contains("server_name"))
            p.hy2Config["sni"] = ob["tls"]["server_name"];
        if (ob["tls"].value("insecure", false))
            p.hy2Config["insecure"] = true;
    }
    if (ob.contains("obfs") && ob["obfs"].is_object()) {
        p.hy2Config["obfs"] = ob["obfs"].value("type", "");
        if (ob["obfs"].contains("password"))
            p.hy2Config["obfs-password"] = ob["obfs"]["password"];
    }
    p.name = QString::fromStdString(ob.value("tag", "Hysteria2"));
    p.remark = "HY2";
    return p;
}

std::optional<ServerProfile> SingboxConverter::fromShadowsocksLegacy(const nlohmann::json& j,
                                                                    const QString& raw) {
    if (!j.contains("server") || !j.contains("server_port") || !j.contains("method") ||
        !j.contains("password"))
        return std::nullopt;

    ServerProfile p;
    p.protocol = Protocol::Shadowsocks;
    p.core = CoreType::Xray;
    p.rawUri = raw;
    p.xrayOutbound = {
        {"protocol", "shadowsocks"},
        {"tag", "proxy"},
        {"settings", {{"servers", nlohmann::json::array({{
            {"address", j["server"]},
            {"port", jsonPort(j, "server_port", 8388)},
            {"method", j["method"]},
            {"password", j["password"]}
        }})}}}
    };
    if (j.contains("prefix"))
        p.xrayOutbound["settings"]["servers"][0]["prefix"] = j["prefix"];
    if (j.contains("tag"))
        p.name = QString::fromStdString(j["tag"].get<std::string>());
    else
        p.name = QString::fromStdString(j.value("remarks", j.value("server", "Shadowsocks")));
    p.remark = "Shadowsocks";
    return p;
}

std::vector<ServerProfile> SingboxConverter::importProfiles(const nlohmann::json& j,
                                                          const QString& raw,
                                                          QString* error) {
    std::vector<ServerProfile> out;
    if (!j.contains("outbounds") || !j["outbounds"].is_array()) {
        if (error) *error = "sing-box config has no outbounds";
        return out;
    }

    for (const auto& ob : j["outbounds"]) {
        const std::string type = ob.value("type", "");
        if (type == "hysteria2") {
            if (auto p = fromSingboxHysteria2(ob, raw))
                out.push_back(*p);
            continue;
        }
        if (auto xray = toXrayOutbound(ob))
            out.push_back(profileFromXrayOutbound(*xray, raw));
    }

    if (out.empty() && error)
        *error = "No supported proxy outbounds in sing-box config";
    return out;
}

} // namespace vicvpn
