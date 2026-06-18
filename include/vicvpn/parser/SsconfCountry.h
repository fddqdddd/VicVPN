#pragma once

#include <QString>
#include <QVector>

namespace vicvpn {

struct SsconfCountryOption {
    QString code;      // ISO 3166-1 alpha-2, empty = auto
    QString nameRu;
    QString nameEn;
};

class SsconfCountry {
public:
    static QVector<SsconfCountryOption> all();
    static QString displayName(const QString& code);
    static QString flagEmoji(const QString& code);
    static bool isSsconfUri(const QString& text);
};

} // namespace vicvpn
