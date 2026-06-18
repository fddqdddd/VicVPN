#include <QApplication>
#include <QIcon>
#include "vicvpn/ui/MainWindow.h"
#include "vicvpn/app/Settings.h"
#include "vicvpn/util/PlatformAutostart.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("VicVPN");
    QApplication::setOrganizationName("VZorg");
    QApplication::setWindowIcon(QIcon(":/icons/vicvpn.png"));

    vicvpn::Settings::instance().load();
    const auto& settings = vicvpn::Settings::instance().get();
    vicvpn::PlatformAutostart::setEnabled(settings.autostart, QApplication::applicationFilePath());

    vicvpn::MainWindow w;
    w.show();
    return app.exec();
}
