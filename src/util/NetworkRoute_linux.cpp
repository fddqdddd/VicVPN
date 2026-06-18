#include "vicvpn/util/NetworkRoute.h"
#include <QProcess>
#include <QRegularExpression>

namespace vicvpn {

QString NetworkRoute::localIpv4ForHost(const QString& host, int port) {
    Q_UNUSED(port);
    if (host.isEmpty())
        return {};

    QProcess proc;
    proc.setProgram("ip");
    proc.setArguments({"route", "get", host});
    proc.start();
    if (!proc.waitForFinished(5000))
        return {};

    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    static QRegularExpression srcRe(R"(\bsrc\s+(\d+\.\d+\.\d+\.\d+))");
    const auto m = srcRe.match(out);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString NetworkRoute::interfaceNameForIfIndex(int) {
    return {};
}

QString NetworkRoute::interfaceNameForLocalIpv4(const QString&) {
    return {};
}

} // namespace vicvpn
