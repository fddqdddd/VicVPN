#include "vicvpn/parser/OutlineConfigParser.h"
#include <QRegularExpression>

namespace vicvpn {

static QString unquoteYamlValue(const QString& value) {
    QString v = value.trimmed();
    if ((v.startsWith('"') && v.endsWith('"')) || (v.startsWith('\'') && v.endsWith('\'')))
        return v.mid(1, v.size() - 2);
    return v;
}

static QString parseYamlScalar(const QString& block, const QString& key) {
    const QRegularExpression re(QString(R"(^%1:\s*(.+)$)").arg(QRegularExpression::escape(key)),
                                QRegularExpression::MultilineOption);
    const auto m = re.match(block);
    if (!m.hasMatch())
        return {};
    return unquoteYamlValue(m.captured(1));
}

static std::optional<ServerProfile> legacyShadowsocksBlock(const QString& block, const QString& raw) {
    const QString server = parseYamlScalar(block, "server");
    const QString method = parseYamlScalar(block, "method");
    const QString password = parseYamlScalar(block, "password");
    const QString portStr = parseYamlScalar(block, "server_port");
    if (server.isEmpty() || method.isEmpty() || password.isEmpty())
        return std::nullopt;

    bool ok = false;
    int port = portStr.toInt(&ok);
    if (!ok || port <= 0)
        port = 443;

    ServerProfile p;
    p.protocol = Protocol::Shadowsocks;
    p.core = CoreType::Xray;
    p.rawUri = raw.isEmpty() ? block : raw;
    p.xrayOutbound = {
        {"protocol", "shadowsocks"},
        {"tag", "proxy"},
        {"settings", {{"servers", nlohmann::json::array({{
            {"address", server.toStdString()},
            {"port", port},
            {"method", method.toStdString()},
            {"password", password.toStdString()}
        }})}}}
    };
    const QString prefix = parseYamlScalar(block, "prefix");
    if (!prefix.isEmpty())
        p.xrayOutbound["settings"]["servers"][0]["prefix"] = prefix.toStdString();
    p.name = parseYamlScalar(block, "remarks");
    if (p.name.isEmpty())
        p.name = server;
    p.remark = "Shadowsocks";
    return p;
}

bool OutlineConfigParser::looksLikeOutlineYaml(const QString& text) {
    const QString t = text.trimmed();
    if (!t.contains("server:", Qt::CaseInsensitive) || !t.contains("method:", Qt::CaseInsensitive))
        return false;
    return t.contains("password:", Qt::CaseInsensitive) || t.contains("secret:", Qt::CaseInsensitive);
}

std::vector<ServerProfile> OutlineConfigParser::importProfiles(const QString& yamlOrText,
                                                               const QString& raw) {
    std::vector<ServerProfile> out;
    const QString text = yamlOrText.trimmed();
    if (text.isEmpty())
        return out;

    if (auto legacy = legacyShadowsocksBlock(text, raw)) {
        out.push_back(*legacy);
        return out;
    }

    const QString tcpBlock = parseYamlScalar(text, "tcp");
    Q_UNUSED(tcpBlock);
    const int transportIdx = text.indexOf("transport:", 0, Qt::CaseInsensitive);
    if (transportIdx < 0)
        return out;

    const QString transport = text.mid(transportIdx);
    const QString endpoint = parseYamlScalar(transport, "endpoint");
    const QString cipher = parseYamlScalar(transport, "cipher");
    const QString secret = parseYamlScalar(transport, "secret");
    if (endpoint.isEmpty() || cipher.isEmpty() || secret.isEmpty())
        return out;

    QString host = endpoint;
    int port = 443;
    const int colon = endpoint.lastIndexOf(':');
    if (colon > 0) {
        host = endpoint.left(colon);
        bool ok = false;
        port = endpoint.mid(colon + 1).toInt(&ok);
        if (!ok || port <= 0)
            port = 443;
    }

    ServerProfile p;
    p.protocol = Protocol::Shadowsocks;
    p.core = CoreType::Xray;
    p.rawUri = raw.isEmpty() ? text : raw;
    p.xrayOutbound = {
        {"protocol", "shadowsocks"},
        {"tag", "proxy"},
        {"settings", {{"servers", nlohmann::json::array({{
            {"address", host.toStdString()},
            {"port", port},
            {"method", cipher.toStdString()},
            {"password", secret.toStdString()}
        }})}}}
    };
    const QString prefix = parseYamlScalar(transport, "prefix");
    if (!prefix.isEmpty())
        p.xrayOutbound["settings"]["servers"][0]["prefix"] = prefix.toStdString();
    p.name = host;
    p.remark = "Outline";
    out.push_back(p);
    return out;
}

} // namespace vicvpn
