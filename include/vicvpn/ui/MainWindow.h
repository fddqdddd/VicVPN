#pragma once

#include "vicvpn/model/ServerProfile.h"
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QDateTime>
#include <QCloseEvent>

class QListWidget;
class QPushButton;
class QLabel;
class QMenu;
class QTimer;

namespace vicvpn {

class Database;
class ConnectionManager;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onImport();
    void onSettings();
    void onAbout();
    void onConnectToggle();
    void onServerSelected();
    void onRefreshSubscriptions();
    void onTestLatency();
    void onChangeCountry();
    void onServerContextMenu(const QPoint& pos);
    void updateSessionTimer();
    void refreshServerList();
    void onConnectionStateChanged(int state, const QString& text);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void setupUi();
    void applyTheme();
    void updateConnectButton();
    void updateStatusBar();
    void fetchDisplayIp(bool vpnConnected, int attempt = 0);
    ServerProfile currentServer() const;

    Database* db_ = nullptr;
    ConnectionManager* conn_ = nullptr;
    QListWidget* serverList_ = nullptr;
    QPushButton* connectBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* ipLabel_ = nullptr;
    QLabel* sessionLabel_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QMenu* trayMenu_ = nullptr;
    QTimer* sessionTimer_ = nullptr;
    QDateTime sessionStart_;
};

} // namespace vicvpn
