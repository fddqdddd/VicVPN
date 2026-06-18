#include "vicvpn/util/AppPaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>

namespace vicvpn {

static QString coreBin(const QString& name) {
#if defined(Q_OS_WIN)
    return AppPaths::coreDir() + "/" + name + ".exe";
#else
    return AppPaths::coreDir() + "/" + name;
#endif
}

QString AppPaths::exeDir() {
    return QCoreApplication::applicationDirPath();
}

QString AppPaths::dataDir() {
    const QString portable = exeDir() + "/data";
    if (QDir(portable).exists() || QFile::exists(exeDir() + "/portable")) {
        QDir().mkpath(portable);
        return portable;
    }
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path;
}

QString AppPaths::coreDir() {
    const QString bundled = exeDir() + "/core";
    if (QDir(bundled).exists())
        return bundled;
    QDir().mkpath(bundled);
    return bundled;
}

QString AppPaths::xrayExe() { return coreBin("xray"); }
QString AppPaths::singboxExe() { return coreBin("sing-box"); }
QString AppPaths::tun2socksExe() { return coreBin("tun2socks"); }
QString AppPaths::hysteriaExe() { return coreBin("hysteria"); }
QString AppPaths::wintunDll() { return coreBin("wintun.dll"); }

bool AppPaths::ensureWintunDll() {
#if !defined(Q_OS_WIN)
    return true;
#else
    const QString path = wintunDll();
    if (QFileInfo::exists(path))
        return true;
    QFile bundled(":/cores/wintun.dll");
    if (!bundled.exists())
        return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (QFile::exists(path))
        QFile::remove(path);
    if (!bundled.copy(path))
        return false;
    QFile license(":/cores/LICENSE-wintun.txt");
    if (license.exists()) {
        const QString licensePath = coreDir() + "/LICENSE-wintun.txt";
        if (QFile::exists(licensePath))
            QFile::remove(licensePath);
        license.copy(licensePath);
    }
    return QFileInfo::exists(path);
#endif
}
QString AppPaths::geoIp() { return coreDir() + "/geoip.dat"; }
QString AppPaths::geoSite() { return coreDir() + "/geosite.dat"; }

QString AppPaths::runtimeDir() {
    const QString path = dataDir() + "/runtime";
    QDir().mkpath(path);
    return path;
}

QString AppPaths::coreLogFile() {
    return runtimeDir() + "/core.log";
}

QString AppPaths::xrayErrorLogFile() {
    return runtimeDir() + "/xray-error.log";
}

void AppPaths::clearRuntimeLogs() {
    QFile::remove(coreLogFile());
    QFile::remove(xrayErrorLogFile());
    QFile log(coreLogFile());
    if (log.open(QIODevice::WriteOnly | QIODevice::Text)) {
        log.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        log.write(" === VicVPN session ===\n");
    }
}

} // namespace vicvpn
