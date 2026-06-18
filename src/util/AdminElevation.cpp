#include "vicvpn/util/AdminElevation.h"
#include <QCoreApplication>
#include <windows.h>
#include <shellapi.h>

namespace vicvpn {

bool isRunningAsAdmin() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    if (!ok)
        return false;
    return elevation.TokenIsElevated != FALSE;
}

bool requestAdminRelaunch() {
    const QString exe = QCoreApplication::applicationFilePath();
    const QStringList argList = QCoreApplication::arguments().mid(1);
    const auto wexe = reinterpret_cast<LPCWSTR>(exe.utf16());
    const QString args = argList.join(' ');
    LPCWSTR wargs = args.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(args.utf16());
    HINSTANCE r = ShellExecuteW(nullptr, L"runas", wexe, wargs, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(r) > 32;
}

} // namespace vicvpn
