#pragma once

#include <QDialog>

namespace vicvpn {

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

} // namespace vicvpn
