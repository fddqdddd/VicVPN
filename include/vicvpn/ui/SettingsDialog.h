#pragma once

#include <QDialog>

namespace vicvpn {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
};

} // namespace vicvpn
