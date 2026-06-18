#include "vicvpn/ui/AboutDialog.h"
#include "vicvpn/app/I18n.h"
#include "vicvpn/app/Version.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace vicvpn {

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(VTR("about.title"));
    setFixedWidth(360);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    auto* icon = new QLabel;
    icon->setPixmap(QPixmap(":/icons/vicvpn.png").scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon);

    auto* title = new QLabel(QString("<b>%1</b>").arg(VTR("app.title")));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* ver = new QLabel(VTR("about.version") + " " + QString(VICVPN_VERSION));
    ver->setAlignment(Qt::AlignCenter);
    layout->addWidget(ver);

    auto* desc = new QLabel(VTR("about.description"));
    desc->setWordWrap(true);
    desc->setAlignment(Qt::AlignCenter);
    layout->addWidget(desc);

    auto* license = new QLabel(VTR("about.license"));
    license->setWordWrap(true);
    license->setAlignment(Qt::AlignCenter);
    license->setStyleSheet("color: #888;");
    layout->addWidget(license);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("OK");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

} // namespace vicvpn
