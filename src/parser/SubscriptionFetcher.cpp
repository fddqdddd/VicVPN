#include "vicvpn/parser/ImportService.h"
#include "vicvpn/parser/UriParser.h"
#include "vicvpn/parser/SingboxConverter.h"
#include "vicvpn/parser/SsconfCountry.h"
#include "vicvpn/util/StringUtil.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace vicvpn {

static QByteArray httpGet(const QString& url, QString* error) {
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "VicVPN/0.1");
    QEventLoop loop;
    QNetworkReply* reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        if (error) *error = reply->errorString();
        reply->deleteLater();
        return {};
    }
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    return data;
}

static QString decodeSubscriptionBody(const QByteArray& raw) {
    const QString text = QString::fromUtf8(raw).trimmed();
    if (text.contains("://"))
        return text;
    const QByteArray dec = QByteArray::fromBase64(raw);
    return QString::fromUtf8(dec);
}

std::vector<ServerProfile> SubscriptionFetcher::fetch(const QString& url, QString* error) {
    const QByteArray raw = httpGet(url, error);
    if (raw.isEmpty()) return {};
    const QString body = decodeSubscriptionBody(raw);
    auto servers = UriParser::parseMany(body);
    for (auto& s : servers)
        s.subscriptionUrl = url;
    return servers;
}

std::vector<ServerProfile> ImportService::importText(const QString& text, QString* error,
                                                     const ImportOptions& options) {
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        if (error) *error = "Empty input";
        return {};
    }
    if (t.startsWith("http://", Qt::CaseInsensitive) || t.startsWith("https://", Qt::CaseInsensitive))
        return SubscriptionFetcher::fetch(t, error);
    if (SsconfCountry::isSsconfUri(t))
        return SsconfResolver::resolve(t, error, options.ssconfCountry);
    if (t.startsWith('{') || t.startsWith('['))
        return importJson(t, error);
    if (auto one = UriParser::parse(t))
        return {*one};
    return UriParser::parseMany(t);
}

static ServerProfile fromXrayOutbound(const nlohmann::json& ob, const QString& raw) {
    ServerProfile p;
    p.rawUri = raw;
    p.xrayOutbound = ob;
    const std::string proto = ob.value("protocol", "");
    if (proto == "vless") p.protocol = Protocol::Vless;
    else if (proto == "vmess") p.protocol = Protocol::Vmess;
    else if (proto == "shadowsocks") p.protocol = Protocol::Shadowsocks;
    else if (proto == "trojan") p.protocol = Protocol::Trojan;
    else if (proto == "socks") p.protocol = Protocol::Socks;
    p.core = CoreType::Xray;
    p.name = QString::fromStdString(ob.value("tag", "imported"));
    p.remark = p.protocolLabel();
    return p;
}

std::vector<ServerProfile> ImportService::importJson(const QString& jsonText, QString* error) {
    std::vector<ServerProfile> out;
    try {
        const auto j = nlohmann::json::parse(jsonText.toStdString());

        if (j.is_object()) {
            if (auto legacy = SingboxConverter::fromShadowsocksLegacy(j, jsonText))
                return {*legacy};
            if (SingboxConverter::isSingbox(j))
                return SingboxConverter::importProfiles(j, jsonText, error);
        }

        if (j.is_array()) {
            for (const auto& item : j) {
                if (item.contains("outbounds")) {
                    ServerProfile p;
                    p.passthroughJson = true;
                    p.passthroughConfig = item;
                    p.core = CoreType::Xray;
                    p.name = "JSON profile";
                    p.remark = "Passthrough";
                    out.push_back(p);
                } else if (item.contains("protocol")) {
                    out.push_back(fromXrayOutbound(item, jsonText));
                }
            }
            return out;
        }
        if (j.contains("outbounds")) {
            if (j.contains("inbounds")) {
                ServerProfile p;
                p.passthroughJson = true;
                p.passthroughConfig = j;
                p.core = CoreType::Xray;
                p.name = "Full JSON";
                p.remark = "Passthrough";
                out.push_back(p);
                return out;
            }
            for (const auto& ob : j["outbounds"]) {
                if (ob.value("protocol", "") == "freedom" || ob.value("protocol", "") == "blackhole")
                    continue;
                out.push_back(fromXrayOutbound(ob, jsonText));
            }
            if (!out.empty())
                return out;
        }
        if (error) *error = "Unsupported JSON format";
    } catch (const std::exception& e) {
        if (error) *error = e.what();
    }
    return out;
}

} // namespace vicvpn
