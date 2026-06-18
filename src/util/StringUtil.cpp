#include "vicvpn/util/StringUtil.h"
#include <QByteArray>
#include <QRegularExpression>
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
    return s.trimmed();
}

QUrl parseUriQuery(const QString& query) {
    return QUrl("?" + query);
}

QString hostFromUri(const QString& uri) {
    const QUrl u(uri);
    return u.host();
}

} // namespace vicvpn
