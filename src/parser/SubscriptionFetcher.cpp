#include "vicvpn/parser/ImportService.h"
#include "vicvpn/parser/UriParser.h"
#include "vicvpn/parser/SingboxConverter.h"
#include "vicvpn/parser/OutlineConfigParser.h"
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
#include <QTimer>
#include <QUrl>

namespace vicvpn {

QByteArray httpGetSubscription(const QString& url, QString* error, int timeoutMs,
                               const QByteArray& userAgent) {
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(timeoutMs);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QNetworkReply* reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    timer.stop();

    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorText = reply->errorString();
    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (netError != QNetworkReply::NoError) {
        if (error) {
            if (netError == QNetworkReply::OperationCanceledError)
                *error = QString("Таймаут ответа сервера (%1 с)").arg(timeoutMs / 1000);
            else if (netError == QNetworkReply::AuthenticationRequiredError)
                *error = "Сервер отклонил запрос (401). Проверьте ключ подписки.";
            else if (netError == QNetworkReply::SslHandshakeFailedError)
                *error = "Ошибка SSL/TLS: " + netErrorText;
            else
                *error = netErrorText;
        }
        return {};
    }
    return data;
}

static std::vector<ServerProfile> parseSubscriptionText(const QString& text, QString* error) {
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        if (error) *error = "Пустой ответ сервера";
        return {};
    }

    if (t.startsWith('{') || t.startsWith('[')) {
        QString scratch;
        auto v = ImportService::importJson(t, &scratch);
        if (!v.empty())
            return v;
    }

    if (OutlineConfigParser::looksLikeOutlineYaml(t)) {
        auto v = OutlineConfigParser::importProfiles(t);
        if (!v.empty())
            return v;
    }

    auto v = UriParser::parseMany(t);
    if (!v.empty())
        return v;
    if (auto one = UriParser::parse(t))
        return {*one};

    if (error && error->isEmpty())
        *error = "Ответ не содержит поддерживаемых ключей "
                 "(ожидаются vless://, vmess://, ss://, trojan://, socks://, hy2://, "
                 "JSON или base64-список)";
    return {};
}

std::vector<ServerProfile> SubscriptionBodyDecoder::decode(const QByteArray& raw, QString* error) {
    if (raw.isEmpty()) {
        if (error) *error = "Пустой ответ сервера";
        return {};
    }

    QString scratch;
    const QString text = QString::fromUtf8(raw).trimmed();
    auto direct = parseSubscriptionText(text, &scratch);
    if (!direct.empty())
        return direct;

    QByteArray decoded = QByteArray::fromBase64(raw);
    if (decoded.trimmed().isEmpty())
        decoded = base64UrlDecode(text);
    const QString plain = QString::fromUtf8(decoded).trimmed();
    if (!plain.isEmpty() && plain != text) {
        auto v = parseSubscriptionText(plain, &scratch);
        if (!v.empty())
            return v;
    }

    if (error) *error = scratch;
    return {};
}

std::vector<ServerProfile> SubscriptionFetcher::fetch(const QString& url, QString* error) {
    const QByteArray raw = httpGetSubscription(url, error);
    if (raw.isEmpty())
        return {};
    auto servers = SubscriptionBodyDecoder::decode(raw, error);
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
