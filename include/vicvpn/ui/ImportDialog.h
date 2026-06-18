#pragma once

#include <QDialog>
#include <vector>
#include "vicvpn/model/ServerProfile.h"
#include "vicvpn/parser/ImportService.h"

class QComboBox;
class QTabWidget;
class QTextEdit;
class QLineEdit;
class QLabel;

namespace vicvpn {

class ImportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ImportDialog(QWidget* parent = nullptr);
    std::vector<ServerProfile> imported() const { return imported_; }

private slots:
    void onClipboard();
    void onUrl();
    void onManual();
    void onOk();
    void onInputChanged();

private:
    void updateCountryUi(const QString& text);
    ImportOptions importOptions() const;

    QTabWidget* tabs_ = nullptr;
    QTextEdit* manualEdit_ = nullptr;
    QLineEdit* urlEdit_ = nullptr;
    QLabel* countryLabel_ = nullptr;
    QComboBox* countryCombo_ = nullptr;
    std::vector<ServerProfile> imported_;
};

} // namespace vicvpn
