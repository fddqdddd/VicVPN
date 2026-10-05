#pragma once

#include "vicvpn/model/ServerProfile.h"
#include <QByteArray>
#include <QString>
#include <vector>

namespace vicvpn {

QByteArray httpGetSubscription(const QString& url, QString* error = nullptr,
                               int timeoutMs = 30000, const QByteArray& userAgent = "VicVPN/0.1");

class ParseLog {
public:
    static void init();
    /** Logs an import attempt: detected scheme, masked input and outcome. */
    static void attempt(const QString& input, const QString& result);
    static void detail(const QString& text);
    static QString filePath();
    /** Returns "scheme" (lowercased, without "://") or "unknown". */
    static QString detectScheme(const QString& input);
    /** Hides passwords/UUIDs/keys so logs can be shared safely. */
    static QString mask(const QString& input);
};

class SubscriptionBodyDecoder {
public:
    static std::vector<ServerProfile> decode(const QByteArray& raw, QString* error = nullptr);
};

struct ImportOptions {
    QString ssconfCountry;
};

class SubscriptionFetcher {
public:
    static std::vector<ServerProfile> fetch(const QString& url, QString* error = nullptr);
};

class SsconfResolver {
public:
    static QString toFetchUrl(const QString& ssconfUri, const QString& countryCode = {});
    static std::vector<ServerProfile> resolve(const QString& ssconfUri, QString* error = nullptr,
                                             const QString& countryCode = {});
    static ServerProfile pickByCountry(const std::vector<ServerProfile>& servers,
                                       const QString& countryCode);
};

class ImportService {
public:
    static std::vector<ServerProfile> importText(const QString& text, QString* error = nullptr,
                                                 const ImportOptions& options = {});
    static std::vector<ServerProfile> importJson(const QString& jsonText, QString* error = nullptr);
};

} // namespace vicvpn
