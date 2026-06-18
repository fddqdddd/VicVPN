#include "vicvpn/util/TunWindows.h"
#include "vicvpn/util/AppPaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QCryptographicHash>
#include <QUuid>
#include <QDateTime>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <QtGlobal>

namespace vicvpn {

static QString g_sessionTun;
static bool g_sessionRoutesReady = false;
static bool g_physicalBumped = false;
static bool g_tunDnsSet = false;

static QString formatIpv4(DWORD addr) {
    in_addr a{};
    a.S_un.S_addr = addr;
    char buf[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &a, buf, sizeof(buf)))
        return QString::fromUtf8(buf);
    return QString("0x%1").arg(addr, 0, 16);
}

static DWORD interfaceRouteMetric(DWORD ifIndex, DWORD requested) {
    MIB_IPINTERFACE_ROW iface{};
    InitializeIpInterfaceEntry(&iface);
    iface.Family = AF_INET;
    iface.InterfaceIndex = ifIndex;
    if (GetIpInterfaceEntry(&iface) != NO_ERROR)
        return requested > 0 ? requested : 1;
    const DWORD ifaceMetric = iface.Metric > 0 ? iface.Metric : 1;
    return qMax(requested > 0 ? requested : 1, ifaceMetric);
}

static void logDiag(const QString& line) {
    QFile f(AppPaths::coreLogFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        f.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        f.write(" [routes] ");
        f.write(line.toUtf8());
        f.write("\n");
    }
}

static QString tunDevicePnPId(const QString& tunName) {
    const QByteArray hash = QCryptographicHash::hash(tunName.toUtf8(), QCryptographicHash::Md5);
    const auto uuid = QUuid::fromRfc4122(hash);
    return QString("SWD\\Wintun\\{%1}").arg(uuid.toString(QUuid::WithoutBraces));
}

QString TunWindows::sessionTunName() {
    return QString("vcp%1").arg(QCoreApplication::applicationPid());
}

QString TunWindows::singboxTunName() {
    return QStringLiteral("vicvpn0");
}

void TunWindows::purgeWintunPool() {
    const QStringList dirs = {AppPaths::coreDir(), AppPaths::runtimeDir(),
                              QFileInfo(AppPaths::singboxExe()).absolutePath()};
    for (const QString& dir : dirs) {
        QDir d(dir);
        for (const QString& file : d.entryList({"*.wintun"}, QDir::Files)) {
            const QString path = d.filePath(file);
            if (QFile::remove(path))
                logDiag(QString("removed wintun pool %1").arg(path));
        }
    }
    QProcess::execute("netsh", {"interface", "set", "interface", "name=vicvpn0", "admin=disabled"});
}

static IP_ADAPTER_ADDRESSES* enumerateAdapters(std::vector<BYTE>& buffer) {
    ULONG size = 32 * 1024;
    buffer.resize(size);
    auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(size);
        addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &size);
    }
    return rc == NO_ERROR ? addrs : nullptr;
}

static QString adapterFriendly(const IP_ADAPTER_ADDRESSES* a) {
    return QString::fromWCharArray(a->FriendlyName);
}

static QString adapterDescription(const IP_ADAPTER_ADDRESSES* a) {
    return a->Description ? QString::fromWCharArray(a->Description) : QString();
}

static bool looksLikeWintun(const IP_ADAPTER_ADDRESSES* a) {
    const QString friendly = adapterFriendly(a);
    const QString desc = adapterDescription(a);
    return desc.contains("Wintun", Qt::CaseInsensitive) ||
           desc.contains("Xray", Qt::CaseInsensitive) ||
           friendly.contains("Xray Tunnel", Qt::CaseInsensitive) ||
           friendly.startsWith("vcp", Qt::CaseInsensitive) ||
           friendly.compare("vicvpn0", Qt::CaseInsensitive) == 0 ||
           friendly.compare("xray0", Qt::CaseInsensitive) == 0;
}

void TunWindows::cleanupStaleDevices() {
    purgeWintunPool();

    std::vector<BYTE> buffer;
    if (const auto* addrs = enumerateAdapters(buffer)) {
        for (auto* a = addrs; a; a = a->Next) {
            if (!looksLikeWintun(a))
                continue;
            const QString friendly = adapterFriendly(a);
            QProcess::execute("netsh",
                              {"interface", "set", "interface", QString("name=%1").arg(friendly),
                               "admin=disabled"});
            logDiag(QString("disabled wintun adapter %1").arg(friendly));
        }
    }
}

static bool isWintunIfIndex(DWORD ifIndex) {
    std::vector<BYTE> buffer;
    const auto* addrs = enumerateAdapters(buffer);
    if (!addrs)
        return false;
    for (auto* a = addrs; a; a = a->Next) {
        if (a->IfIndex == ifIndex)
            return looksLikeWintun(a);
    }
    return false;
}

static bool hasIpv4Prefix(const IP_ADAPTER_ADDRESSES* a, quint8 b0, quint8 b1, quint8 b2) {
    for (auto* uni = a->FirstUnicastAddress; uni; uni = uni->Next) {
        if (!uni->Address.lpSockaddr || uni->Address.lpSockaddr->sa_family != AF_INET)
            continue;
        const auto* sa = reinterpret_cast<const sockaddr_in*>(uni->Address.lpSockaddr);
        const auto* bytes = reinterpret_cast<const quint8*>(&sa->sin_addr);
        if (bytes[0] == b0 && bytes[1] == b1 && bytes[2] == b2)
            return true;
    }
    return false;
}

static QString adapterIpv4List(const IP_ADAPTER_ADDRESSES* a) {
    QStringList ips;
    for (auto* uni = a->FirstUnicastAddress; uni; uni = uni->Next) {
        if (!uni->Address.lpSockaddr || uni->Address.lpSockaddr->sa_family != AF_INET)
            continue;
        const auto* sa = reinterpret_cast<const sockaddr_in*>(uni->Address.lpSockaddr);
        char buf[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf)))
            ips << QString::fromUtf8(buf);
    }
    return ips.join(',');
}

void TunWindows::logNetworkAdapters() {
    std::vector<BYTE> buffer;
    const auto* addrs = enumerateAdapters(buffer);
    if (!addrs) {
        logDiag("GetAdaptersAddresses failed");
        return;
    }
    for (auto* a = addrs; a; a = a->Next) {
        logDiag(QString("adapter if=%1 name=%2 desc=%3 ip=%4")
                    .arg(a->IfIndex)
                    .arg(adapterFriendly(a))
                    .arg(adapterDescription(a))
                    .arg(adapterIpv4List(a)));
    }
}

int TunWindows::tunInterfaceIndex(const QString& tunName) {
    std::vector<BYTE> buffer;
    const auto* addrs = enumerateAdapters(buffer);
    if (!addrs)
        return 0;

    int exactMatch = 0;
    int subnetMatch = 0;
    int wintunMatch = 0;
    int xrayTunnelMatch = 0;
    int vcpPrefixMatch = 0;
    int wintunCount = 0;

    for (auto* a = addrs; a; a = a->Next) {
        const QString friendly = adapterFriendly(a);
        const bool wintun = looksLikeWintun(a);
        if (wintun)
            ++wintunCount;

        if (friendly.compare(tunName, Qt::CaseInsensitive) == 0)
            exactMatch = static_cast<int>(a->IfIndex);

        if (wintun && hasIpv4Prefix(a, 10, 10, 0))
            subnetMatch = static_cast<int>(a->IfIndex);

        if (wintun && friendly.startsWith("vcp", Qt::CaseInsensitive))
            vcpPrefixMatch = static_cast<int>(a->IfIndex);

        if (friendly.contains("Xray Tunnel", Qt::CaseInsensitive))
            xrayTunnelMatch = static_cast<int>(a->IfIndex);

        if (wintun && !subnetMatch)
            wintunMatch = static_cast<int>(a->IfIndex);
    }

    if (exactMatch > 0)
        return exactMatch;
    if (subnetMatch > 0)
        return subnetMatch;
    if (vcpPrefixMatch > 0)
        return vcpPrefixMatch;
    if (xrayTunnelMatch > 0)
        return xrayTunnelMatch;
    if (wintunCount == 1 && wintunMatch > 0)
        return wintunMatch;

    return 0;
}

static DWORD ipv4ToHost(const QString& ip) {
    in_addr addr{};
    if (inet_pton(AF_INET, ip.toUtf8().constData(), &addr) != 1)
        return 0;
    return addr.S_un.S_addr;
}

static PMIB_IPFORWARDTABLE fetchForwardTable() {
    ULONG size = 0;
    if (GetIpForwardTable(nullptr, &size, TRUE) != ERROR_INSUFFICIENT_BUFFER)
        return nullptr;
    auto* buffer = new BYTE[size];
    auto* table = reinterpret_cast<PMIB_IPFORWARDTABLE>(buffer);
    if (GetIpForwardTable(table, &size, TRUE) != NO_ERROR) {
        delete[] buffer;
        return nullptr;
    }
    return table;
}

static void freeForwardTable(PMIB_IPFORWARDTABLE table) {
    delete[] reinterpret_cast<BYTE*>(table);
}

static bool physicalDefaultRoute(DWORD excludeIfIndex, MIB_IPFORWARDROW* out) {
    if (!out)
        return false;
    const PMIB_IPFORWARDTABLE table = fetchForwardTable();
    if (!table)
        return false;

    bool found = false;
    DWORD bestMetric = MAXDWORD;
    MIB_IPFORWARDROW best{};

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IPFORWARDROW& row = table->table[i];
        if (row.dwForwardDest != 0 || row.dwForwardMask != 0)
            continue;
        if (row.dwForwardIfIndex == excludeIfIndex)
            continue;
        if (isWintunIfIndex(row.dwForwardIfIndex))
            continue;
        if (row.dwForwardMetric1 < bestMetric) {
            bestMetric = row.dwForwardMetric1;
            best = row;
            found = true;
        }
    }

    freeForwardTable(table);
    if (found)
        *out = best;
    return found;
}

static bool bestPhysicalRoute(const QString& host, DWORD excludeIfIndex, MIB_IPFORWARDROW* out) {
    if (!out || host.isEmpty())
        return false;
    const DWORD dst = ipv4ToHost(host);
    if (!dst)
        return false;

    MIB_IPFORWARDROW best{};
    if (GetBestRoute(dst, 0, &best) != NO_ERROR)
        return false;

    if (!isWintunIfIndex(best.dwForwardIfIndex) && best.dwForwardIfIndex != excludeIfIndex) {
        *out = best;
        return true;
    }

    if (physicalDefaultRoute(excludeIfIndex, out)) {
        logDiag(QString("GetBestRoute(%1) via wintun if=%2, using physical default if=%3 gw=%4")
                    .arg(host)
                    .arg(best.dwForwardIfIndex)
                    .arg(out->dwForwardIfIndex)
                    .arg(out->dwForwardNextHop));
        return true;
    }

    return false;
}

BypassRoute TunWindows::resolveBypassRoute(const QString& bypassHost) {
    BypassRoute result;
    if (bypassHost.isEmpty())
        return result;

    MIB_IPFORWARDROW row{};
    if (!bestPhysicalRoute(bypassHost, 0, &row))
        return result;

    result.ifIndex = row.dwForwardIfIndex;
    result.nextHop = row.dwForwardNextHop;
    result.valid = true;
    logDiag(QString("resolveBypassRoute %1 -> if=%2 gw=%3")
                .arg(bypassHost)
                .arg(result.ifIndex)
                .arg(formatIpv4(row.dwForwardNextHop)));
    return result;
}

static bool createForwardRow(DWORD dest, DWORD mask, DWORD nextHop, DWORD ifIndex, DWORD metric) {
    MIB_IPFORWARDROW row{};
    row.dwForwardDest = dest;
    row.dwForwardMask = mask;
    row.dwForwardPolicy = 0;
    row.dwForwardNextHop = nextHop;
    row.dwForwardIfIndex = ifIndex;
    row.dwForwardType = (nextHop != 0 && nextHop != dest) ? MIB_IPROUTE_TYPE_INDIRECT
                                                          : MIB_IPROUTE_TYPE_DIRECT;
    row.dwForwardProto = MIB_IPPROTO_NETMGMT;
    row.dwForwardAge = 0;
    row.dwForwardNextHopAS = 0;
    row.dwForwardMetric1 = interfaceRouteMetric(ifIndex, metric);
    const DWORD err = CreateIpForwardEntry(&row);
    if (err != NO_ERROR && err != ERROR_OBJECT_ALREADY_EXISTS) {
        logDiag(QString("CreateIpForwardEntry dest=%1 mask=%2 if=%3 gw=%4 metric=%5 err=%6")
                    .arg(formatIpv4(dest))
                    .arg(formatIpv4(mask))
                    .arg(ifIndex)
                    .arg(formatIpv4(nextHop))
                    .arg(row.dwForwardMetric1)
                    .arg(err));
    }
    return err == NO_ERROR || err == ERROR_OBJECT_ALREADY_EXISTS;
}

static bool deleteForwardRow(DWORD dest, DWORD mask, DWORD nextHop, DWORD ifIndex) {
    MIB_IPFORWARDROW row{};
    row.dwForwardDest = dest;
    row.dwForwardMask = mask;
    row.dwForwardNextHop = nextHop;
    row.dwForwardIfIndex = ifIndex;
    const DWORD err = DeleteIpForwardEntry(&row);
    return err == NO_ERROR || err == ERROR_NOT_FOUND;
}

static bool defaultRouteOnInterface(DWORD ifIndex) {
    const PMIB_IPFORWARDTABLE table = fetchForwardTable();
    if (!table)
        return false;
    bool found = false;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IPFORWARDROW& row = table->table[i];
        if (row.dwForwardDest == 0 && row.dwForwardMask == 0 && row.dwForwardIfIndex == ifIndex) {
            found = true;
            break;
        }
    }
    freeForwardTable(table);
    return found;
}

static bool runNetshBypassRouteAdd(const QString& host, int ifIndex, DWORD nextHop);
static bool bypassRoutesViaInterface(const QString& host, DWORD expectedIf);

static bool runNetshBypassRouteAdd(const QString& host, int ifIndex, DWORD nextHop) {
    QProcess proc;
    proc.setProgram("netsh.exe");
    proc.setArguments({"interface", "ipv4", "add", "route",
                       QString("prefix=%1/32").arg(host),
                       QString("interface=%1").arg(ifIndex),
                       QString("nexthop=%1").arg(formatIpv4(nextHop)),
                       "metric=1", "store=active"});
    proc.start();
    if (!proc.waitForFinished(8000))
        return false;
    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput() + proc.readAllStandardError());
    logDiag(QString("netsh bypass %1 if=%2 gw=%3 exit=%4 %5")
                .arg(host)
                .arg(ifIndex)
                .arg(formatIpv4(nextHop))
                .arg(proc.exitCode())
                .arg(out.trimmed()));
    if (proc.exitCode() == 0)
        return true;
    if (bypassRoutesViaInterface(host, static_cast<DWORD>(ifIndex)))
        return true;
    return out.contains("already exists", Qt::CaseInsensitive) ||
           out.contains("уже существует", Qt::CaseInsensitive) ||
           out.contains("object already exists", Qt::CaseInsensitive);
}

static bool runNetshBypassRouteDelete(const QString& host, int ifIndex, DWORD nextHop) {
    QProcess proc;
    proc.setProgram("netsh.exe");
    proc.setArguments({"interface", "ipv4", "delete", "route",
                       QString("prefix=%1/32").arg(host),
                       QString("interface=%1").arg(ifIndex),
                       QString("nexthop=%1").arg(formatIpv4(nextHop)),
                       "store=active"});
    proc.start();
    proc.waitForFinished(5000);
    return proc.exitCode() == 0;
}

static bool hostRouteOnInterface(DWORD host, DWORD ifIndex) {
    const PMIB_IPFORWARDTABLE table = fetchForwardTable();
    if (!table)
        return false;
    bool found = false;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IPFORWARDROW& row = table->table[i];
        if (row.dwForwardDest == host && row.dwForwardMask == 0xFFFFFFFF &&
            row.dwForwardIfIndex == ifIndex) {
            found = true;
            break;
        }
    }
    freeForwardTable(table);
    return found;
}

static bool bestDefaultRouteIf(DWORD* outIf, DWORD* outMetric) {
    const PMIB_IPFORWARDTABLE table = fetchForwardTable();
    if (!table)
        return false;
    bool found = false;
    DWORD bestMetric = MAXDWORD;
    DWORD bestIf = 0;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IPFORWARDROW& row = table->table[i];
        if (row.dwForwardDest != 0 || row.dwForwardMask != 0)
            continue;
        if (row.dwForwardMetric1 < bestMetric) {
            bestMetric = row.dwForwardMetric1;
            bestIf = row.dwForwardIfIndex;
            found = true;
        }
    }
    freeForwardTable(table);
    if (found && outIf)
        *outIf = bestIf;
    if (found && outMetric)
        *outMetric = bestMetric;
    return found;
}

static void optimizeTunDefaultRoute(DWORD tunIf) {
    const PMIB_IPFORWARDTABLE table = fetchForwardTable();
    if (!table)
        return;
    const DWORD metric = interfaceRouteMetric(tunIf, 1);
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        MIB_IPFORWARDROW row = table->table[i];
        if (row.dwForwardDest != 0 || row.dwForwardMask != 0)
            continue;
        if (row.dwForwardIfIndex != tunIf)
            continue;
        if (row.dwForwardMetric1 <= metric)
            continue;
        row.dwForwardMetric1 = metric;
        if (SetIpForwardEntry(&row) != NO_ERROR) {
            DeleteIpForwardEntry(&row);
            CreateIpForwardEntry(&row);
        }
        logDiag(QString("optimized TUN default metric on if=%1 to %2").arg(tunIf).arg(metric));
    }
    freeForwardTable(table);
}

static bool bypassRoutesViaInterface(const QString& host, DWORD expectedIf) {
    const DWORD dst = ipv4ToHost(host);
    if (!dst)
        return false;
    if (hostRouteOnInterface(dst, expectedIf)) {
        logDiag(QString("route table %1 /32 on if=%2").arg(host).arg(expectedIf));
        return true;
    }
    MIB_IPFORWARDROW best{};
    if (GetBestRoute(dst, 0, &best) != NO_ERROR)
        return false;
    logDiag(QString("GetBestRoute(%1) -> ifIndex=%2 gw=%3 metric=%4")
                .arg(host)
                .arg(best.dwForwardIfIndex)
                .arg(formatIpv4(best.dwForwardNextHop))
                .arg(best.dwForwardMetric1));
    return best.dwForwardIfIndex == expectedIf;
}

static bool ensureBypassRoute(const QString& bypassHost, const BypassRoute& bypass, DWORD tunIf) {
    if (bypassHost.isEmpty())
        return true;
    if (!bypass.valid)
        return false;
    if (bypass.ifIndex == tunIf) {
        logDiag(QString("proxy bypass skipped: would loop via TUN if=%1").arg(tunIf));
        return false;
    }

    const DWORD ifIndex = bypass.ifIndex;
    const DWORD nextHop = bypass.nextHop;
    const DWORD host = ipv4ToHost(bypassHost);

    if (hostRouteOnInterface(host, ifIndex)) {
        logDiag(QString("ensureBypassRoute %1 /32 already on if=%2").arg(bypassHost).arg(ifIndex));
        return true;
    }

    bool ok = runNetshBypassRouteAdd(bypassHost, static_cast<int>(ifIndex), nextHop);
    if (!ok)
        ok = createForwardRow(host, 0xFFFFFFFF, nextHop, ifIndex, 1);
    if (!ok && hostRouteOnInterface(host, ifIndex))
        ok = true;

    const bool verified = hostRouteOnInterface(host, ifIndex);
    logDiag(QString("ensureBypassRoute %1 ok=%2 verified=%3").arg(bypassHost).arg(ok).arg(verified));
    return ok && verified;
}

static bool runNetshRouteAdd(int ifIndex) {
    const DWORD metric = interfaceRouteMetric(static_cast<DWORD>(ifIndex), 1);
    QProcess proc;
    proc.setProgram("netsh.exe");
    proc.setArguments({"interface", "ipv4", "add", "route", "prefix=0.0.0.0/0",
                       QString("interface=%1").arg(ifIndex), "nexthop=0.0.0.0",
                       QString("metric=%1").arg(metric), "store=active"});
    proc.start();
    if (!proc.waitForFinished(8000))
        return false;
    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput() + proc.readAllStandardError());
    logDiag(QString("netsh add if=%1 exit=%2 %3").arg(ifIndex).arg(proc.exitCode()).arg(out.trimmed()));
    if (proc.exitCode() == 0)
        return true;
    if (defaultRouteOnInterface(static_cast<DWORD>(ifIndex)))
        return true;
    return out.contains("already exists", Qt::CaseInsensitive) ||
           out.contains("уже существует", Qt::CaseInsensitive) ||
           out.contains("object already exists", Qt::CaseInsensitive);
}

static bool runNetshRouteDelete(int ifIndex) {
    QProcess proc;
    proc.setProgram("netsh.exe");
    proc.setArguments({"interface", "ipv4", "delete", "route", "prefix=0.0.0.0/0",
                       QString("interface=%1").arg(ifIndex), "nexthop=0.0.0.0", "store=active"});
    proc.start();
    proc.waitForFinished(5000);
    return proc.exitCode() == 0;
}

static bool trafficWouldUseInterface(DWORD ifIndex) {
    DWORD bestIf = 0;
    DWORD bestMetric = 0;
    if (bestDefaultRouteIf(&bestIf, &bestMetric) && bestIf == ifIndex) {
        logDiag(QString("default route table -> ifIndex=%1 metric=%2").arg(bestIf).arg(bestMetric));
        return true;
    }
    in_addr dst{};
    inet_pton(AF_INET, "1.1.1.1", &dst);
    MIB_IPFORWARDROW best{};
    if (GetBestRoute(dst.S_un.S_addr, 0, &best) != NO_ERROR)
        return defaultRouteOnInterface(ifIndex);
    logDiag(QString("GetBestRoute(1.1.1.1) -> ifIndex=%1 metric=%2")
                .arg(best.dwForwardIfIndex)
                .arg(best.dwForwardMetric1));
    return best.dwForwardIfIndex == ifIndex;
}

static void restorePhysicalDefaultRoutes() {
    const PMIB_IPFORWARDTABLE table = fetchForwardTable();
    if (!table)
        return;

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        MIB_IPFORWARDROW row = table->table[i];
        if (row.dwForwardDest != 0 || row.dwForwardMask != 0)
            continue;
        if (isWintunIfIndex(row.dwForwardIfIndex))
            continue;
        if (row.dwForwardMetric1 < 5000)
            continue;
        row.dwForwardMetric1 = interfaceRouteMetric(row.dwForwardIfIndex, 256);
        if (SetIpForwardEntry(&row) != NO_ERROR) {
            DeleteIpForwardEntry(&row);
            CreateIpForwardEntry(&row);
        }
        logDiag(QString("restored default route metric on if=%1 to %2")
                    .arg(row.dwForwardIfIndex)
                    .arg(row.dwForwardMetric1));
    }

    freeForwardTable(table);
}

static void setTunAdapterDns(const QString& tunName) {
    {
        QProcess proc;
        proc.setProgram("netsh.exe");
        proc.setArguments({"interface", "ipv4", "set", "dnsservers",
                           QString("name=%1").arg(tunName), "source=static", "address=1.1.1.1", "register=none"});
        proc.start();
        proc.waitForFinished(5000);
        logDiag(QString("netsh dns tun=%1 primary exit=%2").arg(tunName).arg(proc.exitCode()));
    }
    {
        QProcess proc;
        proc.setProgram("netsh.exe");
        proc.setArguments({"interface", "ipv4", "add", "dnsservers",
                           QString("name=%1").arg(tunName), "8.8.8.8", "index=2"});
        proc.start();
        proc.waitForFinished(5000);
        logDiag(QString("netsh dns tun=%1 secondary exit=%2").arg(tunName).arg(proc.exitCode()));
    }
}

static void bumpPhysicalDefaultRoutes(DWORD tunIfIndex) {
    const PMIB_IPFORWARDTABLE table = fetchForwardTable();
    if (!table)
        return;

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        MIB_IPFORWARDROW row = table->table[i];
        if (row.dwForwardDest != 0 || row.dwForwardMask != 0)
            continue;
        if (row.dwForwardIfIndex == tunIfIndex)
            continue;
        if (isWintunIfIndex(row.dwForwardIfIndex))
            continue;
        if (row.dwForwardMetric1 >= 5000)
            continue;
        row.dwForwardMetric1 = 5000;
        if (SetIpForwardEntry(&row) != NO_ERROR) {
            DeleteIpForwardEntry(&row);
            CreateIpForwardEntry(&row);
        }
        logDiag(QString("raised default route metric on if=%1 to 5000").arg(row.dwForwardIfIndex));
    }

    freeForwardTable(table);
}

static BypassRoute effectiveBypass(const QString& bypassHost, const BypassRoute& preResolved, DWORD tunIf) {
    if (preResolved.valid)
        return preResolved;

    BypassRoute result;
    if (bypassHost.isEmpty())
        return result;

    MIB_IPFORWARDROW row{};
    if (!bestPhysicalRoute(bypassHost, tunIf, &row))
        return result;

    result.ifIndex = row.dwForwardIfIndex;
    result.nextHop = row.dwForwardNextHop;
    result.valid = true;
    return result;
}

bool TunWindows::ensureProxyBypass(const QString& bypassHost, const BypassRoute& preResolvedBypass,
                                   const QString& activeTunName) {
    if (bypassHost.isEmpty())
        return true;
    DWORD tunIf = 0;
    if (!activeTunName.isEmpty())
        tunIf = static_cast<DWORD>(tunInterfaceIndex(activeTunName));
    const BypassRoute bypass =
        preResolvedBypass.valid ? preResolvedBypass : resolveBypassRoute(bypassHost);
    return ensureBypassRoute(bypassHost, bypass, tunIf);
}

static bool bypassLooksOk(const QString& bypassHost, const BypassRoute& bypass, DWORD tunIf) {
    if (bypassHost.isEmpty())
        return true;
    if (!bypass.valid)
        return false;
    if (bypass.ifIndex == tunIf)
        return false;
    const DWORD host = ipv4ToHost(bypassHost);
    return hostRouteOnInterface(host, bypass.ifIndex);
}

bool TunWindows::ensureRoutes(const QString& tunName, const QString& bypassHost,
                              const BypassRoute& preResolvedBypass, bool diagnostic) {
    const int tunIf = tunInterfaceIndex(tunName);
    if (tunIf <= 0) {
        logDiag(QString("TUN adapter not found (wanted %1)").arg(tunName));
        if (diagnostic)
            logNetworkAdapters();
        return false;
    }

    logDiag(QString("TUN ifIndex=%1 for %2").arg(tunIf).arg(tunName));

    const BypassRoute bypass = effectiveBypass(bypassHost, preResolvedBypass, static_cast<DWORD>(tunIf));
    const bool bypassOk = bypassLooksOk(bypassHost, bypass, static_cast<DWORD>(tunIf));
    const bool viaTun = trafficWouldUseInterface(static_cast<DWORD>(tunIf));
    const bool hasDefault = defaultRouteOnInterface(static_cast<DWORD>(tunIf));

    if (g_sessionRoutesReady && g_sessionTun == tunName && viaTun && bypassOk && hasDefault) {
        logDiag(QString("ensureRoutes tun=%1 if=%2 cached ok").arg(tunName).arg(tunIf));
        return true;
    }

    if (hasDefault && viaTun && bypassOk) {
        logDiag(QString("ensureRoutes tun=%1 if=%2 already ok").arg(tunName).arg(tunIf));
        g_sessionRoutesReady = true;
        g_sessionTun = tunName;
        return true;
    }

    if (bypass.valid) {
        logDiag(QString("proxy bypass %1 via if=%2 gw=%3%4")
                    .arg(bypassHost)
                    .arg(bypass.ifIndex)
                    .arg(formatIpv4(bypass.nextHop))
                    .arg(preResolvedBypass.valid ? " (pre-resolved)" : ""));
    } else if (!bypassHost.isEmpty()) {
        logDiag(QString("proxy bypass %1 unresolved").arg(bypassHost));
    }

    const bool bypassApplied = ensureBypassRoute(bypassHost, bypass, static_cast<DWORD>(tunIf));

    bool ok = hasDefault;
    if (!ok)
        ok = runNetshRouteAdd(tunIf);
    if (!ok)
        ok = createForwardRow(0, 0, 0, static_cast<DWORD>(tunIf), 1);
    if (!ok && defaultRouteOnInterface(static_cast<DWORD>(tunIf)))
        ok = true;

    if (!g_physicalBumped) {
        bumpPhysicalDefaultRoutes(static_cast<DWORD>(tunIf));
        g_physicalBumped = true;
    }
    if (!g_sessionRoutesReady)
        optimizeTunDefaultRoute(static_cast<DWORD>(tunIf));
    if (!g_tunDnsSet) {
        setTunAdapterDns(tunName);
        g_tunDnsSet = true;
    }

    const bool viaTunNow = trafficWouldUseInterface(static_cast<DWORD>(tunIf));
    const bool bypassOkNow = bypassLooksOk(bypassHost, bypass, static_cast<DWORD>(tunIf));
    logDiag(QString("ensureRoutes tun=%1 if=%2 ok=%3 viaTun=%4 bypassOk=%5")
                .arg(tunName)
                .arg(tunIf)
                .arg(ok)
                .arg(viaTunNow)
                .arg(bypassOkNow && bypassApplied));
    const bool success = ok && viaTunNow && bypassOkNow && bypassApplied;
    if (success) {
        g_sessionRoutesReady = true;
        g_sessionTun = tunName;
    }
    return success;
}

void TunWindows::removeRoutes(const QString& tunName, const QString& bypassHost,
                              const BypassRoute& preResolvedBypass) {
    g_sessionRoutesReady = false;
    g_sessionTun.clear();
    g_physicalBumped = false;
    g_tunDnsSet = false;
    const int tunIf = tunInterfaceIndex(tunName);
    if (tunIf > 0) {
        runNetshRouteDelete(tunIf);
        deleteForwardRow(0, 0, 0, static_cast<DWORD>(tunIf));
    }

    const BypassRoute bypass = effectiveBypass(bypassHost, preResolvedBypass, static_cast<DWORD>(tunIf));
    if (bypass.valid && bypass.ifIndex != static_cast<quint32>(tunIf)) {
        runNetshBypassRouteDelete(bypassHost, static_cast<int>(bypass.ifIndex), bypass.nextHop);
        const DWORD host = ipv4ToHost(bypassHost);
        deleteForwardRow(host, 0xFFFFFFFF, bypass.nextHop, bypass.ifIndex);
    }
    restorePhysicalDefaultRoutes();
    logDiag(QString("removeRoutes tun=%1").arg(tunName));
}

} // namespace vicvpn
