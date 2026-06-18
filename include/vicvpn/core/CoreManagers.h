#pragma once

#include <QObject>
#include <QString>

class QProcess;

namespace vicvpn {

class XrayCoreManager : public QObject {
    Q_OBJECT
public:
    explicit XrayCoreManager(QObject* parent = nullptr);
    bool start(const QString& configPath);
    void stop();
    bool isRunning() const;
    QString lastError() const { return lastError_; }
    QString recentLog() const { return recentLog_; }

signals:
    void started();
    void stopped(int exitCode);
    void logLine(const QString& line);

private:
    QProcess* proc_ = nullptr;
    QString lastError_;
    QString recentLog_;
    quint64 generation_ = 0;
    quint64 aliveRun_ = 0;
};

class HysteriaCoreManager : public QObject {
    Q_OBJECT
public:
    explicit HysteriaCoreManager(QObject* parent = nullptr);
    bool start(const QString& configPath);
    void stop();
    bool isRunning() const;
    QString lastError() const { return lastError_; }
    QString recentLog() const { return recentLog_; }

signals:
    void started();
    void stopped(int exitCode);
    void logLine(const QString& line);

private:
    QProcess* proc_ = nullptr;
    QString lastError_;
    QString recentLog_;
    quint64 generation_ = 0;
    quint64 aliveRun_ = 0;
};

/** Primary engine: sing-box TUN (Outline-style, fast, stealth SS/VLESS). */
class SingboxCoreManager : public QObject {
    Q_OBJECT
public:
    explicit SingboxCoreManager(QObject* parent = nullptr);
    bool start(const QString& configPath);
    void stop();
    bool isRunning() const;
    QString lastError() const { return lastError_; }
    QString recentLog() const { return recentLog_; }

signals:
    void started();
    void stopped(int exitCode);
    void logLine(const QString& line);

private:
    QProcess* proc_ = nullptr;
    QString lastError_;
    QString recentLog_;
    quint64 generation_ = 0;
    quint64 aliveRun_ = 0;
};

/** Legacy: tun2socks + Xray SOCKS. */
class Tun2socksManager : public QObject {
    Q_OBJECT
public:
    explicit Tun2socksManager(QObject* parent = nullptr);
    bool start(const QString& proxyUrl, const QString& tunName, const QString& physicalInterface,
               int mtu = 1500);
    void stop();
    bool isRunning() const;
    QString lastError() const { return lastError_; }
    QString recentLog() const { return recentLog_; }

signals:
    void started();
    void stopped(int exitCode);
    void logLine(const QString& line);

private:
    QProcess* proc_ = nullptr;
    QString lastError_;
    QString recentLog_;
    quint64 generation_ = 0;
    quint64 aliveRun_ = 0;
};

} // namespace vicvpn
