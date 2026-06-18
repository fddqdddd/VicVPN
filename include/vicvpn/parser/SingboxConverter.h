#pragma once

#include "vicvpn/model/ServerProfile.h"
#include <nlohmann/json.hpp>
#include <QString>
#include <optional>
#include <vector>

namespace vicvpn {

class SingboxConverter {
public:
    static bool isSingbox(const nlohmann::json& j);
    static std::vector<ServerProfile> importProfiles(const nlohmann::json& j,
                                                     const QString& raw,
                                                     QString* error = nullptr);
    static std::optional<ServerProfile> fromShadowsocksLegacy(const nlohmann::json& j,
                                                              const QString& raw);
    static std::optional<nlohmann::json> toXrayOutbound(const nlohmann::json& ob);
};

} // namespace vicvpn
