#include "vicvpn/parser/SsconfCountry.h"
#include "vicvpn/app/I18n.h"

namespace vicvpn {

QVector<SsconfCountryOption> SsconfCountry::all() {
    return {
        {"", "Авто", "Auto"},
        {"US", "США", "United States"},
        {"NL", "Нидерланды", "Netherlands"},
        {"DE", "Германия", "Germany"},
        {"GB", "Великобритания", "United Kingdom"},
        {"FR", "Франция", "France"},
        {"CA", "Канада", "Canada"},
        {"JP", "Япония", "Japan"},
        {"SG", "Сингапур", "Singapore"},
        {"FI", "Финляндия", "Finland"},
        {"SE", "Швеция", "Sweden"},
        {"CH", "Швейцария", "Switzerland"},
        {"TR", "Турция", "Turkey"},
        {"HK", "Гонконг", "Hong Kong"},
        {"PL", "Польша", "Poland"},
        {"IT", "Италия", "Italy"},
        {"ES", "Испания", "Spain"},
        {"AU", "Австралия", "Australia"},
        {"KR", "Южная Корея", "South Korea"},
        {"IN", "Индия", "India"},
    };
}

QString SsconfCountry::displayName(const QString& code) {
    if (code.isEmpty())
        return {};
    for (const auto& c : all()) {
        if (c.code.compare(code, Qt::CaseInsensitive) == 0)
            return I18n::instance().langCode() == "en" ? c.nameEn : c.nameRu;
    }
    return code.toUpper();
}

QString SsconfCountry::flagEmoji(const QString& code) {
    if (code.size() != 2)
        return {};
    QString out;
    for (const QChar c : code.toUpper()) {
        if (c < 'A' || c > 'Z')
            return {};
        out.append(QChar(0x1F1E6 + c.unicode() - 'A'));
    }
    return out;
}

bool SsconfCountry::isSsconfUri(const QString& text) {
    const QString t = text.trimmed();
    return t.startsWith("ssconf://", Qt::CaseInsensitive) ||
           t.startsWith("ssconf:", Qt::CaseInsensitive);
}

} // namespace vicvpn
