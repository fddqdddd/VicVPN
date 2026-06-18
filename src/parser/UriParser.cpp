#include "vicvpn/parser/UriParser.h"
#include "vicvpn/util/StringUtil.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace vicvpn {

static ServerProfile baseProfile(Protocol proto, const QString& raw) {
    ServerProfile p;
    p.protocol = proto;
    p.core = CoreType::Xray;
    p.rawUri = raw;
    return p;
}

static QString queryParam(const QUrlQuery& q, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const QString v = q.queryItemValue(QString::fromUtf8(key), QUrl::FullyDecoded);
        if (!v.isEmpty())
            return v;
    }
    return {};
}

static QUrlQuery mergedVlessQuery(const QString& uri, const QUrl& u) {
    QUrlQuery q(u);
    const int qm = uri.indexOf('?');
    if (qm < 0)
        return q;
    int end = uri.indexOf('#', qm);
    if (end < 0)
        end = uri.size();
    const QUrlQuery manual(uri.mid(qm + 1, end - qm - 1));
    for (const auto& item : manual.queryItems(QUrl::FullyDecoded)) {
        if (q.queryItemValue(item.first, QUrl::FullyDecoded).isEmpty())
            q.addQueryItem(item.first, item.second);
    }
    return q;
}

static nlohmann::json vlessOutbound(const QString& uuid, const QString& host, int port,
                                    const QUrlQuery& q) {
    nlohmann::json o;
    o["protocol"] = "vless";
    o["tag"] = "proxy";
    o["settings"] = {{"vnext", nlohmann::json::array({{
        {"address", host.toStdString()},
        {"port", port},
        {"users", nlohmann::json::array({([&]() {
            nlohmann::json u;
            u["id"] = uuid.toStdString();
            u["encryption"] = "none";
            const auto flow = q.queryItemValue("flow", QUrl::FullyDecoded).toStdString();
            if (!flow.empty())
                u["flow"] = flow;
            return u;
        })()})}
    }})}};

    nlohmann::json stream;
    const QString sec = queryParam(q, {"security"});
    const QString pbk = queryParam(q, {"pbk", "publicKey", "password", "public_key"});
    const bool reality = !pbk.isEmpty() && (sec == "reality" || sec.isEmpty() || sec == "none");
    QString net = queryParam(q, {"type"});
    if (net.isEmpty())
        net = reality ? "raw" : "tcp";
    else if (reality && net == "tcp")
        net = "raw";
    stream["network"] = net.toStdString();

    if (reality) {
        stream["security"] = "reality";
        nlohmann::json rs;
        const QString sni = queryParam(q, {"sni", "host"});
        if (!sni.isEmpty())
            rs["serverName"] = sni.toStdString();
        const QString fp = queryParam(q, {"fp", "fingerprint"});
        rs["fingerprint"] = fp.isEmpty() ? "chrome" : fp.toStdString();
        rs["publicKey"] = pbk.toStdString();
        rs["password"] = pbk.toStdString();
        const QString sid = queryParam(q, {"sid", "shortId"});
        if (!sid.isEmpty())
            rs["shortId"] = sid.toStdString();
        const QString spx = queryParam(q, {"spx", "spiderX"});
        if (!spx.isEmpty())
            rs["spiderX"] = spx.toStdString();
        stream["realitySettings"] = rs;
    } else if (sec == "tls") {
        stream["security"] = "tls";
        nlohmann::json tls;
        const QString sni = q.queryItemValue("sni", QUrl::FullyDecoded);
        if (!sni.isEmpty())
            tls["serverName"] = sni.toStdString();
        const QString fp = q.queryItemValue("fp", QUrl::FullyDecoded);
        if (!fp.isEmpty())
            tls["fingerprint"] = fp.toStdString();
        if (q.queryItemValue("allowInsecure", QUrl::FullyDecoded) == "1")
            tls["allowInsecure"] = true;
        stream["tlsSettings"] = tls;
    } else {
        stream["security"] = "none";
    }

    if (stream["network"] == "ws") {
        nlohmann::json ws;
        ws["path"] = q.queryItemValue("path", QUrl::FullyDecoded).toStdString();
        const QString wsHost = q.queryItemValue("host", QUrl::FullyDecoded);
        if (!wsHost.isEmpty())
            ws["headers"] = {{"Host", wsHost.toStdString()}};
        stream["wsSettings"] = ws;
    }
    if (stream["network"] == "grpc") {
        stream["grpcSettings"] = {
            {"serviceName", q.queryItemValue("serviceName", QUrl::FullyDecoded).toStdString()}};
    }
    o["streamSettings"] = stream;
    return o;
}

static std::optional<ServerProfile> parseVless(const QString& uri) {
    QUrl u(uri);
    if (!u.isValid() || u.scheme() != "vless")
        return std::nullopt;
    auto p = baseProfile(Protocol::Vless, uri);
    const QString uuid = urlDecode(u.userName());
    const QString host = u.host();
    const int port = u.port(443);
    const QUrlQuery q = mergedVlessQuery(uri, u);
    p.xrayOutbound = vlessOutbound(uuid, host, port, q);
    QString tag = urlDecode(u.fragment());
    if (tag.contains('?')) tag = tag.split('?').first();
    p.name = tag.isEmpty() ? host : tag;
    p.remark = p.protocolLabel();
    return p;
}

static std::optional<ServerProfile> parseVmess(const QString& uri) {
    if (!uri.startsWith("vmess://", Qt::CaseInsensitive)) return std::nullopt;
    const QByteArray payload = base64UrlDecode(uri.mid(8));
    const auto doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) return std::nullopt;
    const QJsonObject o = doc.object();
    auto p = baseProfile(Protocol::Vmess, uri);
    nlohmann::json out;
    out["protocol"] = "vmess";
    out["tag"] = "proxy";
    nlohmann::json user;
    user["id"] = o.value("id").toString().toStdString();
    user["alterId"] = o.value("aid").toInt();
    user["security"] = o.value("scy").toString("auto").toStdString();
    out["settings"] = {{"vnext", nlohmann::json::array({{
        {"address", o.value("add").toString().toStdString()},
        {"port", o.value("port").toString().toInt()},
        {"users", nlohmann::json::array({user})}
    }})}};
    nlohmann::json stream;
    stream["network"] = o.value("net").toString("tcp").toStdString();
    if (o.value("tls").toString() == "tls") {
        stream["security"] = "tls";
        nlohmann::json tls;
        tls["enabled"] = true;
        tls["serverName"] = o.value("sni").toString(o.value("host").toString()).toStdString();
        stream["tlsSettings"] = tls;
    }
    if (o.value("net").toString() == "ws") {
        stream["wsSettings"] = {
            {"path", o.value("path").toString().toStdString()},
            {"headers", {{"Host", o.value("host").toString().toStdString()}}}
        };
    }
    out["streamSettings"] = stream;
    p.xrayOutbound = out;
    p.name = o.value("ps").toString(o.value("add").toString());
    p.remark = "VMess";
    return p;
}

static std::optional<ServerProfile> parseSs(const QString& uri) {
    QUrl u(uri);
    if (!u.isValid() || u.scheme() != "ss") return std::nullopt;
    QString method, password, host;
    int port = 8388;
    if (!u.userName().isEmpty() && !u.password().isEmpty()) {
        method = urlDecode(u.userName());
        password = urlDecode(u.password());
        host = u.host();
        port = u.port(8388);
    } else {
        const QByteArray dec = QByteArray::fromBase64(u.userName().toUtf8() + u.path().toUtf8());
        const QString plain = QString::fromUtf8(dec);
        static QRegularExpression re(R"(^([^:@]+):([^@]+)@([^:]+):(\d+)$)");
        auto m = re.match(plain);
        if (!m.hasMatch()) {
            const QByteArray dec2 = QByteArray::fromBase64(uri.mid(5).split('#').first().toUtf8());
            auto m2 = re.match(QString::fromUtf8(dec2));
            if (!m2.hasMatch()) return std::nullopt;
            method = m2.captured(1);
            password = m2.captured(2);
            host = m2.captured(3);
            port = m2.captured(4).toInt();
        } else {
            method = m.captured(1);
            password = m.captured(2);
            host = m.captured(3);
            port = m.captured(4).toInt();
        }
    }
    auto p = baseProfile(Protocol::Shadowsocks, uri);
    p.xrayOutbound = {
        {"protocol", "shadowsocks"},
        {"tag", "proxy"},
        {"settings", {
            {"servers", nlohmann::json::array({{
                {"address", host.toStdString()},
                {"port", port},
                {"method", method.toStdString()},
                {"password", password.toStdString()}
            }})}
        }}
    };
    p.name = urlDecode(u.fragment());
    if (p.name.isEmpty()) p.name = host;
    p.remark = "Shadowsocks";
    return p;
}

static std::optional<ServerProfile> parseTrojan(const QString& uri) {
    QUrl u(uri);
    if (!u.isValid() || u.scheme() != "trojan") return std::nullopt;
    auto p = baseProfile(Protocol::Trojan, uri);
    const QString pass = urlDecode(u.userName());
    const QString host = u.host();
    const int port = u.port(443);
    QUrlQuery q(u);
    nlohmann::json stream;
    stream["network"] = q.queryItemValue("type", QUrl::FullyDecoded).isEmpty()
        ? "tcp" : q.queryItemValue("type").toStdString();
    stream["security"] = "tls";
    nlohmann::json tls;
    tls["serverName"] = q.queryItemValue("sni", QUrl::FullyDecoded).isEmpty()
        ? host.toStdString() : q.queryItemValue("sni", QUrl::FullyDecoded).toStdString();
    const QString fp = q.queryItemValue("fp", QUrl::FullyDecoded);
    if (!fp.isEmpty())
        tls["fingerprint"] = fp.toStdString();
    stream["tlsSettings"] = tls;
    p.xrayOutbound = {
        {"protocol", "trojan"},
        {"tag", "proxy"},
        {"settings", {{"servers", nlohmann::json::array({{
            {"address", host.toStdString()},
            {"port", port},
            {"password", pass.toStdString()}
        }})}}},
        {"streamSettings", stream}
    };
    p.name = urlDecode(u.fragment());
    if (p.name.isEmpty()) p.name = host;
    p.remark = "Trojan";
    return p;
}

static std::optional<ServerProfile> parseSocks(const QString& uri) {
    QUrl u(uri);
    if (!u.isValid() || (u.scheme() != "socks" && u.scheme() != "socks5")) return std::nullopt;
    auto p = baseProfile(Protocol::Socks, uri);
    nlohmann::json servers = nlohmann::json::array({{
        {"address", u.host().toStdString()},
        {"port", u.port(1080)},
        {"users", nlohmann::json::array()}
    }});
    if (!u.userName().isEmpty()) {
        servers[0]["users"] = nlohmann::json::array({{
            {"user", urlDecode(u.userName()).toStdString()},
            {"pass", urlDecode(u.password()).toStdString()}
        }});
    }
    p.xrayOutbound = {
        {"protocol", "socks"},
        {"tag", "proxy"},
        {"settings", {{"servers", servers}}}
    };
    p.name = urlDecode(u.fragment());
    if (p.name.isEmpty()) p.name = u.host();
    p.remark = "Socks";
    return p;
}

std::optional<ServerProfile> UriParser::parse(const QString& input) {
    const QString s = trimUri(input);
    if (s.startsWith("vless://", Qt::CaseInsensitive)) return parseVless(s);
    if (s.startsWith("vmess://", Qt::CaseInsensitive)) return parseVmess(s);
    if (s.startsWith("ss://", Qt::CaseInsensitive)) return parseSs(s);
    if (s.startsWith("trojan://", Qt::CaseInsensitive)) return parseTrojan(s);
    if (s.startsWith("socks://", Qt::CaseInsensitive) || s.startsWith("socks5://", Qt::CaseInsensitive))
        return parseSocks(s);
    if (s.startsWith("hy2://", Qt::CaseInsensitive) || s.startsWith("hysteria2://", Qt::CaseInsensitive))
        return Hy2UriParser::parse(s);
    return std::nullopt;
}

std::vector<ServerProfile> UriParser::parseMany(const QString& blob) {
    std::vector<ServerProfile> out;
    const auto lines = blob.split(QRegularExpression(R"([\r\n]+)"), Qt::SkipEmptyParts);
    for (const auto& line : lines) {
        const QString t = trimUri(line);
        if (t.isEmpty()) continue;
        if (auto p = parse(t)) out.push_back(*p);
    }
    return out;
}

} // namespace vicvpn
