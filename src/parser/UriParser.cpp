#include "vicvpn/parser/UriParser.h"
#include "vicvpn/parser/ImportService.h"
#include "vicvpn/util/StringUtil.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace vicvpn {

namespace {

QString schemeOf(const QUrl& u) {
    return u.scheme().toLower();
}

int jsonToInt(const QJsonValue& v) {
    if (v.isDouble())
        return v.toInt();
    if (v.isString())
        return v.toString().toInt();
    return 0;
}

// Tolerant base64 for config payloads: strips whitespace, accepts url-safe alphabet.
QByteArray decodeConfigBase64(const QString& raw, const QString& what) {
    QString s = raw.trimmed();
    const int hash = s.indexOf(QLatin1Char('#'));
    if (hash >= 0)
        s = s.left(hash);
    s.remove(QRegularExpression(QStringLiteral("\\s")));
    if (s.isEmpty()) {
        ParseLog::detail(QStringLiteral("%1: empty payload").arg(what));
        return {};
    }

    QByteArray dec = QByteArray::fromBase64(s.toUtf8(), QByteArray::Base64Encoding);
    const bool looksTextful =
        dec.contains("://") || dec.contains('{') || dec.contains('"') || dec.contains(':');
    if (!looksTextful) {
        const QByteArray alt = base64UrlDecode(s);
        if (alt.size() > dec.size())
            dec = alt;
    }

    ParseLog::detail(QStringLiteral("%1: raw_len=%2 decoded_len=%3 starts_json=%4")
                         .arg(what)
                         .arg(s.size())
                         .arg(dec.size())
                         .arg(dec.trimmed().startsWith('{') ? "yes" : "no"));
    if (dec.trimmed().isEmpty())
        ParseLog::detail(QStringLiteral("%1: decoded payload is empty").arg(what));
    return dec;
}

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
    if (!u.isValid() || schemeOf(u) != "vless") return std::nullopt;
    const QString uuid = urlDecode(u.userName());
    const QString host = u.host();
    if (uuid.isEmpty() || host.isEmpty()) {
        ParseLog::detail(QStringLiteral("vless: missing uuid or host (user='%1' host='%2')")
                             .arg(u.userName()).arg(host));
        return std::nullopt;
    }
    auto p = baseProfile(Protocol::Vless, uri);
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
    if (!uri.startsWith("vmess://", Qt::CaseInsensitive))
        return std::nullopt;

    const QByteArray payload = decodeConfigBase64(uri.mid(8), QStringLiteral("vmess payload"));
    if (payload.isEmpty()) {
        ParseLog::detail(QStringLiteral("vmess: payload decode failed"));
        return std::nullopt;
    }

    const auto doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        ParseLog::detail(QStringLiteral("vmess: not a JSON object after base64 (first 120 bytes: %1)")
                             .arg(QString::fromUtf8(payload.left(120)).replace('\n', ' ')));
        return std::nullopt;
    }

    const QJsonObject o = doc.object();
    const QString address = o.value("add").toString();
    if (address.isEmpty()) {
        ParseLog::detail(QStringLiteral("vmess: JSON has no 'add' field, keys=%1")
                             .arg(QStringList(o.keys()).join(',')));
        return std::nullopt;
    }
    const int port = jsonToInt(o.value("port"));
    if (port <= 0 || port > 65535) {
        ParseLog::detail(QStringLiteral("vmess: bad 'port' value '%1'")
                             .arg(o.value("port").toVariant().toString()));
        return std::nullopt;
    }
    if (o.value("id").toString().isEmpty()) {
        ParseLog::detail(QStringLiteral("vmess: JSON has no 'id' field"));
        return std::nullopt;
    }

    auto p = baseProfile(Protocol::Vmess, uri);
    nlohmann::json out;
    out["protocol"] = "vmess";
    out["tag"] = "proxy";
    nlohmann::json user;
    user["id"] = o.value("id").toString().toStdString();
    user["alterId"] = jsonToInt(o.value("aid"));
    user["security"] = o.value("scy").toString("auto").toStdString();
    nlohmann::json vnext;
    vnext["address"] = address.toStdString();
    vnext["port"] = port;
    vnext["users"] = nlohmann::json::array({user});
    out["settings"] = {{"vnext", nlohmann::json::array({vnext})}};

    nlohmann::json stream;
    const QString net = o.value("net").toString(QStringLiteral("tcp"));
    stream["network"] = net.toStdString();

    const QJsonValue tlsValue = o.value("tls");
    const bool tlsOn = tlsValue.isBool() ? tlsValue.toBool()
                                         : (tlsValue.toString().compare("tls", Qt::CaseInsensitive) == 0 ||
                                            tlsValue.toString() == "true");
    const QString hostHeader = o.value("host").toString();
    if (tlsOn) {
        stream["security"] = "tls";
        nlohmann::json tls;
        tls["enabled"] = true;
        tls["serverName"] = o.value("sni").toString(hostHeader).toStdString();
        if (!hostHeader.isEmpty())
            tls["alpn"] = nlohmann::json::array({hostHeader.toStdString()});
        stream["tlsSettings"] = tls;
    }

    if (net == "ws") {
        nlohmann::json ws;
        ws["path"] = o.value("path").toString(QStringLiteral("/")).toStdString();
        if (!hostHeader.isEmpty())
            ws["headers"] = {{"Host", hostHeader.toStdString()}};
        stream["wsSettings"] = ws;
    } else if (net == "grpc") {
        stream["grpcSettings"] = {
            {"serviceName", o.value("path").toString().toStdString()}};
    }

    out["streamSettings"] = stream;
    p.xrayOutbound = out;
    p.name = o.value("ps").toString(address);
    p.remark = "VMess";
    ParseLog::detail(QStringLiteral("vmess: ok add=%1 port=%2 net=%3 tls=%4")
                         .arg(address).arg(port).arg(net).arg(tlsOn ? "on" : "off"));
    return p;
}

static std::optional<ServerProfile> parseSs(const QString& uri) {
    static const QRegularExpression legacyRe(R"(^([^:@]+):([^@]+)@([^:]+):(\d+)$)");

    const int hash = uri.indexOf(QLatin1Char('#'));
    const QString fragment = hash >= 0 ? uri.mid(hash + 1) : QString();
    const QString body = hash >= 0 ? uri.left(hash) : uri;

    QString payload = body.startsWith("ss://", Qt::CaseInsensitive) ? body.mid(5) : body;
    const int qm = payload.indexOf(QLatin1Char('?'));
    const QString queryPart = qm >= 0 ? payload.mid(qm + 1) : QString();
    if (qm >= 0)
        payload = payload.left(qm);

    QString method;
    QString password;
    QString host;
    int port = -1;

    // Legacy: base64(method:password@host:port) as the whole payload
    {
        QByteArray dec = QByteArray::fromBase64(payload.toUtf8());
        if (dec.trimmed().isEmpty())
            dec = base64UrlDecode(payload);
        const auto m = legacyRe.match(QString::fromUtf8(dec));
        if (m.hasMatch()) {
            method = m.captured(1);
            password = m.captured(2);
            host = m.captured(3);
            port = m.captured(4).toInt();
        }
    }

    // SIP002: [base64(method:password)@|method:password@]host:port
    if (method.isEmpty()) {
        const QUrl u(uri);
        if (u.isValid() && u.scheme().compare("ss", Qt::CaseInsensitive) == 0) {
            host = u.host();
            port = u.port(-1);

            const QString user = u.userName();
            const QString pass = u.password();
            if (!user.isEmpty() && !pass.isEmpty()) {
                method = urlDecode(user);
                password = urlDecode(pass);
            } else {
                QString userInfo = !user.isEmpty() ? user : pass;
                if (!userInfo.isEmpty()) {
                    const QString plain =
                        QString::fromUtf8(QByteArray::fromBase64(userInfo.toUtf8()));
                    userInfo = plain.contains(QLatin1Char(':')) ? plain : urlDecode(userInfo);
                    const int colon = userInfo.indexOf(QLatin1Char(':'));
                    if (colon > 0) {
                        method = userInfo.left(colon);
                        password = userInfo.mid(colon + 1);
                    }
                }
            }
        }
    }

    if (method.isEmpty() || host.isEmpty() || port <= 0)
        return std::nullopt;

    auto p = baseProfile(Protocol::Shadowsocks, uri);
    nlohmann::json server;
    server["address"] = host.toStdString();
    server["port"] = port;
    server["method"] = method.toStdString();
    server["password"] = password.toStdString();
    p.xrayOutbound = {
        {"protocol", "shadowsocks"},
        {"tag", "proxy"},
        {"settings", {{"servers", nlohmann::json::array({server})}}}
    };

    if (!queryPart.isEmpty()) {
        const QUrlQuery query(queryPart);
        const QString prefix = query.queryItemValue("prefix", QUrl::FullyDecoded);
        if (!prefix.isEmpty())
            p.xrayOutbound["settings"]["servers"][0]["prefix"] = prefix.toStdString();
    }

    p.name = urlDecode(fragment);
    if (p.name.isEmpty())
        p.name = host;
    p.remark = "Shadowsocks";
    return p;
}

static std::optional<ServerProfile> parseTrojan(const QString& uri) {
    QUrl u(uri);
    if (!u.isValid() || schemeOf(u) != "trojan") return std::nullopt;
    const QString host = u.host();
    if (host.isEmpty()) {
        ParseLog::detail(QStringLiteral("trojan: no host in '%1'").arg(u.toString().left(80)));
        return std::nullopt;
    }
    auto p = baseProfile(Protocol::Trojan, uri);
    const QString pass = urlDecode(u.userName());
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
    const QString scheme = schemeOf(u);
    if (!u.isValid() || (scheme != "socks" && scheme != "socks5")) return std::nullopt;
    if (u.host().isEmpty()) {
        ParseLog::detail(QStringLiteral("socks: no host in '%1'").arg(u.toString().left(80)));
        return std::nullopt;
    }
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

static std::optional<ServerProfile> parseSsh(const QString& uri) {
    QUrl u(uri);
    if (!u.isValid() || u.scheme().compare("ssh", Qt::CaseInsensitive) != 0)
        return std::nullopt;
    const QString host = u.host();
    if (host.isEmpty())
        return std::nullopt;

    auto p = baseProfile(Protocol::Ssh, uri);
    p.ssh.host = host;
    p.ssh.port = u.port(22);
    p.ssh.user = urlDecode(u.userName());
    p.ssh.password = urlDecode(u.password());

    QUrlQuery q(u);
    const QString key = queryParam(q, {"privateKey", "private_key", "key", "identity"});
    if (!key.isEmpty())
        p.ssh.privateKeyPath = key;
    const QString pass = queryParam(q, {"passphrase", "keyPassphrase"});
    if (!pass.isEmpty())
        p.ssh.passphrase = pass;

    if (p.ssh.user.isEmpty())
        p.ssh.user = QStringLiteral("root");

    QString tag = urlDecode(u.fragment());
    if (tag.contains('?'))
        tag = tag.split('?').first();
    p.name = tag.isEmpty() ? QString("%1:%2").arg(host).arg(p.ssh.port) : tag;
    p.remark = p.protocolLabel();
    return p;
}

static std::optional<ServerProfile> parseScheme(const QString& s) {
    if (s.startsWith("vless://", Qt::CaseInsensitive)) return parseVless(s);
    if (s.startsWith("vmess://", Qt::CaseInsensitive)) return parseVmess(s);
    if (s.startsWith("ss://", Qt::CaseInsensitive)) return parseSs(s);
    if (s.startsWith("trojan://", Qt::CaseInsensitive)) return parseTrojan(s);
    if (s.startsWith("socks://", Qt::CaseInsensitive) || s.startsWith("socks5://", Qt::CaseInsensitive))
        return parseSocks(s);
    if (s.startsWith("ssh://", Qt::CaseInsensitive)) return parseSsh(s);
    if (s.startsWith("hy2://", Qt::CaseInsensitive) || s.startsWith("hysteria2://", Qt::CaseInsensitive))
        return Hy2UriParser::parse(s);
    ParseLog::detail(QStringLiteral("unhandled scheme '%1'").arg(s.left(24)));
    return std::nullopt;
}

} // namespace

std::optional<ServerProfile> UriParser::parse(const QString& input) {
    const QString s = trimUri(input);
    const std::optional<ServerProfile> result = parseScheme(s);
    if (result)
        ParseLog::attempt(s, QStringLiteral("OK -> %1").arg(result->protocolLabel()));
    else
        ParseLog::attempt(s, QStringLiteral("FAILED (no parser accepted it)"));
    return result;
}

std::optional<ServerProfile> UriParser::parseScheme(const QString& s) {
    return ::vicvpn::parseScheme(s);
}

std::vector<ServerProfile> UriParser::parseMany(const QString& blob) {
    std::vector<ServerProfile> out;
    QString text = blob;
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF))
        text.remove(0, 1);

    const auto lines = text.split(QRegularExpression(R"([\r\n]+)"), Qt::SkipEmptyParts);
    int total = 0;
    for (const auto& line : lines) {
        const QString t = trimUri(line);
        if (t.isEmpty())
            continue;
        ++total;
        if (auto p = parse(t))
            out.push_back(*p);
    }
    if (total > 0 && out.size() < total) {
        ParseLog::detail(QStringLiteral("bulk parse: %1 of %2 lines failed")
                             .arg(total - out.size()).arg(total));
    }
    return out;
}

} // namespace vicvpn
