#include "vicvpn/util/AdminElevation.h"
#include <QCoreApplication>
#include <QProcess>
#include <unistd.h>

namespace vicvpn {

bool isRunningAsAdmin() {
    return geteuid() == 0;
}

bool requestAdminRelaunch() {
    const QString exe = QCoreApplication::applicationFilePath();
    const QStringList argList = QCoreApplication::arguments().mid(1);
    QStringList args;
    args << exe;
    args << argList;
    QProcess::startDetached("pkexec", args);
    return true;
}

} // namespace vicvpn
