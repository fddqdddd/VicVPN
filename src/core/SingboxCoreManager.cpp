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
    if (QFileInfo::exists(src)) {
        QFile::remove(dst);
        QFile::copy(src, dst);
    }
}

static void appendCoreLog(QString* buf, const QString& line, const char* tag) {
    if (!buf || line.isEmpty())
        return;
    *buf += line;
    if (buf->size() > 65536)
        *buf = buf->right(65536);

    QFile log(AppPaths::coreLogFile());
    if (log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        log.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
        log.write(" [");
        log.write(tag);
        log.write("] ");
        log.write(line.toUtf8());
        if (!line.endsWith('\n'))
            log.write("\n");
    }
}

SingboxCoreManager::SingboxCoreManager(QObject* parent) : QObject(parent) {
    proc_ = new QProcess(this);
    connect(proc_, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString line = QString::fromUtf8(proc_->readAllStandardOutput());
        appendCoreLog(&recentLog_, line, "sing-box");
        emit logLine(line);
    });
    connect(proc_, &QProcess::readyReadStandardError, this, [this]() {
        const QString line = QString::fromUtf8(proc_->readAllStandardError());
        appendCoreLog(&recentLog_, line, "sing-box");
        emit logLine(line);
    });
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                if (generation_ != aliveRun_)
                    return;
                emit stopped(code);
            });
}

bool SingboxCoreManager::start(const QString& configPath) {
    stop();
    recentLog_.clear();
    aliveRun_ = ++generation_;

    const QString exe = AppPaths::singboxExe();
    if (!QFileInfo::exists(exe)) {
        lastError_ = "sing-box.exe not found";
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
    proc_->setArguments({"run", "-c", QDir::toNativeSeparators(configPath),
                        "-D", QDir::toNativeSeparators(coreDir)});
    proc_->setWorkingDirectory(coreDir);
    proc_->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    proc_->start();
    if (!proc_->waitForStarted(8000)) {
        lastError_ = proc_->errorString();
        return false;
    }
    emit started();
    return true;
}

void SingboxCoreManager::stop() {
    ++generation_;
    if (proc_->state() == QProcess::NotRunning)
        return;
    proc_->blockSignals(true);
    proc_->terminate();
    if (!proc_->waitForFinished(5000))
        proc_->kill();
    proc_->waitForFinished(3000);
    proc_->blockSignals(false);
}

bool SingboxCoreManager::isRunning() const {
    return proc_->state() == QProcess::Running;
}

} // namespace vicvpn
