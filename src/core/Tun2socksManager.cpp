#include "vicvpn/core/CoreManagers.h"
#include "vicvpn/util/AppPaths.h"
#include "vicvpn/core/CoreHealth.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <QProcessEnvironment>

namespace vicvpn {

static void copyWintunToCore(const QString& targetDir) {
    const QString src = AppPaths::wintunDll();
    const QString dst = targetDir + "/wintun.dll";
    if (QFileInfo::exists(src)) {
        QFile::remove(dst);
        QFile::copy(src, dst);
    }
}

static void appendTun2socksLog(QString* buf, const QString& line) {
    if (!buf || line.isEmpty())
        return;
    *buf += line;
    if (buf->size() > 65536)
        *buf = buf->right(65536);

    QFile log(AppPaths::coreLogFile());
    if (log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        log.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        log.write(" [tun2socks] ");
        log.write(line.toUtf8());
        if (!line.endsWith('\n'))
            log.write("\n");
    }
}

Tun2socksManager::Tun2socksManager(QObject* parent) : QObject(parent) {
    proc_ = new QProcess(this);
    connect(proc_, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString line = QString::fromUtf8(proc_->readAllStandardOutput());
        appendTun2socksLog(&recentLog_, line);
        emit logLine(line);
    });
    connect(proc_, &QProcess::readyReadStandardError, this, [this]() {
        const QString line = QString::fromUtf8(proc_->readAllStandardError());
        appendTun2socksLog(&recentLog_, line);
        emit logLine(line);
    });
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                if (generation_ != aliveRun_)
                    return;
                emit stopped(code);
            });
}

bool Tun2socksManager::start(const QString& proxyUrl, const QString& tunName,
                             const QString& physicalInterface, int mtu) {
    stop();
    recentLog_.clear();
    aliveRun_ = ++generation_;

    const QString exe = AppPaths::tun2socksExe();
    if (!QFileInfo::exists(exe)) {
        lastError_ = "tun2socks.exe not found";
        return false;
    }
    if (proxyUrl.isEmpty()) {
        lastError_ = "empty proxy URL";
        return false;
    }

    const QString coreDir = QFileInfo(exe).absolutePath();
    AppPaths::ensureWintunDll();
    copyWintunToCore(coreDir);

    QStringList args;
    args << "--device" << ("tun://" + tunName);
    args << "--proxy" << proxyUrl;
    args << "--loglevel" << "warn";
    args << "--tcp-auto-tuning";
    if (mtu >= 1280 && mtu <= 9000)
        args << "--mtu" << QString::number(mtu);
#if !defined(Q_OS_WIN)
    if (!physicalInterface.isEmpty())
        args << "--interface" << physicalInterface;
#endif

    proc_->setProgram(exe);
    proc_->setArguments(args);
    proc_->setWorkingDirectory(coreDir);
    proc_->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    proc_->start();
    if (!proc_->waitForStarted(8000)) {
        lastError_ = proc_->errorString();
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1800) {
        if (proc_->bytesAvailable() > 0 || proc_->waitForReadyRead(80)) {
            const QString out = QString::fromUtf8(proc_->readAllStandardOutput());
            const QString err = QString::fromUtf8(proc_->readAllStandardError());
            appendTun2socksLog(&recentLog_, out);
            appendTun2socksLog(&recentLog_, err);
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (proc_->state() != QProcess::Running)
            break;
        if (CoreHealth::tun2socksHasFatal(recentLog_))
            break;
    }

    if (proc_->state() != QProcess::Running || CoreHealth::tun2socksHasFatal(recentLog_)) {
        lastError_ = CoreHealth::tun2socksErrorMessage(recentLog_);
        if (lastError_.isEmpty())
            lastError_ = QString("tun2socks exited (code %1)").arg(proc_->exitCode());
        ++generation_;
        return false;
    }
    emit started();
    return true;
}

void Tun2socksManager::stop() {
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

bool Tun2socksManager::isRunning() const {
    return proc_->state() == QProcess::Running;
}

} // namespace vicvpn
