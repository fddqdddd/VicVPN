#include "vicvpn/update/UpdateChecker.h"
#include "vicvpn/app/Version.h"
#include "vicvpn/util/AppPaths.h"
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>

namespace vicvpn {

namespace {

QString owner() {
#ifdef VICVPN_REPO_OWNER
    return QString::fromLatin1(VICVPN_REPO_OWNER);
#else
    return QStringLiteral("fddqdddd");
#endif
}

QString repo() {
#ifdef VICVPN_REPO_NAME
    return QString::fromLatin1(VICVPN_REPO_NAME);
#else
    return QStringLiteral("VicVPN");
#endif
}

QByteArray httpGet(const QUrl& url, const QMap<QByteArray, QByteArray>& headers, int timeoutMs,
                   QString* error, const std::function<void(qint64, qint64)>& onProgress = {}) {
    QNetworkAccessManager nam;
    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(timeoutMs);
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        req.setRawHeader(it.key(), it.value());

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply* reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    if (onProgress) {
        QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                         [&](qint64 received, qint64 total) { onProgress(received, total); });
    }
    timer.start(timeoutMs);
    loop.exec();
    timer.stop();

    const auto netError = reply->error();
    const QString netErrorText = reply->errorString();
    const QByteArray data = reply->readAll();
    reply->deleteLater();

    if (netError != QNetworkReply::NoError) {
        if (error) {
            if (netError == QNetworkReply::OperationCanceledError)
                *error = QStringLiteral("Таймаут ответа сервера (%1 с)").arg(timeoutMs / 1000);
            else
                *error = netErrorText;
        }
        return {};
    }
    return data;
}

QString applyScript() {
    return QStringLiteral(R"PS(#Requires -Version 5.1
param(
    [string]$ZipPath,
    [string]$Mode,
    [string]$ExePath,
    [string]$AppDir
)
$ErrorActionPreference = 'Stop'

function Fail([string]$Message) {
    Add-Content -Path $PSCommandPath -Value "FAILED: $Message"
    exit 1
}

# The app that launched us must be gone before its files can be replaced.
$running = Get-Process -Name 'VicVPN' -ErrorAction SilentlyContinue
if ($running) {
    $running | Wait-Process -Timeout 90 -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 800
}

if (-not (Test-Path $ZipPath)) { Fail "archive missing: $ZipPath" }

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("vicvpn-upd-" + [Guid]::NewGuid().ToString('N'))
try {
    Expand-Archive -Path $ZipPath -DestinationPath $tmp -Force
} catch { Fail "extract failed: $($_.Exception.Message)" }

$skip = @('Install-VicVPN.ps1', 'Install-VicVPN.cmd', 'Uninstall-VicVPN.ps1', 'VERSION.txt')

if ($Mode -eq 'portable') {
    Get-ChildItem -Path $tmp -Force | Where-Object { $_.Name -notin $skip -and $_.Name -ne 'data' } |
        ForEach-Object {
            $dest = Join-Path $AppDir $_.Name
            if ($_.PSIsContainer) { Copy-Item $_.FullName $dest -Recurse -Force }
            else { Copy-Item $_.FullName $dest -Force }
        }
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
} else {
    $installer = Join-Path $tmp 'Install-VicVPN.ps1'
    if (-not (Test-Path $installer)) { Fail "installer missing in archive" }
    & $installer -Silent
    if ($LASTEXITCODE -ne 0) { Fail "installer returned $LASTEXITCODE" }
}

Start-Process $ExePath
exit 0
)PS");
}

} // namespace

QJsonObject UpdateChecker::pickAsset(const QJsonArray& assets, bool portable) {
    const QString wanted = portable ? QStringLiteral("portable") : QStringLiteral("setup");
    const QString other = portable ? QStringLiteral("setup") : QStringLiteral("portable");

    QJsonArray candidates;
    for (const QJsonValue& v : assets) {
        const QJsonObject a = v.toObject();
        const QString name = a.value("name").toString();
        if (name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
            candidates.append(a);
    }

    for (const QJsonValue& v : std::as_const(candidates)) {
        const QString name = v.toObject().value("name").toString();
        if (name.contains(wanted, Qt::CaseInsensitive))
            return v.toObject();
    }

    // No package for this layout. A zip that does not name either layout may
    // still be generic, but the other layout's package must never be applied:
    // its internal structure differs and extraction would leave a broken install.
    QJsonObject generic;
    for (const QJsonValue& v : std::as_const(candidates)) {
        const QString name = v.toObject().value("name").toString();
        if (name.contains(other, Qt::CaseInsensitive))
            continue;
        if (!generic.isEmpty())
            return QJsonObject();
        generic = v.toObject();
    }
    return generic;
}

QString UpdateChecker::currentVersion() {
    return QString::fromLatin1(VICVPN_VERSION);
}

bool UpdateChecker::isNewer(const QString& candidate, const QString& current) {
    static const QRegularExpression re(R"(^v?(\d+)(?:\.(\d+))?(?:\.(\d+))?(.*)$)");
    const auto cm = re.match(candidate.trimmed());
    const auto rm = re.match(current.trimmed());
    if (!cm.hasMatch() || !rm.hasMatch())
        return false;

    const int cv[3] = {cm.captured(1).toInt(), cm.captured(2).toInt(), cm.captured(3).toInt()};
    const int rv[3] = {rm.captured(1).toInt(), rm.captured(2).toInt(), rm.captured(3).toInt()};
    for (int i = 0; i < 3; ++i) {
        if (cv[i] != rv[i])
            return cv[i] > rv[i];
    }

    // Same numbers: a final release supersedes a pre-release of the same version.
    const bool cPre = !cm.captured(4).isEmpty();
    const bool rPre = !rm.captured(4).isEmpty();
    if (cPre != rPre)
        return !cPre;
    return false;
}

bool UpdateChecker::isPortableInstall() {
    const QString dir = AppPaths::exeDir();
    return QFileInfo::exists(dir + QStringLiteral("/portable")) ||
           QFileInfo::exists(dir + QStringLiteral("/data"));
}

void UpdateChecker::checkLatestAsync(DoneFn done) {
    // A blocking HTTPS call on the GUI thread freezes the window for seconds,
    // so the query runs on its own thread and the result is marshalled back.
    class Worker : public QThread {
    public:
        DoneFn callback;
        void run() override {
            QString error;
            UpdateInfo info = UpdateChecker::checkLatest(&error);
            DoneFn cb = std::move(callback);
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [cb, info = std::move(info), error = std::move(error)]() mutable {
                    cb(std::move(info), std::move(error));
                },
                Qt::QueuedConnection);
        }
    };

    auto* worker = new Worker;
    worker->callback = std::move(done);
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

UpdateInfo UpdateChecker::checkLatest(QString* error) {
    UpdateInfo info;
    info.currentVersion = currentVersion();
    info.latestVersion = currentVersion();

    // /releases rather than /releases/latest: alpha builds are published as
    // pre-releases and would be invisible to the latest endpoint.
    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/%2/releases?per_page=10")
                       .arg(owner(), repo()));
    QMap<QByteArray, QByteArray> headers;
    headers.insert("Accept", "application/vnd.github+json");
    headers.insert("User-Agent", "VicVPN");

    const QByteArray body = httpGet(url, headers, 15000, error);
    if (body.isEmpty())
        return info;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        if (error) *error = QStringLiteral("Не удалось разобрать ответ GitHub");
        return info;
    }

    for (const QJsonValue& v : doc.array()) {
        const QJsonObject r = v.toObject();
        if (r.value("draft").toBool())
            continue;
        const QString tag = r.value("tag_name").toString();
        if (tag.isEmpty())
            continue;

        info.tagName = tag;
        info.latestVersion = tag;
        info.releaseName = r.value("name").toString();
        info.notes = r.value("body").toString().trimmed();
        const QJsonObject asset = pickAsset(r.value("assets").toArray(), isPortableInstall());
        info.assetName = asset.value("name").toString();
        info.assetUrl = asset.value("browser_download_url").toString();
        info.assetSize = static_cast<qint64>(asset.value("size").toDouble());
        break;
    }

    if (info.tagName.isEmpty()) {
        // Nothing published yet is a normal state, not a failure: stay quiet
        // so a fresh install does not look broken.
        return info;
    }

    info.available = isNewer(info.tagName, info.currentVersion);
    if (!info.assetUrl.isEmpty())
        return info;

    if (info.available) {
        if (error)
            *error = QStringLiteral("В релизе %1 нет ZIP-артефакта").arg(info.tagName);
        info.available = false;
    }
    return info;
}

QString UpdateChecker::download(const UpdateInfo& info, QString* error, const ProgressFn& progress) {
    if (info.assetUrl.isEmpty()) {
        if (error) *error = QStringLiteral("Нет ссылки на загрузку");
        return {};
    }

    const QString dir = AppPaths::runtimeDir() + QStringLiteral("/updates/") + info.tagName;
    QDir().mkpath(dir);
    const QString target = dir + QLatin1Char('/') + info.assetName;

    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(info.assetUrl)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "VicVPN");
    req.setTransferTimeout(15 * 60 * 1000);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Не удалось создать %1").arg(target);
        return {};
    }

    QNetworkReply* reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (progress) {
        QObject::connect(reply, &QNetworkReply::downloadProgress, reply,
                         [&](qint64 received, qint64 total) { progress(received, total); });
    }
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [&out, reply]() {
        out.write(reply->readAll());
    });

    timer.start(15 * 60 * 1000);
    loop.exec();
    timer.stop();

    const auto netError = reply->error();
    const QString netErrorText = reply->errorString();
    out.write(reply->readAll());
    reply->deleteLater();
    out.close();

    if (netError != QNetworkReply::NoError) {
        QFile::remove(target);
        if (error)
            *error = netError == QNetworkReply::OperationCanceledError
                         ? QStringLiteral("Загрузка прервана по таймауту")
                         : netErrorText;
        return {};
    }
    const qint64 written = QFileInfo(target).size();
    if (written == 0) {
        QFile::remove(target);
        if (error) *error = QStringLiteral("Загружен пустой файл");
        return {};
    }
    if (info.assetSize > 0 && written != info.assetSize) {
        // A dropped connection can finish without an error; a short file would
        // otherwise be passed to the apply script.
        QFile::remove(target);
        if (error) {
            *error = QStringLiteral("Файл обрезан: получено %1 из %2 байт")
                         .arg(written)
                         .arg(info.assetSize);
        }
        return {};
    }
    return target;
}

bool UpdateChecker::startUpdate(const UpdateInfo& info, const QString& zipPath, QString* error) {
    if (zipPath.isEmpty() || !QFileInfo::exists(zipPath)) {
        if (error) *error = QStringLiteral("Архив обновления не найден");
        return false;
    }

    const QString dir = AppPaths::runtimeDir() + QStringLiteral("/updates");
    QDir().mkpath(dir);
    const QString scriptPath = dir + QStringLiteral("/apply-update.ps1");
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Не удалось создать %1").arg(scriptPath);
        return false;
    }
    script.write(applyScript().toUtf8());
    script.close();

    const QString mode = isPortableInstall() ? QStringLiteral("portable") : QStringLiteral("installed");
    const QStringList args{QStringLiteral("-NoProfile"),
                           QStringLiteral("-ExecutionPolicy"),
                           QStringLiteral("Bypass"),
                           QStringLiteral("-File"),
                           scriptPath,
                           QStringLiteral("-ZipPath"),
                           QFileInfo(zipPath).absoluteFilePath(),
                           QStringLiteral("-Mode"),
                           mode,
                           QStringLiteral("-ExePath"),
                           QFileInfo(AppPaths::exeDir() + QStringLiteral("/VicVPN.exe"))
                               .absoluteFilePath(),
                           QStringLiteral("-AppDir"),
                           QFileInfo(AppPaths::exeDir()).absoluteFilePath()};

    const QString exe = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    const QString shell = exe.isEmpty() ? QStringLiteral("powershell.exe") : exe;
    qint64 pid = 0;
    if (!QProcess::startDetached(shell, args, AppPaths::exeDir(), &pid)) {
        if (error) *error = QStringLiteral("Не удалось запустить скрипт обновления");
        return false;
    }

    if (error)
        *error = QStringLiteral("Обновление до %1 запущено").arg(info.latestVersion);
    return true;
}

} // namespace vicvpn