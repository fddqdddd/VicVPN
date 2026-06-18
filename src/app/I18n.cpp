#include "vicvpn/app/I18n.h"

namespace vicvpn {

I18n& I18n::instance() {
    static I18n i;
    return i;
}

I18n::I18n() {
    loadRu();
    loadEn();
}

void I18n::setLang(Lang lang) {
    lang_ = lang;
}

QString I18n::langCode() const {
    return lang_ == Lang::En ? "en" : "ru";
}

QString I18n::tr(const char* key) const {
    const QString k = QString::fromUtf8(key);
    if (lang_ == Lang::En && en_.contains(k))
        return en_.value(k);
    return ru_.value(k, k);
}

void I18n::loadRu() {
    ru_["app.title"] = "VicVPN";
    ru_["btn.connect"] = "Подключить";
    ru_["btn.disconnect"] = "Отключить";
    ru_["btn.import"] = "Добавить";
    ru_["btn.settings"] = "Настройки";
    ru_["btn.refresh"] = "Обновить подписки";
    ru_["btn.ping"] = "Тест задержки";
    ru_["status.disconnected"] = "Отключено";
    ru_["status.connecting"] = "Подключение…";
    ru_["status.connected"] = "Подключено";
    ru_["status.error"] = "Ошибка";
    ru_["label.servers"] = "Серверы";
    ru_["label.ip"] = "IP:";
    ru_["label.ip_local"] = "Ваш IP:";
    ru_["label.ip_vpn"] = "IP VPN:";
    ru_["import.title"] = "Добавить конфигурацию";
    ru_["import.clipboard"] = "Из буфера";
    ru_["import.url"] = "Подписка / ссылка";
    ru_["import.manual"] = "Вручную";
    ru_["import.ok"] = "Импорт";
    ru_["import.cancel"] = "Отмена";
    ru_["import.country"] = "Страна (ssconf)";
    ru_["import.change_country"] = "Сменить страну";
    ru_["settings.title"] = "Настройки";
    ru_["settings.lang"] = "Язык";
    ru_["settings.bypass_lan"] = "Обход локальной сети";
    ru_["settings.fast_tunnel"] = "Быстрый режим";
    ru_["settings.use_legacy_core"] = "Старый движок (Xray, не рекомендуется)";
    ru_["error.no_singbox"] = "sing-box.exe не найден. Пересоберите проект (build-mingw.bat).";
    ru_["settings.remote_dns"] = "Удалённый DNS (Xray)";
    ru_["settings.kill_switch"] = "Kill switch";
    ru_["settings.block_ipv6"] = "Блокировать IPv6";
    ru_["settings.dpi"] = "DPI (fragment)";
    ru_["settings.mtu"] = "MTU";
    ru_["settings.autostart"] = "Автозапуск с Windows";
    ru_["label.session"] = "Сессия:";
    ru_["admin.required"] = "Нужен запуск от администратора (UAC). Перезапустить с повышением прав?";
    ru_["error.no_server"] = "Выберите сервер";
    ru_["error.no_core"] = "Ядро не найдено. Запустите tools/fetch-core.ps1";
    ru_["error.no_tun2socks"] = "tun2socks.exe не найден. Пересоберите проект (build-mingw.bat).";
    ru_["error.no_wintun"] = "Не удалось установить wintun.dll в папку core. Пересоберите проект (build-mingw.bat).";
    ru_["error.core_failed"] = "Ядро остановилось:";
    ru_["error.core_start_failed"] = "Ядро не запустилось:";
    ru_["error.core_config"] = "Ошибка конфигурации Xray:";
    ru_["server.delete"] = "Удалить";
    ru_["server.delete_confirm"] = "Удалить сервер «%1»?";
    ru_["error.tun_routes"] = "Ошибка TUN (очистите старые адаптеры в диспетчере устройств):";
    ru_["error.tun_routes_no_traffic"] = "VPN подключён, но трафик не идёт через TUN. Смотрите runtime\\core.log";
    ru_["error.server_unreachable"] = "VPN-сервер недоступен с вашей сети. Проверьте подписку у провайдера или смените страну в ssconf.";
    ru_["error.ssconf_fetch"] = "Не удалось загрузить ssconf. Проверьте ссылку или выберите другую страну.";
    ru_["tray.show"] = "Показать";
    ru_["tray.quit"] = "Выход";
    ru_["about.title"] = "О программе";
    ru_["about.version"] = "Версия";
    ru_["about.description"] = "VPN/прокси-клиент (beta). Ядра: sing-box, Xray, Hysteria2. TUN full tunnel.";
    ru_["about.license"] = "Лицензия: GPL-3.0";
    ru_["settings.minimize_tray"] = "Сворачивать в трей";
    ru_["tray.minimized"] = "Приложение свёрнуто в трей";
}

void I18n::loadEn() {
    en_["app.title"] = "VicVPN";
    en_["btn.connect"] = "Connect";
    en_["btn.disconnect"] = "Disconnect";
    en_["btn.import"] = "Add";
    en_["btn.settings"] = "Settings";
    en_["btn.refresh"] = "Refresh subscriptions";
    en_["btn.ping"] = "Latency test";
    en_["status.disconnected"] = "Disconnected";
    en_["status.connecting"] = "Connecting…";
    en_["status.connected"] = "Connected";
    en_["status.error"] = "Error";
    en_["label.servers"] = "Servers";
    en_["label.ip"] = "IP:";
    en_["label.ip_local"] = "Your IP:";
    en_["label.ip_vpn"] = "VPN IP:";
    en_["import.title"] = "Add configuration";
    en_["import.clipboard"] = "From clipboard";
    en_["import.url"] = "Subscription / URL";
    en_["import.manual"] = "Manual";
    en_["import.ok"] = "Import";
    en_["import.cancel"] = "Cancel";
    en_["import.country"] = "Country (ssconf)";
    en_["import.change_country"] = "Change country";
    en_["settings.title"] = "Settings";
    en_["settings.lang"] = "Language";
    en_["settings.bypass_lan"] = "Bypass LAN";
    en_["settings.fast_tunnel"] = "Fast mode";
    en_["settings.use_legacy_core"] = "Legacy engine (Xray, not recommended)";
    en_["error.no_singbox"] = "sing-box.exe not found. Rebuild the project (build-mingw.bat).";
    en_["settings.remote_dns"] = "Remote DNS (Xray)";
    en_["settings.kill_switch"] = "Kill switch";
    en_["settings.block_ipv6"] = "Block IPv6";
    en_["settings.dpi"] = "DPI (fragment)";
    en_["settings.mtu"] = "MTU";
    en_["settings.autostart"] = "Start with Windows";
    en_["label.session"] = "Session:";
    en_["admin.required"] = "Administrator elevation (UAC) required. Relaunch elevated?";
    en_["error.no_server"] = "Select a server";
    en_["error.no_core"] = "Core not found. Run tools/fetch-core.ps1";
    en_["error.no_tun2socks"] = "tun2socks.exe not found. Rebuild the project (build-mingw.bat).";
    en_["error.no_wintun"] = "Failed to install wintun.dll into core/. Rebuild the project (build-mingw.bat).";
    en_["error.core_failed"] = "Core stopped:";
    en_["error.core_start_failed"] = "Core failed to start:";
    en_["error.core_config"] = "Xray config error:";
    en_["server.delete"] = "Delete";
    en_["server.delete_confirm"] = "Delete server \"%1\"?";
    en_["error.tun_routes"] = "TUN error (remove stale adapters in Device Manager):";
    en_["error.tun_routes_no_traffic"] = "VPN connected but traffic bypasses TUN. See runtime\\core.log";
    en_["error.server_unreachable"] = "VPN server is unreachable. Check the key or try another server.";
    en_["error.ssconf_fetch"] = "Failed to load ssconf. Check the link or try another country.";
    en_["tray.show"] = "Show";
    en_["tray.quit"] = "Exit";
    en_["about.title"] = "About";
    en_["about.version"] = "Version";
    en_["about.description"] = "VPN/proxy client (beta). Cores: sing-box, Xray, Hysteria2. TUN full tunnel.";
    en_["about.license"] = "License: GPL-3.0";
    en_["settings.minimize_tray"] = "Minimize to tray";
    en_["tray.minimized"] = "Application minimized to tray";
}

} // namespace vicvpn
