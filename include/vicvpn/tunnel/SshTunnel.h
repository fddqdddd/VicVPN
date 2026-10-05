#pragma once

#include "vicvpn/model/ServerProfile.h"
#include <QString>

namespace vicvpn {

class SshTunnel {
public:
    SshTunnel();
    ~SshTunnel();

    SshTunnel(const SshTunnel&) = delete;
    SshTunnel& operator=(const SshTunnel&) = delete;

    bool start(const SshConfig& config, QString* error);
    void stop();
    bool isRunning() const;
    quint16 localPort() const;

    static void shutdownLibrary();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace vicvpn