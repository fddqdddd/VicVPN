#pragma once

#include "vicvpn/model/ServerProfile.h"
#include <QString>
#include <vector>

namespace vicvpn {

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
