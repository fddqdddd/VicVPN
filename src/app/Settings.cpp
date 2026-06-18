#include "vicvpn/app/Settings.h"
#include "vicvpn/app/I18n.h"
#include "vicvpn/util/AppPaths.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace vicvpn {

Settings& Settings::instance() {
    static Settings s;
    return s;
}

QString Settings::path() const {
    return AppPaths::dataDir() + "/settings.json";
}

void Settings::load() {
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    const auto o = doc.object();
    s_.bypassLan = o.value("bypassLan").toBool(true);
    s_.fastTunnel = o.value("fastTunnel").toBool(true);
    s_.useLegacyCore = o.value("useLegacyCore").toBool(o.value("useTun2socks").toBool(false));
    s_.remoteDns = o.value("remoteDns").toBool(false);
    s_.killSwitch = o.value("killSwitch").toBool(true);
    s_.blockIpv6 = o.value("blockIpv6").toBool(false);
    s_.fragmentDpi = o.value("fragmentDpi").toBool(false);
    s_.autostart = o.value("autostart").toBool(false);
    s_.minimizeToTray = o.value("minimizeToTray").toBool(true);
    s_.mtu = o.value("mtu").toInt(1500);
    s_.subscriptionIntervalHours = o.value("subscriptionIntervalHours").toInt(24);
    s_.language = o.value("language").toString("ru");
    I18n::instance().setLang(s_.language == "en" ? I18n::Lang::En : I18n::Lang::Ru);
}

void Settings::save() {
    QJsonObject o;
    o["bypassLan"] = s_.bypassLan;
    o["fastTunnel"] = s_.fastTunnel;
    o["useLegacyCore"] = s_.useLegacyCore;
    o["remoteDns"] = s_.remoteDns;
    o["killSwitch"] = s_.killSwitch;
    o["blockIpv6"] = s_.blockIpv6;
    o["fragmentDpi"] = s_.fragmentDpi;
    o["autostart"] = s_.autostart;
    o["minimizeToTray"] = s_.minimizeToTray;
    o["mtu"] = s_.mtu;
    o["subscriptionIntervalHours"] = s_.subscriptionIntervalHours;
    o["language"] = s_.language;
    QFile f(path());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(o).toJson());
}

void Settings::set(const AppSettings& s) {
    s_ = s;
    I18n::instance().setLang(s_.language == "en" ? I18n::Lang::En : I18n::Lang::Ru);
    save();
    emit changed();
}

} // namespace vicvpn
