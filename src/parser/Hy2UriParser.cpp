#include "vicvpn/parser/UriParser.h"
#include "vicvpn/util/StringUtil.h"
#include <QUrl>
#include <QUrlQuery>

namespace vicvpn {

std::optional<ServerProfile> Hy2UriParser::parse(const QString& input) {
    QString uri = trimUri(input);
    QString scheme = "hysteria2";
    if (uri.startsWith("hy2://", Qt::CaseInsensitive)) {
        uri.replace(0, 5, "hysteria2://");
    }
    QUrl u(uri);
    if (!u.isValid()) return std::nullopt;
    scheme = u.scheme().toLower();
    if (scheme != "hysteria2" && !scheme.startsWith("hysteria2+"))
        return std::nullopt;

    ServerProfile p;
    p.protocol = Protocol::Hysteria2;
    p.core = CoreType::Hysteria2;
    p.rawUri = input;

    QString auth = urlDecode(u.userName());
    QString host = u.host();
    int port = u.port(443);
    QUrlQuery q(u);

    if (scheme.contains("realm")) {
        const QString realm = u.path().startsWith('/') ? u.path().mid(1) : u.path();
        p.hy2Config["realm_mode"] = true;
        p.hy2Config["realm"] = realm.toStdString();
        p.hy2Config["rendezvous"] = (host + ":" + QString::number(port)).toStdString();
        p.hy2Config["token"] = auth.toStdString();
        if (q.hasQueryItem("auth"))
            p.hy2Config["auth"] = q.queryItemValue("auth").toStdString();
    } else {
        p.hy2Config["server"] = (host + ":" + QString::number(port)).toStdString();
        p.hy2Config["auth"] = auth.toStdString();
    }

    if (q.hasQueryItem("obfs"))
        p.hy2Config["obfs"] = q.queryItemValue("obfs").toStdString();
    if (q.hasQueryItem("obfs-password"))
        p.hy2Config["obfs-password"] = q.queryItemValue("obfs-password").toStdString();
    if (q.hasQueryItem("sni"))
        p.hy2Config["sni"] = q.queryItemValue("sni").toStdString();
    if (q.hasQueryItem("insecure"))
        p.hy2Config["insecure"] = q.queryItemValue("insecure") == "1" || q.queryItemValue("insecure") == "true";
    if (q.hasQueryItem("pinSHA256"))
        p.hy2Config["pinSHA256"] = q.queryItemValue("pinSHA256").toStdString();
    if (q.hasQueryItem("mport"))
        p.hy2Config["mport"] = q.queryItemValue("mport").toStdString();

    p.name = urlDecode(u.fragment());
    if (p.name.isEmpty()) p.name = host;
    p.remark = "Hysteria2";
    return p;
}

} // namespace vicvpn
