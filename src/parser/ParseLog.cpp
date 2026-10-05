#include "vicvpn/parser/ImportService.h"
#include "vicvpn/util/AppPaths.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

namespace vicvpn {

namespace {

QMutex g_logMutex;

constexpr int kMaxDetailLines = 400;

QString resolveLogPath() {
    return AppPaths::runtimeDir() + QStringLiteral("/import.log");
}

void appendLine(const QString& line) {
    QDir().mkpath(AppPaths::runtimeDir());
    QFile f(resolveLogPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    if (f.size() > 2 * 1024 * 1024) {
        f.close();
        QFile::remove(resolveLogPath());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            return;
    }
    f.write(line.toUtf8());
    f.write("\n");
}

} // namespace

QString ParseLog::filePath() {
    return resolveLogPath();
}

void ParseLog::init() {
    QDir().mkpath(AppPaths::runtimeDir());
    QMutexLocker lock(&g_logMutex);
    appendLine(QStringLiteral("=== import log opened %1 build=%2 ===")
                   .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                        QStringLiteral(VICVPN_BUILD_ID)));
}

QString ParseLog::detectScheme(const QString& input) {
    static const QRegularExpression re(R"(^([A-Za-z][A-Za-z0-9+.\-]{1,15}):)");
    const QString t = input.trimmed();
    const auto m = re.match(t);
    if (!m.hasMatch())
        return QStringLiteral("unknown");
    return m.captured(1).toLower();
}

QString ParseLog::mask(const QString& input) {
    QString s = input.trimmed();
    if (s.size() > 600)
        s = s.left(600) + QStringLiteral("...<truncated>");

    // userinfo between "//" and the last "@" before the path
    static const QRegularExpression userinfo(R"((://)([^/\s@]*)(@))");
    s.replace(userinfo, QStringLiteral("\\1<redacted>\\3"));

    // vmess-like base64 payloads have no authority at all
    static const QRegularExpression opaque(R"(^([A-Za-z0-9+.\-]{1,15}://)([A-Za-z0-9+/=_\-]{24,}))");
    s.replace(opaque, QStringLiteral("\\1<base64>"));

    // common query secrets
    static const QRegularExpression secrets(
        R"((?:^|[?&])(password|pass|pwd|passphrase|token|key|secret|uuid|id)=([^&\s#]*))",
        QRegularExpression::CaseInsensitiveOption);
    s.replace(secrets, QStringLiteral("\\1=<redacted>"));

    return s;
}

void ParseLog::detail(const QString& text) {
    QMutexLocker lock(&g_logMutex);
    static int counter = 0;
    if (++counter > kMaxDetailLines)
        return;
    appendLine(QStringLiteral("    %1").arg(text));
}

void ParseLog::attempt(const QString& input, const QString& result) {
    QMutexLocker lock(&g_logMutex);
    appendLine(QStringLiteral("%1  scheme=%2  in=[%3]  -> %4")
                   .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                        detectScheme(input), mask(input), result));
}

} // namespace vicvpn