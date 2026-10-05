#include "vicvpn/ui/AboutDialog.h"
#include "vicvpn/ui/UpdateDialog.h"
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

    auto* ver = new QLabel(VTR("about.version") + " " + QString(VICVPN_VERSION) + " (" +
                       QString(VICVPN_CHANNEL) + ")");
    ver->setAlignment(Qt::AlignCenter);
    layout->addWidget(ver);

    auto* build = new QLabel(QStringLiteral("build ") + QString(VICVPN_BUILD_ID));
    build->setAlignment(Qt::AlignCenter);
    build->setStyleSheet("color: #888;");
    layout->addWidget(build);

    auto* desc = new QLabel(VTR("about.description"));
    desc->setWordWrap(true);
    desc->setAlignment(Qt::AlignCenter);
    layout->addWidget(desc);

    auto* license = new QLabel(VTR("about.license"));
    license->setWordWrap(true);
    license->setAlignment(Qt::AlignCenter);
    license->setStyleSheet("color: #888;");
    layout->addWidget(license);

    auto* updateBtn = new QPushButton(VTR("about.check_update"));
    updateBtn->setToolTip(VTR("about.check_update"));
    layout->addWidget(updateBtn);
    connect(updateBtn, &QPushButton::clicked, this, [this]() {
        if (UpdateDialog::checkAndOffer(this))
            accept();
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("OK");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

} // namespace vicvpn
