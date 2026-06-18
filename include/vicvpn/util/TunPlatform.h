#pragma once

#ifdef Q_OS_WIN
#include "vicvpn/util/TunWindows.h"
namespace vicvpn {
using TunPlatform = TunWindows;
}
#else
#include "vicvpn/util/TunLinux.h"
namespace vicvpn {
using TunPlatform = TunLinux;
}
#endif
