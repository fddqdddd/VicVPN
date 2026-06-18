#pragma once

#include <QString>
#include <QObject>

namespace vicvpn {

struct AppSettings {
    bool bypassLan = true;
    /** Optimized Xray TUN config (buffers, static LAN rules, no extra DNS). */
    bool fastTunnel = true;
    /** Experimental legacy stack (Xray TUN / tun2socks). */
    bool useLegacyCore = false;
    bool remoteDns = false;
    bool killSwitch = true;
    bool blockIpv6 = false;
    bool fragmentDpi = false;
    bool autostart = false;
    bool minimizeToTray = true;
    QString language = "ru";
    int mtu = 1500;
    int subscriptionIntervalHours = 24;
};

class Settings : public QObject {
    Q_OBJECT
public:
    static Settings& instance();
    const AppSettings& get() const { return s_; }
    void set(const AppSettings& s);
    void load();
    void save();

signals:
    void changed();

private:
    Settings() = default;
    QString path() const;
    AppSettings s_;
};

} // namespace vicvpn
