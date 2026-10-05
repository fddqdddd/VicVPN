#include "vicvpn/util/StringUtil.h"
#include <QByteArray>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

namespace vicvpn {

QString urlDecode(const QString& s) {
    return QUrl::fromPercentEncoding(s.toUtf8());
}

QString urlEncode(const QString& s) {
    return QString::fromUtf8(QUrl::toPercentEncoding(s));
}

QByteArray base64UrlDecode(const QString& s) {
    QByteArray b = s.toUtf8();
    b.replace('-', '+');
    b.replace('_', '/');
    while (b.size() % 4)
        b.append('=');
    return QByteArray::fromBase64(b);
}

QString base64UrlEncode(const QByteArray& data) {
    return QString::fromUtf8(data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString trimUri(const QString& s) {
    QString t = s.trimmed();
    // Text copied from messengers and docs often keeps wrapping quotes or
    // smart quotes, which make QUrl reject the whole link.
    const QStringList quotes = {QStringLiteral("\""), QStringLiteral("'"),
                                QStringLiteral("\u201C"), QStringLiteral("\u201D"),
                                QStringLiteral("\u00AB"), QStringLiteral("\u00BB")};
    for (const QString& q : quotes) {
        if (t.size() >= 2 && t.startsWith(q) && t.endsWith(q)) {
            t = t.mid(1, t.size() - 2).trimmed();
            break;
        }
    }
    return t;
}

QUrl parseUriQuery(const QString& query) {
    return QUrl("?" + query);
}

QString hostFromUri(const QString& uri) {
    const QUrl u(uri);
    return u.host();
}

} // namespace vicvpn
