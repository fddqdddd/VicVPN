#pragma once

#include <QWidget>
#include <functional>

namespace vicvpn {

class UpdateDialog {
public:
    /**
     * Asks GitHub for the newest release and, when one exists, offers to
     * download and install it. Returns immediately; onFinished runs once the
     * check has reported back (or nothing is reachable). The parent may be
     * destroyed while the request is in flight.
     */
    static void checkAndOffer(QWidget* parent, std::function<void()> onFinished = {});

    /** Silent variant used at startup; never blocks the UI for long. */
    static void checkSilently(QWidget* parent);
};

} // namespace vicvpn