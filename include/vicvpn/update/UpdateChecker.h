#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>

namespace vicvpn {

struct UpdateInfo {
    bool available = false;
    QString currentVersion;
    QString latestVersion;
    QString tagName;
    QString releaseName;
    QString notes;
    QString assetName;
    QString assetUrl;
    qint64 assetSize = 0;
};

class UpdateChecker {
public:
    using ProgressFn = std::function<void(qint64 received, qint64 total)>;
    using DoneFn = std::function<void(UpdateInfo info, QString error)>;

    /** "0.1.0-alpha" — the version this binary was built as. */
    static QString currentVersion();

    /** Blocking request to the GitHub releases API. Cheap enough for a button. */
    static UpdateInfo checkLatest(QString* error);

    /** Same query on a worker thread; the callback runs on the GUI thread. */
    static void checkLatestAsync(DoneFn done);

    /** true when VicVPN runs from a folder instead of Program Files. */
    static bool isPortableInstall();

    /** Downloads the release asset. Returns an empty path on failure. */
    static QString download(const UpdateInfo& info, QString* error,
                            const ProgressFn& progress = ProgressFn());

    /**
     * Writes an apply script and launches it detached. The script waits for this
     * process to exit, installs the new files and restarts the app.
     * On success the caller should quit immediately.
     */
    static bool startUpdate(const UpdateInfo& info, const QString& zipPath, QString* error);

    /** Exposed for tests: compares "0.1.0-alpha" style versions. */
    static bool isNewer(const QString& candidate, const QString& current);

    /**
     * Picks the asset matching the install layout: "portable" for a folder
     * copy, "setup" for an install. Returns an empty object when no match,
     * so a mismatched package is never applied to the wrong layout.
     */
    static QJsonObject pickAsset(const QJsonArray& assets, bool portable);
};

} // namespace vicvpn