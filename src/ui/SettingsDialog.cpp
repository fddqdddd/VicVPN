#include "vicvpn/ui/SettingsDialog.h"
#include "vicvpn/app/I18n.h"
#include "vicvpn/app/Settings.h"
#include "vicvpn/util/PlatformAutostart.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

namespace vicvpn {

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(VTR("settings.title"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    auto* lang = new QComboBox;
    lang->addItem("Русский", "ru");
    lang->addItem("English", "en");
    const auto& s = Settings::instance().get();
    lang->setCurrentIndex(s.language == "en" ? 1 : 0);

    auto* bypassLan = new QCheckBox;
    bypassLan->setChecked(s.bypassLan);
    auto* fastTunnel = new QCheckBox;
    fastTunnel->setChecked(s.fastTunnel);
    auto* useLegacy = new QCheckBox;
    useLegacy->setChecked(s.useLegacyCore);
    auto* remoteDns = new QCheckBox;
    remoteDns->setChecked(s.remoteDns);
    auto* killSwitch = new QCheckBox;
    killSwitch->setChecked(s.killSwitch);
    auto* blockIpv6 = new QCheckBox;
    blockIpv6->setChecked(s.blockIpv6);
    auto* dpi = new QCheckBox;
    dpi->setChecked(s.fragmentDpi);
    auto* autostart = new QCheckBox;
    autostart->setChecked(s.autostart);
    auto* minimizeTray = new QCheckBox;
    minimizeTray->setChecked(s.minimizeToTray);
    auto* mtu = new QSpinBox;
    mtu->setRange(1280, 9000);
    mtu->setValue(s.mtu);

    form->addRow(VTR("settings.lang"), lang);
    form->addRow(VTR("settings.bypass_lan"), bypassLan);
    form->addRow(VTR("settings.fast_tunnel"), fastTunnel);
    form->addRow(VTR("settings.use_legacy_core"), useLegacy);
    form->addRow(VTR("settings.remote_dns"), remoteDns);
    form->addRow(VTR("settings.kill_switch"), killSwitch);
    form->addRow(VTR("settings.block_ipv6"), blockIpv6);
    form->addRow(VTR("settings.dpi"), dpi);
    form->addRow(VTR("settings.autostart"), autostart);
    form->addRow(VTR("settings.minimize_tray"), minimizeTray);
    form->addRow(VTR("settings.mtu"), mtu);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, lang, bypassLan, fastTunnel, useLegacy, remoteDns, killSwitch, blockIpv6, dpi, autostart, minimizeTray, mtu]() {
        AppSettings ns = Settings::instance().get();
        ns.language = lang->currentData().toString();
        ns.bypassLan = bypassLan->isChecked();
        ns.fastTunnel = fastTunnel->isChecked();
        ns.useLegacyCore = useLegacy->isChecked();
        ns.remoteDns = remoteDns->isChecked();
        ns.killSwitch = killSwitch->isChecked();
        ns.blockIpv6 = blockIpv6->isChecked();
        ns.fragmentDpi = dpi->isChecked();
        ns.autostart = autostart->isChecked();
        ns.minimizeToTray = minimizeTray->isChecked();
        ns.mtu = mtu->value();
        PlatformAutostart::setEnabled(ns.autostart, QApplication::applicationFilePath());
        Settings::instance().set(ns);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace vicvpn
