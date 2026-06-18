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

bool NetworkRoute::isTunAdapterPresent(const QString& name) {
    if (name.isEmpty())
        return false;
    QProcess proc;
    proc.setProgram("ip");
    proc.setArguments({"link", "show", name});
    proc.start();
    return proc.waitForFinished(3000) && proc.exitCode() == 0;
}

bool NetworkRoute::isTunAdapterPresent() {
    QProcess proc;
    proc.setProgram("ip");
    proc.setArguments({"link", "show", "type", "tun"});
    proc.start();
    if (!proc.waitForFinished(3000) || proc.exitCode() != 0)
        return false;
    return !QString::fromUtf8(proc.readAllStandardOutput()).trimmed().isEmpty();
}

} // namespace vicvpn
