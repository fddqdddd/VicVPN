#include "vicvpn/ui/MainWindow.h"
#include "vicvpn/ui/ImportDialog.h"
#include "vicvpn/ui/SettingsDialog.h"
#include "vicvpn/ui/AboutDialog.h"
#include "vicvpn/storage/Database.h"
#include "vicvpn/core/ConnectionManager.h"
#include "vicvpn/parser/ImportService.h"
#include "vicvpn/parser/SsconfCountry.h"
#include "vicvpn/app/I18n.h"
#include "vicvpn/app/Settings.h"
#include "vicvpn/util/AdminElevation.h"
#include <QApplication>
#include <QCloseEvent>
#include <QIcon>
#include <QDateTime>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPushButton>
#include <QSet>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <string>

namespace vicvpn {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    db_ = new Database(this);
    db_->open();
    conn_ = new ConnectionManager(this);
    setupUi();
    applyTheme();
    refreshServerList();

    connect(conn_, &ConnectionManager::stateChanged, this, &MainWindow::onConnectionStateChanged);
    connect(&Settings::instance(), &Settings::changed, this, [this]() {
        applyTheme();
        refreshServerList();
    });

    const qint64 last = db_->lastServerId();
    if (last > 0) {
        for (int i = 0; i < serverList_->count(); ++i) {
            if (serverList_->item(i)->data(Qt::UserRole).toLongLong() == last) {
                serverList_->setCurrentRow(i);
                break;
            }
        }
    }

    if (!isRunningAsAdmin()) {
        QTimer::singleShot(500, this, []() {
            if (QMessageBox::question(nullptr, VTR("app.title"), VTR("admin.required")) == QMessageBox::Yes)
                requestAdminRelaunch();
        });
    }

    QTimer::singleShot(0, this, [this]() { fetchDisplayIp(false); });
}

void MainWindow::setupUi() {
    setWindowTitle(VTR("app.title"));
    setWindowIcon(QIcon(":/icons/vicvpn.png"));
    setMinimumSize(420, 640);
    resize(440, 700);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* top = new QHBoxLayout;
    auto* title = new QLabel(VTR("app.title"));
    title->setObjectName("appTitle");
    top->addWidget(title);
    top->addStretch();
    auto* aboutBtn = new QPushButton("?");
    aboutBtn->setFixedSize(36, 36);
    aboutBtn->setToolTip(VTR("about.title"));
    connect(aboutBtn, &QPushButton::clicked, this, &MainWindow::onAbout);
    top->addWidget(aboutBtn);
    auto* addBtn = new QPushButton("+");
    addBtn->setObjectName("addBtn");
    addBtn->setFixedSize(36, 36);
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::onImport);
    top->addWidget(addBtn);
    layout->addLayout(top);

    auto* serversLbl = new QLabel(VTR("label.servers"));
    serversLbl->setObjectName("sectionLabel");
    layout->addWidget(serversLbl);

    serverList_ = new QListWidget;
    serverList_->setObjectName("serverList");
    serverList_->setSpacing(4);
    serverList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(serverList_, &QListWidget::currentRowChanged, this, &MainWindow::onServerSelected);
    connect(serverList_, &QListWidget::customContextMenuRequested, this, &MainWindow::onServerContextMenu);
    layout->addWidget(serverList_, 1);

    connectBtn_ = new QPushButton(VTR("btn.connect"));
    connectBtn_->setObjectName("connectBtn");
    connectBtn_->setMinimumHeight(56);
    connect(connectBtn_, &QPushButton::clicked, this, &MainWindow::onConnectToggle);
    layout->addWidget(connectBtn_);

    statusLabel_ = new QLabel(VTR("status.disconnected"));
    statusLabel_->setObjectName("statusLabel");
    statusLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(statusLabel_);

    ipLabel_ = new QLabel(VTR("label.ip_local") + " …");
    ipLabel_->setObjectName("ipLabel");
    ipLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(ipLabel_);

    sessionLabel_ = new QLabel(VTR("label.session") + " —");
    sessionLabel_->setObjectName("sessionLabel");
    sessionLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(sessionLabel_);

    sessionTimer_ = new QTimer(this);
    sessionTimer_->setInterval(1000);
    connect(sessionTimer_, &QTimer::timeout, this, &MainWindow::updateSessionTimer);

    auto* bottom = new QHBoxLayout;
    auto* refreshBtn = new QPushButton(VTR("btn.refresh"));
    auto* pingBtn = new QPushButton(VTR("btn.ping"));
    auto* settingsBtn = new QPushButton(VTR("btn.settings"));
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshSubscriptions);
    connect(pingBtn, &QPushButton::clicked, this, &MainWindow::onTestLatency);
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::onSettings);
    bottom->addWidget(refreshBtn);
    bottom->addWidget(pingBtn);
    bottom->addWidget(settingsBtn);
    layout->addLayout(bottom);

    setCentralWidget(central);

    trayMenu_ = new QMenu(this);
    trayMenu_->addAction(VTR("tray.show"), this, &QWidget::show);
    trayMenu_->addAction(VTR("btn.disconnect"), this, &MainWindow::onConnectToggle);
    trayMenu_->addSeparator();
    trayMenu_->addAction(VTR("tray.quit"), qApp, &QApplication::quit);

    tray_ = new QSystemTrayIcon(this);
    tray_->setIcon(QIcon(":/icons/vicvpn.png"));
    tray_->setContextMenu(trayMenu_);
    tray_->setToolTip(VTR("app.title"));
    connect(tray_, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    tray_->show();
}

void MainWindow::applyTheme() {
    QFile f(":/styles/dark.qss");
    if (!f.open(QIODevice::ReadOnly)) {
        f.setFileName(QApplication::applicationDirPath() + "/styles/dark.qss");
        f.open(QIODevice::ReadOnly);
    }
    if (f.isOpen())
        qApp->setStyleSheet(QString::fromUtf8(f.readAll()));
}

void MainWindow::refreshServerList() {
    const qint64 cur = currentServer().id;
    serverList_->clear();
    for (const auto& s : db_->allServers()) {
        QString title = s.name;
        if (!s.countryCode.isEmpty() && !title.contains(SsconfCountry::displayName(s.countryCode))) {
            const QString flag = SsconfCountry::flagEmoji(s.countryCode);
            title = flag.isEmpty() ? SsconfCountry::displayName(s.countryCode) : (flag + " " + title);
        }
        auto* item = new QListWidgetItem(
            QString("%1  [%2]%3")
                .arg(title)
                .arg(s.protocolLabel())
                .arg(s.latencyMs >= 0 ? QString("  %1 ms").arg(s.latencyMs) : ""));
        item->setData(Qt::UserRole, s.id);
        serverList_->addItem(item);
        if (s.id == cur)
            serverList_->setCurrentItem(item);
    }
    updateConnectButton();
}

ServerProfile MainWindow::currentServer() const {
    auto* item = serverList_->currentItem();
    if (!item) return {};
    const qint64 id = item->data(Qt::UserRole).toLongLong();
    if (auto s = db_->serverById(id)) return *s;
    return {};
}

void MainWindow::updateConnectButton() {
    const bool connected = conn_->state() == ConnectionState::Connected;
    connectBtn_->setText(connected ? VTR("btn.disconnect") : VTR("btn.connect"));
    connectBtn_->setProperty("connected", connected);
    connectBtn_->style()->unpolish(connectBtn_);
    connectBtn_->style()->polish(connectBtn_);
}

void MainWindow::fetchDisplayIp(bool vpnConnected, int attempt) {
    const QString prefix = vpnConnected ? VTR("label.ip_vpn") : VTR("label.ip_local");
    ipLabel_->setText(prefix + " …");
    auto* nam = new QNetworkAccessManager(this);
    nam->setProxy(QNetworkProxy::NoProxy);
    const QUrl url = attempt > 0 ? QUrl("http://api.ipify.org") : QUrl("https://api.ipify.org");
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(15000);
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, prefix, vpnConnected, attempt]() {
        if (conn_->state() == ConnectionState::Connected != vpnConnected) {
            reply->deleteLater();
            nam->deleteLater();
            return;
        }
        if (reply->error() == QNetworkReply::NoError) {
            ipLabel_->setText(prefix + " " + QString::fromUtf8(reply->readAll()).trimmed());
        } else if (attempt < 2) {
            QTimer::singleShot(3000, this, [this, vpnConnected, attempt]() {
                fetchDisplayIp(vpnConnected, attempt + 1);
            });
        } else {
            ipLabel_->setText(prefix + " …");
        }
        reply->deleteLater();
        nam->deleteLater();
    });
}

void MainWindow::updateStatusBar() {
    statusLabel_->setText(conn_->statusText());
}

void MainWindow::onImport() {
    ImportDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    for (const auto& s : dlg.imported()) {
        db_->insertServer(s);
    }
    refreshServerList();
}

void MainWindow::onSettings() {
    SettingsDialog dlg(this);
    dlg.exec();
}

void MainWindow::onAbout() {
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (Settings::instance().get().minimizeToTray && tray_->isVisible()) {
        hide();
        tray_->showMessage(VTR("app.title"), VTR("tray.minimized"));
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::onConnectToggle() {
    if (conn_->state() == ConnectionState::Connected || conn_->state() == ConnectionState::Connecting) {
        conn_->disconnect();
        fetchDisplayIp(false);
        updateConnectButton();
        updateStatusBar();
        return;
    }
    const auto s = currentServer();
    if (s.id <= 0) {
        QMessageBox::warning(this, VTR("app.title"), VTR("error.no_server"));
        return;
    }
    if (!isRunningAsAdmin()) {
        if (QMessageBox::question(this, VTR("app.title"), VTR("admin.required")) == QMessageBox::Yes)
            requestAdminRelaunch();
        return;
    }
    db_->setLastServerId(s.id);
    conn_->connectServer(s);
    updateConnectButton();
    updateStatusBar();
}

void MainWindow::onServerSelected() {
    updateConnectButton();
}

void MainWindow::onRefreshSubscriptions() {
    int updated = 0;
    QString err;
    for (const auto& s : db_->allServers()) {
        if (s.subscriptionUrl.isEmpty()) continue;
        std::vector<ServerProfile> servers;
        if (SsconfCountry::isSsconfUri(s.subscriptionUrl))
            servers = SsconfResolver::resolve(s.subscriptionUrl, &err, s.countryCode);
        else
            servers = SubscriptionFetcher::fetch(s.subscriptionUrl, &err);
        if (servers.empty()) continue;
        auto profile = servers.front();
        profile.id = s.id;
        profile.countryCode = s.countryCode;
        if (profile.name.isEmpty())
            profile.name = s.name;
        db_->updateServer(profile);
        ++updated;
    }
    refreshServerList();
    if (updated == 0 && !err.isEmpty())
        QMessageBox::warning(this, VTR("app.title"), err);
}

void MainWindow::onTestLatency() {
    const ServerProfile server = currentServer();
    if (server.id <= 0) return;
    QString host;
    if (server.core == CoreType::Hysteria2 && server.hy2Config.contains("server"))
        host = QString::fromStdString(server.hy2Config["server"].get<std::string>()).split(':').first();
    else if (server.xrayOutbound.contains("settings")) {
        const auto& st = server.xrayOutbound["settings"];
        if (st.contains("vnext") && !st["vnext"].empty())
            host = QString::fromStdString(st["vnext"][0]["address"].get<std::string>());
        else if (st.contains("servers") && !st["servers"].empty())
            host = QString::fromStdString(st["servers"][0]["address"].get<std::string>());
    }
    if (host.isEmpty()) return;

    auto* nam = new QNetworkAccessManager(this);
    const auto start = QDateTime::currentMSecsSinceEpoch();
    QNetworkReply* reply = nam->get(QNetworkRequest(QUrl("http://" + host)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, start, server]() mutable {
        const int ms = static_cast<int>(QDateTime::currentMSecsSinceEpoch() - start);
        auto profile = server;
        profile.latencyMs = reply->error() == QNetworkReply::NoError ? ms : -1;
        db_->updateServer(profile);
        reply->deleteLater();
        nam->deleteLater();
        refreshServerList();
    });
}

void MainWindow::onConnectionStateChanged(int state, const QString& text) {
    Q_UNUSED(state);
    statusLabel_->setText(text);
    updateConnectButton();
    if (conn_->state() == ConnectionState::Connected) {
        sessionStart_ = QDateTime::currentDateTime();
        sessionTimer_->start();
        updateSessionTimer();
        QTimer::singleShot(4000, this, [this]() { fetchDisplayIp(true); });
        QTimer::singleShot(9000, this, [this]() {
            if (conn_->state() == ConnectionState::Connected)
                fetchDisplayIp(true);
        });
    } else {
        sessionTimer_->stop();
        sessionLabel_->setText(VTR("label.session") + " —");
        if (conn_->state() != ConnectionState::Connecting)
            fetchDisplayIp(false);
    }
}

void MainWindow::updateSessionTimer() {
    if (!sessionStart_.isValid()) return;
    const qint64 secs = sessionStart_.secsTo(QDateTime::currentDateTime());
    const int h = static_cast<int>(secs / 3600);
    const int m = static_cast<int>((secs % 3600) / 60);
    const int s = static_cast<int>(secs % 60);
    sessionLabel_->setText(VTR("label.session") +
                           QString(" %1:%2:%3")
                               .arg(h, 2, 10, QChar('0'))
                               .arg(m, 2, 10, QChar('0'))
                               .arg(s, 2, 10, QChar('0')));
}

void MainWindow::onServerContextMenu(const QPoint& pos) {
    auto* item = serverList_->itemAt(pos);
    if (!item)
        return;
    const qint64 id = item->data(Qt::UserRole).toLongLong();
    const auto server = db_->serverById(id);
    if (!server)
        return;

    QMenu menu(this);

    if (SsconfCountry::isSsconfUri(server->subscriptionUrl)) {
        auto* countryMenu = menu.addMenu(VTR("import.change_country"));
        for (const auto& c : SsconfCountry::all()) {
            const QString label = I18n::instance().langCode() == "en" ? c.nameEn : c.nameRu;
            auto* action = countryMenu->addAction(label);
            const QString code = c.code;
            connect(action, &QAction::triggered, this, [this, id, code]() {
                auto profile = db_->serverById(id);
                if (!profile)
                    return;
                profile->countryCode = code;
                QString err;
                auto servers = SsconfResolver::resolve(profile->subscriptionUrl, &err, profile->countryCode);
                if (servers.empty()) {
                    QMessageBox::warning(this, VTR("app.title"), err.isEmpty() ? "Parse failed" : err);
                    return;
                }
                auto updated = SsconfResolver::pickByCountry(servers, profile->countryCode);
                updated.id = profile->id;
                updated.countryCode = profile->countryCode;
                db_->updateServer(updated);
                refreshServerList();
            });
        }
        menu.addSeparator();
    }

    auto* del = menu.addAction(VTR("server.delete"));
    connect(del, &QAction::triggered, this, [this, id, name = server->name]() {
        const auto answer = QMessageBox::question(this, VTR("app.title"),
                                                  VTR("server.delete_confirm").arg(name));
        if (answer != QMessageBox::Yes)
            return;
        if (conn_->connectedServerId() == id)
            conn_->disconnect();
        db_->deleteServer(id);
        refreshServerList();
        updateConnectButton();
        updateStatusBar();
    });

    menu.exec(serverList_->viewport()->mapToGlobal(pos));
}

void MainWindow::onChangeCountry() {}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger)
        showNormal();
    else if (reason == QSystemTrayIcon::DoubleClick)
        onConnectToggle();
}

} // namespace vicvpn
