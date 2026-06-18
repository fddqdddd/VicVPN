#include "vicvpn/util/NetworkRoute.h"
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

namespace vicvpn {

static bool ensureWsa() {
    static bool ok = [] {
        WSADATA wsa{};
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    return ok;
}

static bool adapterMatchesName(const IP_ADAPTER_ADDRESSES* adapter, const QString& name) {
    if (!adapter)
        return false;
    const QString friendly = QString::fromWCharArray(adapter->FriendlyName);
    if (friendly.compare(name, Qt::CaseInsensitive) == 0)
        return true;
    if (adapter->Description) {
        const QString desc = QString::fromWCharArray(adapter->Description);
        if (desc.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

static bool adapterLooksLikeXrayTun(const IP_ADAPTER_ADDRESSES* adapter) {
    if (!adapter)
        return false;
    const QString friendly = QString::fromWCharArray(adapter->FriendlyName);
    const QString desc = adapter->Description ? QString::fromWCharArray(adapter->Description) : QString();
    if (friendly.compare("vicvpn0", Qt::CaseInsensitive) == 0 ||
        friendly.compare("xray0", Qt::CaseInsensitive) == 0)
        return true;
    return desc.contains("Xray Tunnel", Qt::CaseInsensitive) ||
           desc.contains("Wintun", Qt::CaseInsensitive);
}

static bool isPhysicalAdapterUp(const IP_ADAPTER_ADDRESSES* adapter) {
    return adapter && adapter->OperStatus == IfOperStatusUp;
}

static IP_ADAPTER_ADDRESSES* enumerateAdapters(std::vector<BYTE>& buffer) {
    ULONG size = 16 * 1024;
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

QString NetworkRoute::localIpv4ForHost(const QString& host, int port) {
    if (host.isEmpty() || !ensureWsa())
        return {};

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    const QByteArray h = host.toUtf8();
    const QByteArray p = QByteArray::number(port);
    if (getaddrinfo(h.constData(), p.constData(), &hints, &res) != 0 || !res)
        return {};

    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(res);
        return {};
    }

    QString out;
    if (connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen)) == 0) {
        sockaddr_in local{};
        int len = sizeof(local);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
            char buf[INET_ADDRSTRLEN]{};
            if (inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf)))
                out = QString::fromUtf8(buf);
        }
    }

    closesocket(s);
    freeaddrinfo(res);
    return out;
}

QString NetworkRoute::interfaceNameForLocalIpv4(const QString& localIp) {
    if (localIp.isEmpty())
        return {};

    std::vector<BYTE> buffer;
    const auto* addrs = enumerateAdapters(buffer);
    if (!addrs)
        return {};

    for (auto* adapter = addrs; adapter; adapter = adapter->Next) {
        if (!isPhysicalAdapterUp(adapter))
            continue;
        for (auto* uni = adapter->FirstUnicastAddress; uni; uni = uni->Next) {
            if (!uni->Address.lpSockaddr || uni->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            const auto* sa = reinterpret_cast<sockaddr_in*>(uni->Address.lpSockaddr);
            char buf[INET_ADDRSTRLEN]{};
            if (!inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf)))
                continue;
            if (localIp == QString::fromUtf8(buf))
                return QString::fromWCharArray(adapter->FriendlyName);
        }
    }
    return {};
}

QString NetworkRoute::interfaceNameForIfIndex(int ifIndex) {
    if (ifIndex <= 0)
        return {};

    std::vector<BYTE> buffer;
    const auto* addrs = enumerateAdapters(buffer);
    if (!addrs)
        return {};

    for (auto* adapter = addrs; adapter; adapter = adapter->Next) {
        if (static_cast<int>(adapter->IfIndex) == ifIndex)
            return QString::fromWCharArray(adapter->FriendlyName);
    }
    return {};
}

bool NetworkRoute::isTunAdapterPresent(const QString& name) {
    std::vector<BYTE> buffer;
    const auto* addrs = enumerateAdapters(buffer);
    if (!addrs)
        return false;

    for (auto* adapter = addrs; adapter; adapter = adapter->Next) {
        if (adapterMatchesName(adapter, name))
            return true;
    }
    return false;
}

bool NetworkRoute::isTunAdapterPresent() {
    std::vector<BYTE> buffer;
    const auto* addrs = enumerateAdapters(buffer);
    if (!addrs)
        return false;

    for (auto* adapter = addrs; adapter; adapter = adapter->Next) {
        if (adapterLooksLikeXrayTun(adapter))
            return true;
    }
    return false;
}

} // namespace vicvpn
