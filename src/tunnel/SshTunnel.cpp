#include "vicvpn/tunnel/SshTunnel.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <libssh2.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace vicvpn {

namespace {

std::once_flag g_libssh2InitFlag;

void ensureLibssh2Init() {
    std::call_once(g_libssh2InitFlag, []() { libssh2_init(0); });
}

QString lastLibssh2Error(LIBSSH2_SESSION* session, const QString& what) {
    char* msg = nullptr;
    const int rc = libssh2_session_last_error(session, &msg, nullptr, 0);
    const QString text = (msg && *msg) ? QString::fromUtf8(msg) : QStringLiteral("unknown error");
    return QString("%1: %2 (код %3)").arg(what, text).arg(rc);
}

SOCKET connectTcp(const QString& host, int port, QString* error) {
    const QByteArray hostUtf8 = host.toUtf8();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const QByteArray portText = QByteArray::number(port);
    addrinfo* res = nullptr;
    if (::getaddrinfo(hostUtf8.constData(), portText.constData(), &hints, &res) != 0 || !res) {
        *error = QString("Не удалось разрешить хост %1").arg(host);
        return INVALID_SOCKET;
    }

    SOCKET sock = INVALID_SOCKET;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        sock = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == INVALID_SOCKET)
            continue;
        if (::connect(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) {
            int yes = 1;
            ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&yes), sizeof(yes));
            break;
        }
        ::closesocket(sock);
        sock = INVALID_SOCKET;
    }
    ::freeaddrinfo(res);

    if (sock == INVALID_SOCKET)
        *error = QString("Не удалось подключиться к %1:%2").arg(host).arg(port);
    return sock;
}

bool sendAll(SOCKET fd, const char* data, int size) {
    int sent = 0;
    while (sent < size) {
        const int n = ::send(fd, data + sent, size - sent, 0);
        if (n <= 0)
            return false;
        sent += n;
    }
    return true;
}

bool recvExact(SOCKET fd, char* data, int size) {
    int got = 0;
    while (got < size) {
        const int n = ::recv(fd, data + got, size - got, 0);
        if (n <= 0)
            return false;
        got += n;
    }
    return true;
}

struct SocksTarget {
    QString host;
    int port = 0;
    bool valid = false;
};

bool socks5Handshake(SOCKET fd, SocksTarget* target) {
    char head[2];
    if (!recvExact(fd, head, 2) || head[0] != 0x05)
        return false;

    const int methodCount = static_cast<unsigned char>(head[1]);
    if (methodCount <= 0)
        return false;
    std::vector<char> methods(static_cast<size_t>(methodCount));
    if (!recvExact(fd, methods.data(), methodCount))
        return false;

    const char greeting[2] = {0x05, 0x00};
    if (!sendAll(fd, greeting, 2))
        return false;

    char req[4];
    if (!recvExact(fd, req, 4) || req[0] != 0x05 || req[1] != 0x01)
        return false;

    const char atyp = req[3];
    if (atyp == 0x01) {
        char buf[4];
        if (!recvExact(fd, buf, 4))
            return false;
        char text[INET_ADDRSTRLEN];
        in_addr addr;
        std::memcpy(&addr, buf, 4);
        if (!inet_ntop(AF_INET, &addr, text, sizeof(text)))
            return false;
        target->host = QString::fromLatin1(text);
    } else if (atyp == 0x03) {
        char lenByte;
        if (!recvExact(fd, &lenByte, 1))
            return false;
        const int len = static_cast<unsigned char>(lenByte);
        if (len <= 0 || len > 255)
            return false;
        std::vector<char> name(static_cast<size_t>(len) + 1, '\0');
        if (!recvExact(fd, name.data(), len))
            return false;
        target->host = QString::fromLatin1(name.data(), len);
    } else if (atyp == 0x04) {
        char buf[16];
        if (!recvExact(fd, buf, 16))
            return false;
        char text[INET6_ADDRSTRLEN];
        in6_addr addr;
        std::memcpy(&addr, buf, 16);
        if (!inet_ntop(AF_INET6, &addr, text, sizeof(text)))
            return false;
        target->host = QString::fromLatin1(text);
    } else {
        return false;
    }

    char portBuf[2];
    if (!recvExact(fd, portBuf, 2))
        return false;
    target->port = (static_cast<unsigned char>(portBuf[0]) << 8) |
                   static_cast<unsigned char>(portBuf[1]);
    target->valid = true;
    return true;
}

void sendSocks5Reply(SOCKET fd, char status) {
    const char reply[10] = {0x05, status, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    ::send(fd, reply, sizeof(reply), 0);
}

void pumpTraffic(SOCKET client, LIBSSH2_SESSION* session, LIBSSH2_CHANNEL* channel,
                 const std::atomic<bool>& running) {
    char buffer[16 * 1024];

    while (running.load()) {
        bool finished = false;

        for (;;) {
            const ssize_t n =
                libssh2_channel_read_ex(channel, 0, buffer, sizeof(buffer));
            if (n > 0) {
                if (!sendAll(client, buffer, static_cast<int>(n))) {
                    finished = true;
                    break;
                }
                continue;
            }
            if (n == 0) {
                finished = true;
                break;
            }
            if (libssh2_session_last_errno(session) == LIBSSH2_ERROR_EAGAIN)
                break;
            finished = true;
            break;
        }
        if (finished)
            return;

        WSAPOLLFD pfd{};
        pfd.fd = client;
        pfd.events = POLLRDNORM;
        const int ready = ::WSAPoll(&pfd, 1, 100);
        if (ready == SOCKET_ERROR)
            return;
        if (ready == 0)
            continue;

        const int received = ::recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0)
            return;

        int offset = 0;
        while (offset < received && running.load()) {
            const ssize_t written = libssh2_channel_write_ex(
                channel, 0, buffer + offset, static_cast<size_t>(received - offset));
            if (written > 0) {
                offset += static_cast<int>(written);
                continue;
            }
            if (written < 0 && libssh2_session_last_errno(session) == LIBSSH2_ERROR_EAGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            return;
        }
    }
}

} // namespace

struct SshTunnel::Impl {
    SshConfig config;
    LIBSSH2_SESSION* session = nullptr;
    std::atomic<bool> running{false};
    std::atomic<int> localPort{0};
    std::mutex sessionMutex;
    SOCKET sshSocket = INVALID_SOCKET;
    SOCKET listener = INVALID_SOCKET;
    std::thread worker;
    std::vector<std::thread> clients;

    ~Impl() { teardown(); }

    void teardown() {
        if (listener != INVALID_SOCKET) {
            ::shutdown(listener, SD_BOTH);
            ::closesocket(listener);
            listener = INVALID_SOCKET;
        }
        if (worker.joinable())
            worker.join();
        running.store(false);
        for (auto& t : clients) {
            if (t.joinable())
                t.detach();
        }
        clients.clear();
        if (session) {
            libssh2_session_disconnect(session, "VicVPN shutdown");
            libssh2_session_free(session);
            session = nullptr;
        }
        if (sshSocket != INVALID_SOCKET) {
            ::closesocket(sshSocket);
            sshSocket = INVALID_SOCKET;
        }
    }

    bool connectSession(QString* error) {
        ensureLibssh2Init();

        const int port = config.port > 0 ? config.port : 22;
        sshSocket = connectTcp(config.host, port, error);
        if (sshSocket == INVALID_SOCKET)
            return false;

        session = libssh2_session_init();
        if (!session) {
            *error = QString("libssh2_session_init failed");
            return false;
        }
        libssh2_session_set_blocking(session, 1);

        if (libssh2_session_handshake(session, sshSocket) != 0) {
            *error = lastLibssh2Error(session, QString("SSH handshake не прошёл (%1)").arg(config.host));
            return false;
        }

        const QByteArray user = config.user.toUtf8();
        int authRc = 0;
        const QString keyPath = config.privateKeyPath.trimmed();
        if (!keyPath.isEmpty()) {
            const QByteArray keyFile = keyPath.toUtf8();
            const QByteArray passphrase = config.passphrase.toUtf8();
            authRc = libssh2_userauth_publickey_fromfile(
                session, user.constData(), nullptr, keyFile.constData(),
                passphrase.isEmpty() ? nullptr : passphrase.constData());
        } else {
            const QByteArray password = config.password.toUtf8();
            authRc = libssh2_userauth_password(session, user.constData(), password.constData());
        }

        if (authRc != 0) {
            *error = lastLibssh2Error(session, QString("SSH-аутентификация не пройдена"));
            return false;
        }
        return true;
    }

    int openListener(QString* error) {
        listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) {
            *error = QString("Не удалось создать сокет туннеля");
            return -1;
        }

        int yes = 1;
        ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&yes), sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(listener, 32) != 0) {
            *error = QString("Не удалось занять локальный порт туннеля");
            ::closesocket(listener);
            listener = INVALID_SOCKET;
            return -1;
        }

        int nameLen = sizeof(addr);
        if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &nameLen) != 0) {
            *error = QString("getsockname failed");
            ::closesocket(listener);
            listener = INVALID_SOCKET;
            return -1;
        }
        return ntohs(addr.sin_port);
    }

    void runAcceptLoop() {
        while (running.load() && listener != INVALID_SOCKET) {
            WSAPOLLFD pfd{};
            pfd.fd = listener;
            pfd.events = POLLRDNORM;
            const int ready = ::WSAPoll(&pfd, 1, 200);
            if (ready == SOCKET_ERROR)
                break;
            if (ready == 0)
                continue;

            const SOCKET client = ::accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                continue;

            clients.emplace_back([this, client]() {
                handleClient(client);
                ::shutdown(client, SD_BOTH);
                ::closesocket(client);
            });
        }
    }

    void handleClient(SOCKET client) {
        if (!running.load())
            return;

        SocksTarget target;
        if (!socks5Handshake(client, &target) || !target.valid) {
            sendSocks5Reply(client, 0x01);
            return;
        }

        std::lock_guard<std::mutex> guard(sessionMutex);
        if (!session || !running.load()) {
            sendSocks5Reply(client, 0x01);
            return;
        }

        const QByteArray host = target.host.toUtf8();
        const QByteArray shost = config.host.toUtf8();
        const int sport = config.port > 0 ? config.port : 22;

        LIBSSH2_CHANNEL* channel = libssh2_channel_direct_tcpip_ex(
            session, host.constData(), static_cast<int>(target.port), shost.constData(), sport);
        if (!channel) {
            sendSocks5Reply(client, 0x01);
            return;
        }

        sendSocks5Reply(client, 0x00);
        libssh2_channel_set_blocking(channel, 0);
        pumpTraffic(client, session, channel, running);
        libssh2_channel_free(channel);
    }
};

SshTunnel::SshTunnel() : impl_(new Impl) {}

SshTunnel::~SshTunnel() {
    stop();
    delete impl_;
}

bool SshTunnel::start(const SshConfig& config, QString* error) {
    stop();
    impl_->config = config;

    QString err;
    if (!impl_->connectSession(&err)) {
        if (error)
            *error = err;
        return false;
    }

    const int port = impl_->openListener(&err);
    if (port <= 0) {
        if (error)
            *error = err;
        return false;
    }

    impl_->localPort.store(port);
    impl_->running.store(true);
    impl_->worker = std::thread([this]() { impl_->runAcceptLoop(); });
    return true;
}

void SshTunnel::stop() {
    impl_->running.store(false);
    if (impl_->listener != INVALID_SOCKET)
        ::shutdown(impl_->listener, SD_BOTH);
    if (impl_->worker.joinable())
        impl_->worker.join();
    impl_->teardown();
}

bool SshTunnel::isRunning() const {
    return impl_->running.load() && impl_->localPort.load() > 0;
}

quint16 SshTunnel::localPort() const {
    return static_cast<quint16>(impl_->localPort.load());
}

void SshTunnel::shutdownLibrary() {
    libssh2_exit();
}

} // namespace vicvpn