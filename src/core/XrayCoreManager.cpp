#include "vicvpn/core/CoreManagers.h"
#include "vicvpn/util/AppPaths.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

namespace vicvpn {

static void copyWintun(const QString& targetDir) {
    const QString src = AppPaths::wintunDll();
    const QString dst = targetDir + "/wintun.dll";
    if (QFileInfo::exists(src))
        QFile::remove(dst);
    if (QFileInfo::exists(src))
        QFile::copy(src, dst);
}

static bool isNoisyAccessLogLine(const QString& line) {
    return line.contains("accepted tcp:", Qt::CaseInsensitive) ||
           line.contains("accepted udp:", Qt::CaseInsensitive);
}

static void appendLog(QString* buf, const QString& line) {
    if (!buf || line.isEmpty() || isNoisyAccessLogLine(line))
        return;
    *buf += line;
    if (buf->size() > 65536)
        *buf = buf->right(65536);

    QFile log(AppPaths::coreLogFile());
    if (log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        log.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        log.write(" ");
        log.write(line.toUtf8());
        if (!line.endsWith('\n'))
            log.write("\n");
    }
}

XrayCoreManager::XrayCoreManager(QObject* parent) : QObject(parent) {
    proc_ = new QProcess(this);
    connect(proc_, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString line = QString::fromUtf8(proc_->readAllStandardOutput());
        appendLog(&recentLog_, line);
        emit logLine(line);
    });
    connect(proc_, &QProcess::readyReadStandardError, this, [this]() {
        const QString line = QString::fromUtf8(proc_->readAllStandardError());
        appendLog(&recentLog_, line);
        emit logLine(line);
    });
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                if (generation_ != aliveRun_)
                    return;
                emit stopped(code);
            });
}

bool XrayCoreManager::start(const QString& configPath) {
    stop();
    recentLog_.clear();
    aliveRun_ = ++generation_;

    const QString exe = AppPaths::xrayExe();
    if (!QFileInfo::exists(exe)) {
        lastError_ = "xray.exe not found";
        return false;
    }
    const QString coreDir = QFileInfo(exe).absolutePath();
    AppPaths::ensureWintunDll();
    copyWintun(coreDir);
    if (!QFileInfo::exists(coreDir + "/wintun.dll")) {
        lastError_ = "wintun.dll not found in core/";
        return false;
    }
    proc_->setProgram(exe);
    proc_->setArguments({"run", "-c", QDir::toNativeSeparators(configPath)});
    proc_->setWorkingDirectory(coreDir);
    proc_->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    proc_->start();
    if (!proc_->waitForStarted(5000)) {
        lastError_ = proc_->errorString();
        return false;
    }
    emit started();
    return true;
}

void XrayCoreManager::stop() {
    ++generation_;
    if (proc_->state() == QProcess::NotRunning)
        return;
    proc_->blockSignals(true);
    proc_->terminate();
    if (!proc_->waitForFinished(3000))
        proc_->kill();
    proc_->waitForFinished(1000);
    proc_->blockSignals(false);
}

bool XrayCoreManager::isRunning() const {
    return proc_->state() == QProcess::Running;
}

HysteriaCoreManager::HysteriaCoreManager(QObject* parent) : QObject(parent) {
    proc_ = new QProcess(this);
    connect(proc_, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString line = QString::fromUtf8(proc_->readAllStandardOutput());
        appendLog(&recentLog_, line);
        emit logLine(line);
    });
    connect(proc_, &QProcess::readyReadStandardError, this, [this]() {
        const QString line = QString::fromUtf8(proc_->readAllStandardError());
        appendLog(&recentLog_, line);
        emit logLine(line);
    });
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                if (generation_ != aliveRun_)
                    return;
                emit stopped(code);
            });
}

bool HysteriaCoreManager::start(const QString& configPath) {
    stop();
    recentLog_.clear();
    aliveRun_ = ++generation_;
    const QString exe = AppPaths::hysteriaExe();
    if (!QFileInfo::exists(exe)) {
        lastError_ = "hysteria.exe not found";
        return false;
    }
    copyWintun(QFileInfo(exe).absolutePath());
    proc_->setProgram(exe);
    proc_->setArguments({"client", "-c", QDir::toNativeSeparators(configPath)});
    proc_->setWorkingDirectory(QFileInfo(exe).absolutePath());
    proc_->start();
    if (!proc_->waitForStarted(5000)) {
        lastError_ = proc_->errorString();
        return false;
    }
    emit started();
    return true;
}

void HysteriaCoreManager::stop() {
    ++generation_;
    if (proc_->state() == QProcess::NotRunning)
        return;
    proc_->blockSignals(true);
    proc_->terminate();
    if (!proc_->waitForFinished(3000))
        proc_->kill();
    proc_->waitForFinished(1000);
    proc_->blockSignals(false);
}

bool HysteriaCoreManager::isRunning() const {
    return proc_->state() == QProcess::Running;
}

} // namespace vicvpn
