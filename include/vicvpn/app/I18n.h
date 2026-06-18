#pragma once

#include <QString>
#include <QMap>

namespace vicvpn {

class I18n {
public:
    enum class Lang { Ru, En };

    static I18n& instance();
    void setLang(Lang lang);
    Lang lang() const { return lang_; }
    QString tr(const char* key) const;
    QString langCode() const;

private:
    I18n();
    void loadRu();
    void loadEn();

    Lang lang_ = Lang::Ru;
    QMap<QString, QString> ru_;
    QMap<QString, QString> en_;
};

#define VTR(key) vicvpn::I18n::instance().tr(key)

} // namespace vicvpn
