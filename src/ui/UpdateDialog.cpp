#include "vicvpn/ui/UpdateDialog.h"
#include "vicvpn/update/UpdateChecker.h"
#include "vicvpn/app/I18n.h"
#include "vicvpn/util/AppPaths.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>

namespace vicvpn {

namespace {

void logLine(const QString& text) {
    QDir().mkpath(AppPaths::runtimeDir());
    QFile log(AppPaths::runtimeDir() + QStringLiteral("/update.log"));
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    // BOM so the file opens as UTF-8 in Notepad.
    if (log.size() == 0)
        log.write("\xEF\xBB\xBF");
    log.write(text.toUtf8());
    log.write("\n");
}

QString humanSize(qint64 bytes) {
    if (bytes <= 0)
        return {};
    return QStringLiteral("%1 МБ").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

void showUpToDate(QWidget* parent, const UpdateInfo& info) {
    QMessageBox::information(parent, VTR("update.title"),
                             QStringLiteral("Установленная версия (%1) актуальна.")
                                 .arg(info.currentVersion));
}

void showCheckFailed(QWidget* parent, const QString& error) {
    QMessageBox::warning(parent, VTR("update.title"),
                         QStringLiteral("Не удалось проверить обновления: %1").arg(error));
}

bool confirmUpdate(QWidget* parent, const UpdateInfo& info) {
    const QString sizeText = humanSize(info.assetSize);
    QMessageBox box(parent);
    box.setWindowTitle(VTR("update.title"));
    box.setIcon(QMessageBox::Information);
    box.setText(QStringLiteral("Доступна новая версия %1\nТекущая: %2")
                    .arg(info.latestVersion, info.currentVersion));
    QString details = QStringLiteral("Файл: %1").arg(info.assetName);
    if (!sizeText.isEmpty())
        details += QStringLiteral(" (%1)").arg(sizeText);
    if (!info.notes.isEmpty())
        details += QStringLiteral("\n\n%1").arg(info.notes.left(1200));
    box.setInformativeText(details);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::Yes);
    return box.exec() == QMessageBox::Yes;
}

void downloadAndApply(QWidget* parent, const UpdateInfo& info) {
    auto* progress = new QProgressDialog(VTR("update.downloading"), QStringLiteral("Отмена"), 0, 100,
                                         parent);
    progress->setWindowTitle(VTR("update.title"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);

    QString dlError;
    const QString zip = UpdateChecker::download(
        info, &dlError, [progress](qint64 received, qint64 total) {
            const int percent = total > 0 ? int(received * 100 / total) : 0;
            progress->setValue(percent);
            if (total > 0) {
                progress->setLabelText(VTR("update.downloading") +
                                       QStringLiteral(" %1% (%2 / %3 МБ)")
                                           .arg(percent)
                                           .arg(received / (1024.0 * 1024.0), 0, 'f', 1)
                                           .arg(total / (1024.0 * 1024.0), 0, 'f', 1));
            }
        });

    progress->close();
    progress->deleteLater();

    if (zip.isEmpty()) {
        logLine(QStringLiteral("download failed: %1").arg(dlError));
        QMessageBox::warning(parent, VTR("update.title"),
                             QStringLiteral("Не удалось скачать обновление: %1").arg(dlError));
        return;
    }
    logLine(QStringLiteral("downloaded to %1").arg(zip));

    QString startError;
    if (!UpdateChecker::startUpdate(info, zip, &startError)) {
        logLine(QStringLiteral("startUpdate failed: %1").arg(startError));
        QMessageBox::warning(parent, VTR("update.title"), startError);
        return;
    }

    logLine(QStringLiteral("update started, quitting"));
    QMessageBox::information(
        parent, VTR("update.title"),
        QStringLiteral("Обновление до %1 запущено. Приложение будет закрыто.")
            .arg(info.latestVersion));
    QTimer::singleShot(300, qApp, &QApplication::quit);
}

} // namespace

void UpdateDialog::checkAndOffer(QWidget* parent, std::function<void()> onFinished) {
    logLine(QStringLiteral("--- update check (manual) ---"));

    // The caller's window may already be gone by the time the reply arrives.
    const QPointer<QWidget> guard(parent);
    UpdateChecker::checkLatestAsync([guard, onFinished](UpdateInfo info, QString error) {
        QWidget* parent = guard.data();
        if (!error.isEmpty()) {
            logLine(QStringLiteral("check failed: %1").arg(error));
            showCheckFailed(parent, error);
        } else if (!info.available) {
            logLine(QStringLiteral("up to date, current=%1 latest=%2")
                        .arg(info.currentVersion, info.latestVersion));
            showUpToDate(parent, info);
        } else {
            logLine(QStringLiteral("update available: %1 -> %2 (%3, %4 bytes)")
                        .arg(info.currentVersion, info.latestVersion, info.assetName,
                             QString::number(info.assetSize)));
            if (confirmUpdate(parent, info))
                downloadAndApply(parent, info);
            else
                logLine(QStringLiteral("update declined"));
        }
        if (onFinished)
            onFinished();
    });
}

void UpdateDialog::checkSilently(QWidget* parent) {
    const QPointer<QWidget> guard(parent);
    UpdateChecker::checkLatestAsync([guard](UpdateInfo info, QString error) {
        QWidget* parent = guard.data();
        if (!error.isEmpty()) {
            logLine(QStringLiteral("startup check failed: %1").arg(error));
            return;
        }
        if (!info.available) {
            logLine(QStringLiteral("startup: up to date (%1)").arg(info.currentVersion));
            return;
        }
        logLine(QStringLiteral("startup: update available %1").arg(info.latestVersion));
        if (confirmUpdate(parent, info))
            downloadAndApply(parent, info);
    });
}

} // namespace vicvpn