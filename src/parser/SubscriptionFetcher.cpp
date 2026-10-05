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

static bool looksLikeHtml(const QString& text) {
    const QString head = text.left(600).toLower();
    return head.contains("<!doctype html") || head.contains("<html") ||
           head.contains("<form") || head.contains("<head") || head.contains("recaptcha") ||
           head.contains("g-recaptcha") || head.contains("<title>");
}

static std::vector<ServerProfile> parseSubscriptionText(const QString& text, QString* error) {
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        if (error) *error = "Пустой ответ сервера";
        return {};
    }

    if (looksLikeHtml(t)) {
        ParseLog::detail(QStringLiteral("response looks like HTML, not a key list (first 200: %1)")
                             .arg(QString::fromUtf8(t.left(200).toUtf8()).replace('\n', ' ')));
        if (error)
            *error = "Сервер вернул HTML-страницу, а не список ключей. "
                     "Скорее всего нужна авторизация или капча — откройте ссылку в браузере. "
                     "Подробности в import.log";
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
    ParseLog::attempt(url, QStringLiteral("http subscription fetch"));
    const QByteArray raw = httpGetSubscription(url, error);
    if (raw.isEmpty()) {
        if (error && error->isEmpty())
            *error = QStringLiteral("Сервер вернул пустой ответ (0 байт). Проверьте ссылку подписки.");
        ParseLog::attempt(url, QStringLiteral("FAILED (empty body, error='%1')")
                                  .arg(error && !error->isEmpty() ? *error : QStringLiteral("none")));
        return {};
    }
    ParseLog::detail(QStringLiteral("subscription body: %1 bytes").arg(raw.size()));
    auto servers = SubscriptionBodyDecoder::decode(raw, error);
    ParseLog::attempt(url, QStringLiteral("-> %1 profile(s)").arg(servers.size()));
    for (auto& s : servers)
        s.subscriptionUrl = url;
    return servers;
}

// People paste SSH targets in every shape: "ssh://root:pw@h:22", "root@h:22",
// "h:22", "ssh root@h". Normalise them so only the explicit parser deals with URIs.
static QString normalizeSshTarget(const QString& input) {
    const QString t = input.trimmed();
    if (t.isEmpty() || t.contains(QLatin1String("://")))
        return t;

    static const QRegularExpression withUser(
        QStringLiteral(R"(^(?:ssh\s+)?([A-Za-z0-9._\-]+)(?:@)([A-Za-z0-9._\-]+)(?::(\d{1,5}))?$)"));
    static const QRegularExpression hostOnly(
        QStringLiteral(R"(^(?:ssh\s+)?([A-Za-z0-9._\-]+)(?::(\d{1,5}))?$)"));

    const auto m = withUser.match(t);
    if (m.hasMatch()) {
        const int port = m.captured(3).isEmpty() ? 22 : m.captured(3).toInt();
        return QStringLiteral("ssh://%1@%2:%3").arg(m.captured(1), m.captured(2)).arg(port);
    }
    const auto h = hostOnly.match(t);
    // A bare host only becomes SSH when a port is spelled out, otherwise a
    // pasted domain would silently turn into a login attempt.
    if (h.hasMatch() && !h.captured(2).isEmpty())
        return QStringLiteral("ssh://%1:%2").arg(h.captured(1)).arg(h.captured(2).toInt());
    return t;
}

std::vector<ServerProfile> ImportService::importText(const QString& text, QString* error,
                                                     const ImportOptions& options) {
    QString t = trimUri(text);
    if (t.isEmpty()) {
        if (error) *error = "Empty input";
        return {};
    }
    t = normalizeSshTarget(t);
    if (t.startsWith("http://", Qt::CaseInsensitive) || t.startsWith("https://", Qt::CaseInsensitive)) {
        ParseLog::attempt(t, QStringLiteral("subscription fetch"));
        auto list = SubscriptionFetcher::fetch(t, error);
        ParseLog::detail(QStringLiteral("subscription '%1' -> %2 profile(s)")
                             .arg(ParseLog::detectScheme(t)).arg(list.size()));
        return list;
    }
    if (SsconfCountry::isSsconfUri(t)) {
        ParseLog::attempt(t, QStringLiteral("ssconf resolve"));
        auto list = SsconfResolver::resolve(t, error, options.ssconfCountry);
        ParseLog::detail(QStringLiteral("ssconf -> %1 profile(s)").arg(list.size()));
        return list;
    }
    if (t.startsWith('{') || t.startsWith('[')) {
        auto list = importJson(t, error);
        ParseLog::attempt(t, QStringLiteral("json import -> %1 profile(s)").arg(list.size()));
        return list;
    }
    // A pasted block may hold many keys: try bulk first, otherwise a single
    // vless:// on line 1 would win and hide every other line.
    if (t.contains(QLatin1Char('\n')) || t.contains(QLatin1Char('\r'))) {
        auto many = UriParser::parseMany(t);
        if (!many.empty())
            return many;
    }

    if (auto one = UriParser::parse(t))
        return {*one};

    const auto many = UriParser::parseMany(t);
    if (!many.empty())
        return many;

    if (error && error->isEmpty()) {
        const QString scheme = ParseLog::detectScheme(t);
        *error = scheme == QLatin1String("unknown")
            ? QStringLiteral("Не распознан формат. Ожидается ключ вида vless://, vmess://, ss://, "
                             "trojan://, socks://, hy2://, ssh:// либо ссылка https:// на подписку.")
            : QStringLiteral("Формат «%1://» не удалось разобрать. Подробности в файле import.log")
                  .arg(scheme);
    }
    return {};
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
