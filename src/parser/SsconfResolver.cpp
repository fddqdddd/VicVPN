#include "vicvpn/parser/ImportService.h"
#include "vicvpn/util/StringUtil.h"
#include "vicvpn/parser/UriParser.h"
#include "vicvpn/parser/SsconfCountry.h"
#include "vicvpn/parser/OutlineConfigParser.h"
#include "vicvpn/util/AppPaths.h"
#include <QEventLoop>
#include <QFile>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <optional>

namespace vicvpn {

static QString stripSsconfPrefix(QString payload) {
    if (payload.startsWith("ssconf://", Qt::CaseInsensitive))
        return payload.mid(9);
    if (payload.startsWith("ssconf:", Qt::CaseInsensitive))
        return payload.mid(7);
    return payload;
}

static QString ssconfPayload(const QString& ssconfUri) {
    QString payload = stripSsconfPrefix(ssconfUri.trimmed());
    const int hash = payload.indexOf('#');
    if (hash >= 0)
        payload = payload.left(hash);
    return payload.trimmed();
}

static std::optional<QByteArray> inlineSsconfBody(const QString& ssconfUri) {
    const QString payload = ssconfPayload(ssconfUri);
    if (payload.startsWith('{') || payload.startsWith('['))
        return payload.toUtf8();

    QByteArray decoded = QByteArray::fromBase64(payload.toUtf8());
    if (decoded.isEmpty() || !decoded.contains('.'))
        decoded = base64UrlDecode(payload);
    const QString text = QString::fromUtf8(decoded).trimmed();
    if (text.startsWith('{') || text.startsWith('['))
        return text.toUtf8();
    return std::nullopt;
}

QString SsconfResolver::toFetchUrl(const QString& ssconfUri, const QString& countryCode) {
    QString payload = ssconfPayload(ssconfUri);
    QString hashParams;
    const QString fullPayload = stripSsconfPrefix(ssconfUri.trimmed());
    const int hash = fullPayload.indexOf('#');
    if (hash >= 0) {
        hashParams = fullPayload.mid(hash + 1);
    }

    QString url;
    if (payload.startsWith("http://", Qt::CaseInsensitive) ||
        payload.startsWith("https://", Qt::CaseInsensitive))
        url = payload;
    else if (payload.contains('.') && (payload.contains('/') || payload.contains(':')))
        url = "https://" + payload;
    else {
        QByteArray decoded = QByteArray::fromBase64(payload.toUtf8());
        if (decoded.isEmpty() || !decoded.contains('.'))
            decoded = base64UrlDecode(payload);
        const QString text = QString::fromUtf8(decoded).trimmed();
        if (text.startsWith("http://", Qt::CaseInsensitive) ||
            text.startsWith("https://", Qt::CaseInsensitive))
            url = text;
        else if (text.startsWith('{') || text.startsWith('['))
            return {};
        else if (!text.isEmpty())
            url = "https://" + text;
        else
            url = "https://" + payload;
    }

    QUrl u(url);
    QUrlQuery query(u);
    if (!hashParams.isEmpty()) {
        const QUrlQuery hashQuery(hashParams);
        for (const auto& item : hashQuery.queryItems(QUrl::FullyDecoded)) {
            if (!query.hasQueryItem(item.first))
                query.addQueryItem(item.first, item.second);
        }
    }

    QString country = countryCode.trimmed().toUpper();
    if (country.isEmpty() && query.hasQueryItem("country"))
        country = query.queryItemValue("country").toUpper();
    if (country.isEmpty() && query.hasQueryItem("location"))
        country = query.queryItemValue("location").toUpper();

    if (!country.isEmpty()) {
        const QString cc = country.toLower();
        query.removeAllQueryItems("country");
        query.removeAllQueryItems("location");
        query.removeAllQueryItems("region");
        query.removeAllQueryItems("geo");
        query.addQueryItem("country", cc);

        QUrlQuery hashQuery(hashParams);
        if (!hashQuery.hasQueryItem("country"))
            hashQuery.addQueryItem("country", cc);
        hashParams = hashQuery.toString(QUrl::FullyEncoded);
    }

    u.setQuery(query);
    if (!hashParams.isEmpty())
        u.setFragment(hashParams);
    return u.toString(QUrl::FullyEncoded);
}

static QByteArray httpGetSsconf(const QString& url, QString* error) {
    QFile log(AppPaths::coreLogFile());
    if (log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        log.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        log.write(" [ssconf] GET ");
        log.write(url.toUtf8());
        log.write("\n");
    }

    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "Happ/3.0");
    QEventLoop loop;
    QNetworkReply* reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        if (error) {
            if (reply->error() == QNetworkReply::AuthenticationRequiredError)
                *error = QString::fromUtf8(
                    "Сервер отклонил запрос (401). Не меняйте путь ключа — выберите «Авто» в стране.");
            else
                *error = reply->errorString();
        }
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
    return QString::fromUtf8(QByteArray::fromBase64(raw));
}

std::vector<ServerProfile> SsconfResolver::resolve(const QString& ssconfUri, QString* error,
                                                   const QString& countryCode) {
    QByteArray raw;
    if (auto inlineBody = inlineSsconfBody(ssconfUri)) {
        raw = *inlineBody;
    } else {
        const QString url = toFetchUrl(ssconfUri, countryCode);
        if (url.isEmpty()) {
            if (error) *error = "Invalid ssconf URI";
            return {};
        }
        raw = httpGetSsconf(url, error);
        if (raw.isEmpty())
            return {};
    }

    const QString text = QString::fromUtf8(raw).trimmed();
    std::vector<ServerProfile> result;
    if (text.startsWith('{') || text.startsWith('[')) {
        result = ImportService::importJson(text, error);
    } else if (OutlineConfigParser::looksLikeOutlineYaml(text)) {
        result = OutlineConfigParser::importProfiles(text, ssconfUri);
    } else {
        result = UriParser::parseMany(decodeSubscriptionBody(raw));
        if (result.empty()) {
            if (auto one = UriParser::parse(text))
                result.push_back(*one);
            else if (error && error->isEmpty())
                *error = "Remote config contains no supported URIs";
        }
    }
    if (result.empty() && error && error->isEmpty())
        *error = "Unsupported remote JSON format";

    const QString cc = countryCode.trimmed().toUpper();
    for (auto& s : result) {
        s.subscriptionUrl = ssconfUri.trimmed();
        s.countryCode = cc;
        if (!cc.isEmpty()) {
            const QString flag = SsconfCountry::flagEmoji(cc);
            const QString label = SsconfCountry::displayName(cc);
            if (!s.name.contains(label, Qt::CaseInsensitive))
                s.name = flag.isEmpty() ? label : (flag + " " + label);
        }
    }
    return result;
}

ServerProfile SsconfResolver::pickByCountry(const std::vector<ServerProfile>& list,
                                            const QString& countryCode) {
    if (list.empty())
        return {};
    if (list.size() == 1)
        return list.front();
    if (!countryCode.isEmpty()) {
        const QString label = SsconfCountry::displayName(countryCode);
        for (const auto& s : list) {
            if (s.name.contains(label, Qt::CaseInsensitive) ||
                s.name.contains(countryCode, Qt::CaseInsensitive))
                return s;
        }
    }
    return list.front();
}

} // namespace vicvpn