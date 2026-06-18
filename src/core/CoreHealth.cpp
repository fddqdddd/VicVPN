#include "vicvpn/core/CoreHealth.h"
#include <QTcpSocket>
#include <QHostAddress>
#include <QStringList>

namespace vicvpn {

bool CoreHealth::xrayHasConfigError(const QString& log) {
    return log.contains("failed to load", Qt::CaseInsensitive) ||
           log.contains("failed to decode", Qt::CaseInsensitive) ||
           log.contains("invalid config", Qt::CaseInsensitive) ||
           log.contains("unknown transport", Qt::CaseInsensitive) ||
           log.contains("not valid", Qt::CaseInsensitive) ||
           (log.contains("config", Qt::CaseInsensitive) &&
            log.contains("error", Qt::CaseInsensitive));
}

bool CoreHealth::xrayHasFatalTunError(const QString& log) {
    return log.contains("unable to set routes", Qt::CaseInsensitive) ||
           log.contains("unable to set ips", Qt::CaseInsensitive) ||
           log.contains("failed to start tun", Qt::CaseInsensitive) ||
           log.contains("failed to create tun", Qt::CaseInsensitive) ||
           log.contains("failed to configure tun", Qt::CaseInsensitive) ||
           log.contains("Cannot create a file when that file already exists", Qt::CaseInsensitive) ||
           log.contains("Failed to setup adapter", Qt::CaseInsensitive);
}

bool CoreHealth::xrayIndicatesStarted(const QString& log) {
    return log.contains("started", Qt::CaseInsensitive);
}

bool CoreHealth::xrayLooksReady(const QString& log, bool running) {
    return running && !xrayHasFatalTunError(log);
}

QString CoreHealth::formatLogTail(const QString& log, int maxLines) {
    const auto lines = log.trimmed().split('\n', Qt::SkipEmptyParts);
    QString out;
    for (int i = qMax(0, lines.size() - maxLines); i < lines.size(); ++i) {
        if (!out.isEmpty())
            out += '\n';
        out += lines[i].trimmed().left(240);
    }
    return out;
}

bool CoreHealth::tun2socksHasFatal(const QString& log) {
    return log.contains("\"level\":\"fatal\"", Qt::CaseInsensitive) ||
           log.contains("failed to start", Qt::CaseInsensitive) ||
           log.contains("cipher not supported", Qt::CaseInsensitive);
}

QString CoreHealth::tun2socksErrorMessage(const QString& log) {
    if (log.contains("cipher not supported", Qt::CaseInsensitive))
        return QString::fromUtf8(
            "tun2socks: ошибка Shadowsocks (ключ Дядя Ваня/Outline). "
            "Обновите VicVPN; при повторе отключите «Быстрый режим» в настройках.");
    const auto idx = log.indexOf("\"msg\":\"");
    if (idx >= 0) {
        const int start = idx + 7;
        const int end = log.indexOf('"', start);
        if (end > start) {
            QString msg = log.mid(start, end - start);
            msg.replace("\\\"", "\"");
            if (!msg.isEmpty())
                return QString("tun2socks: %1").arg(msg);
        }
    }
    const QString tail = formatLogTail(log, 3);
    if (!tail.isEmpty())
        return tail.left(300);
    return {};
}

bool CoreHealth::proxyGatewayFailed(const QString& log) {
    if (!log.contains("i/o timeout", Qt::CaseInsensitive))
        return false;
    if (log.contains("[proxy]", Qt::CaseInsensitive) ||
        log.contains("outbound/shadowsocks", Qt::CaseInsensitive) ||
        log.contains("outbound/vless", Qt::CaseInsensitive) ||
        log.contains("outbound/vmess", Qt::CaseInsensitive) ||
        log.contains("outbound/trojan", Qt::CaseInsensitive))
        return true;
    // tun2socks direct Shadowsocks (Outline / Дядя Ваня)
    return log.contains("tunnel/tcp.go", Qt::CaseInsensitive) &&
           log.contains("connect to", Qt::CaseInsensitive);
}

bool CoreHealth::testTcpConnect(const QString& host, int port, int timeoutMs) {
    if (host.isEmpty() || port <= 0)
        return false;
    QTcpSocket sock;
    sock.connectToHost(host, static_cast<quint16>(port));
    return sock.waitForConnected(timeoutMs);
}

bool CoreHealth::testSocks5Connect(int socksPort, const QString& targetHost, int targetPort,
                                   int timeoutMs) {
    if (targetHost.isEmpty() || targetPort <= 0 || socksPort <= 0)
        return false;

    QTcpSocket sock;
    sock.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(socksPort));
    if (!sock.waitForConnected(qMin(3000, timeoutMs)))
        return false;

    const char greeting[] = {0x05, 0x01, 0x00};
    if (sock.write(greeting, sizeof(greeting)) != sizeof(greeting))
        return false;
    if (!sock.waitForReadyRead(qMin(3000, timeoutMs)))
        return false;
    const QByteArray method = sock.read(2);
    if (method.size() < 2 || static_cast<uchar>(method[0]) != 0x05 ||
        static_cast<uchar>(method[1]) != 0x00)
        return false;

    QByteArray req;
    req.append(char(0x05));
    req.append(char(0x01));
    req.append(char(0x00));
    QHostAddress ip(targetHost);
    if (ip.protocol() == QAbstractSocket::IPv4Protocol) {
        req.append(char(0x01));
        const quint32 v4 = ip.toIPv4Address();
        req.append(char((v4 >> 24) & 0xff));
        req.append(char((v4 >> 16) & 0xff));
        req.append(char((v4 >> 8) & 0xff));
        req.append(char(v4 & 0xff));
    } else {
        const QByteArray host = targetHost.toUtf8();
        if (host.isEmpty() || host.size() > 255)
            return false;
        req.append(char(0x03));
        req.append(char(host.size()));
        req.append(host);
    }
    req.append(char((targetPort >> 8) & 0xff));
    req.append(char(targetPort & 0xff));
    if (sock.write(req) != req.size())
        return false;
    if (!sock.waitForReadyRead(timeoutMs))
        return false;
    const QByteArray rep = sock.read(10);
    return rep.size() >= 2 && static_cast<uchar>(rep[1]) == 0x00;
}

QString CoreHealth::singboxErrorMessage(const QString& log) {
    if (proxyGatewayFailed(log))
        return QString::fromUtf8(
            "VPN-сервер недоступен. Попробуйте другую страну в ssconf или другой ключ.");
    if (log.contains("Cannot create a file when that file already exists", Qt::CaseInsensitive) ||
        log.contains("open existing adapter", Qt::CaseInsensitive) ||
        log.contains("configure tun interface", Qt::CaseInsensitive))
        return QString::fromUtf8(
            "Ошибка TUN-адаптера. Отключите VPN, подождите 5 сек и подключитесь снова.");
    if (log.contains("legacy inbound fields", Qt::CaseInsensitive))
        return QString::fromUtf8("Устаревший формат конфига sing-box. Пересоберите VicVPN.");
    const QString tail = formatLogTail(log, 4);
    if (!tail.isEmpty())
        return tail.left(320);
    return {};
}

} // namespace vicvpn
