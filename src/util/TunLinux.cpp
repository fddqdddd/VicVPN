#include "vicvpn/util/TunLinux.h"
#include "vicvpn/util/AppPaths.h"
#include <QDateTime>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>

namespace vicvpn {

static void tunLog(const QString& line) {
    QFile log(AppPaths::coreLogFile());
    if (log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        log.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        log.write(" [routes] ");
        log.write(line.toUtf8());
        if (!line.endsWith('\n'))
            log.write("\n");
    }
}

static bool runIp(const QStringList& args, QString* out = nullptr, int timeoutMs = 8000) {
    QProcess proc;
    proc.setProgram("ip");
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForFinished(timeoutMs))
        return false;
    if (out)
        *out = QString::fromUtf8(proc.readAllStandardOutput());
    return proc.exitCode() == 0;
}

QString TunLinux::sessionTunName() { return QStringLiteral("vicvpn0"); }
QString TunLinux::singboxTunName() { return QStringLiteral("vicvpn0"); }
void TunLinux::purgeWintunPool() {}
void TunLinux::cleanupStaleDevices() {}

BypassRoute TunLinux::resolveBypassRoute(const QString& bypassHost) {
    BypassRoute route;
    if (bypassHost.isEmpty())
        return route;

    QString out;
    if (!runIp({"route", "get", bypassHost}, &out))
        return route;

    static QRegularExpression devRe(R"(\bdev\s+(\S+))");
    static QRegularExpression viaRe(R"(\bvia\s+(\S+))");
    const auto devM = devRe.match(out);
    const auto viaM = viaRe.match(out);
    if (devM.hasMatch())
        route.ifaceName = devM.captured(1);
    if (viaM.hasMatch())
        route.gateway = viaM.captured(1);
    route.valid = !route.ifaceName.isEmpty();
    return route;
}

bool TunLinux::ensureProxyBypass(const QString& bypassHost, const BypassRoute& preResolvedBypass,
                                 const QString&) {
    if (bypassHost.isEmpty())
        return true;

    BypassRoute route = preResolvedBypass.valid ? preResolvedBypass : resolveBypassRoute(bypassHost);
    if (!route.valid)
        return false;

    QStringList args = {"route", "replace", bypassHost + "/32"};
    if (!route.gateway.isEmpty())
        args << "via" << route.gateway;
    if (!route.ifaceName.isEmpty())
        args << "dev" << route.ifaceName;

    const bool ok = runIp(args);
    tunLog(QString("ensureBypassRoute %1 %2 ok=%3").arg(bypassHost, route.ifaceName).arg(ok));
    return ok;
}

bool TunLinux::ensureRoutes(const QString&, const QString&, const BypassRoute&, bool) {
    // sing-box TUN with auto_route handles default routing on Linux.
    return true;
}

void TunLinux::removeRoutes(const QString&, const QString& bypassHost, const BypassRoute&) {
    if (!bypassHost.isEmpty())
        runIp({"route", "del", bypassHost + "/32"}, nullptr, 3000);
    tunLog(QString("removeRoutes host=%1").arg(bypassHost));
}

int TunLinux::tunInterfaceIndex(const QString&) { return 0; }
void TunLinux::logNetworkAdapters() {}

} // namespace vicvpn
