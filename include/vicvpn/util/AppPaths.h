#pragma once

#include <QString>

namespace vicvpn {

class AppPaths {
public:
    static QString dataDir();
    static QString exeDir();
    static QString coreDir();
    static QString xrayExe();
    static QString singboxExe();
    static QString tun2socksExe();
    static QString hysteriaExe();
    static QString wintunDll();
    /** Extract bundled wintun.dll into core/ if missing. */
    static bool ensureWintunDll();
    static QString geoIp();
    static QString geoSite();
    static QString runtimeDir();
    static QString coreLogFile();
    static QString xrayErrorLogFile();
    /** Remove session logs and write a fresh header (call on each connect). */
    static void clearRuntimeLogs();
};

} // namespace vicvpn
