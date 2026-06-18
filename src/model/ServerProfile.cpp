#include "vicvpn/model/ServerProfile.h"

namespace vicvpn {

QString ServerProfile::protocolLabel() const {
    switch (protocol) {
    case Protocol::Vless: return "VLESS";
    case Protocol::Vmess: return "VMess";
    case Protocol::Shadowsocks: return "SS";
    case Protocol::Trojan: return "Trojan";
    case Protocol::Socks: return "Socks";
    case Protocol::Hysteria2: return "HY2";
    default: return "Unknown";
    }
}

QString ServerProfile::coreLabel() const {
    return core == CoreType::Hysteria2 ? "Hysteria2" : "Xray";
}

} // namespace vicvpn
