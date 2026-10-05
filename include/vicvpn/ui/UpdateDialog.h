#pragma once

#include <QWidget>

namespace vicvpn {

class UpdateDialog {
public:
    /**
     * Asks GitHub for the newest release and, when one exists, offers to
     * download and install it. Returns true when an update was started, in
     * which case the caller must let the application quit.
     */
    static bool checkAndOffer(QWidget* parent);

    /** Silent variant used at startup; never blocks the UI for long. */
    static void checkSilently(QWidget* parent);
};

} // namespace vicvpn