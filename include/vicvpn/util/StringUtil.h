#pragma once

#include <QString>
#include <QUrl>

namespace vicvpn {

QString urlDecode(const QString& s);
QString urlEncode(const QString& s);
QByteArray base64UrlDecode(const QString& s);
QString base64UrlEncode(const QByteArray& data);
QString trimUri(const QString& s);
QUrl parseUriQuery(const QString& query);
QString hostFromUri(const QString& uri);

} // namespace vicvpn
