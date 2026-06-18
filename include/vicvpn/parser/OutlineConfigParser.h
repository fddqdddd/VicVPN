#pragma once

#include "vicvpn/model/ServerProfile.h"
#include <QString>
#include <vector>

namespace vicvpn {

class OutlineConfigParser {
public:
    static bool looksLikeOutlineYaml(const QString& text);
    static std::vector<ServerProfile> importProfiles(const QString& yamlOrText,
                                                     const QString& raw = QString());
};

} // namespace vicvpn
