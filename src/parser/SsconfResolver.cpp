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
    ParseLog::detail(QStringLiteral("ssconf GET %1").arg(url));
    QFile log(AppPaths::coreLogFile());
    if (log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        log.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        log.write(" [ssconf] GET ");
        log.write(url.toUtf8());
        log.write("\n");
    }
    const QByteArray body = httpGetSubscription(url, error, 30000, "Happ/3.0");
    ParseLog::detail(QStringLiteral("ssconf response: %1 bytes, error='%2'")
                         .arg(body.size())
                         .arg(error && !error->isEmpty() ? *error : QStringLiteral("none")));
    return body;
}

std::vector<ServerProfile> SsconfResolver::resolve(const QString& ssconfUri, QString* error,
                                                   const QString& countryCode) {
    ParseLog::attempt(ssconfUri, QStringLiteral("ssconf resolve, country=%1").arg(countryCode));

    QByteArray raw;
    if (auto inlineBody = inlineSsconfBody(ssconfUri)) {
        raw = *inlineBody;
        ParseLog::detail(QStringLiteral("ssconf: inline body, %1 bytes").arg(raw.size()));
    } else {
        const QString url = toFetchUrl(ssconfUri, countryCode);
        if (url.isEmpty()) {
            ParseLog::detail(QStringLiteral("ssconf: cannot build fetch URL from URI"));
            if (error) *error = "Invalid ssconf URI";
            return {};
        }
        raw = httpGetSsconf(url, error);
        if (raw.isEmpty()) {
            ParseLog::attempt(ssconfUri, QStringLiteral("ssconf FAILED (empty body)"));
            if (error && error->isEmpty())
                *error = QStringLiteral("Сервер вернул пустой ответ");
            return {};
        }
    }

    const QString text = QString::fromUtf8(raw).trimmed();
    std::vector<ServerProfile> result = SubscriptionBodyDecoder::decode(raw, error);
    if (result.empty()) {
        ParseLog::attempt(ssconfUri, QStringLiteral("ssconf FAILED (no key recognised, %1 bytes)")
                                    .arg(raw.size()));
        if (error && error->isEmpty())
            *error = text.isEmpty() ? "Пустой ответ сервера" : "Не удалось распознать формат ответа";
        return {};
    }
    ParseLog::attempt(ssconfUri,
                      QStringLiteral("ssconf OK -> %1 profile(s)").arg(result.size()));

    const QString cc = countryCode.trimmed().toUpper();
    for (auto& s : result) {
        if (s.rawUri.isEmpty())
            s.rawUri = ssconfUri.trimmed();
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