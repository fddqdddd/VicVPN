#pragma once

#include "vicvpn/model/ServerProfile.h"
#include <QString>
#include <vector>
#include <optional>

namespace vicvpn {

class UriParser {
public:
    static std::optional<ServerProfile> parse(const QString& input);
    static std::vector<ServerProfile> parseMany(const QString& blob);
};

class Hy2UriParser {
public:
    static std::optional<ServerProfile> parse(const QString& input);
};

} // namespace vicvpn
