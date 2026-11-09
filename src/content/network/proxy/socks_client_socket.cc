/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/proxy/socks_client_socket.h"

#include "content/network/async/async_operation_manager.h"
#include "content/network/logging/network_logger.h"
#include "content/network/socket/platform/socket_platform.h"

#include <cstring>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <arpa/inet.h>
#endif

namespace Platform = XER::Network::Socket::Platform;

namespace XER::Network::Proxy {

SocksClientSocket::SocksClientSocket(int fd, const ProxyServer& proxy)
    : fd_(fd), proxy_(proxy) {}

bool SocksClientSocket::Handshake(const std::string& host,
                                  uint16_t port,
                                  std::string* error) {
    Logging::NetworkLogger::Instance().LogInfo("proxy",
        "SOCKS handshake to " + proxy_.host + ":" + std::to_string(proxy_.port));

    if (proxy_.type == ProxyType::kSocks4)
        return Socks4Handshake(host, port, error);
    return Socks5Handshake(host, port, error);
}

void SocksClientSocket::HandshakeAsync(const std::string& host,
                                      uint16_t port,
                                      ProxyCallback cb) {
    Async::AsyncOperationManager::Instance().Enqueue(
        [this, host, port, cb = std::move(cb)]() {
            std::string error;
            bool ok = Handshake(host, port, &error);
            cb(ok, error);
        });
}

bool SocksClientSocket::Socks4Handshake(const std::string& host,
                                         uint16_t port,
                                         std::string* error) {
    auto& platform = Platform::SocketPlatform::GetInstance();
    Platform::SocketHandle socket =
        Platform::SocketHandle::Borrowed(static_cast<uintptr_t>(fd_));

    struct in_addr addr{};
    if (inet_pton(AF_INET, host.c_str(), &addr) != 1) {
        if (error) *error = "SOCKS4 requires IPv4 literal";
        return false;
    }
    std::vector<uint8_t> req = {
        0x04, 0x01,
        static_cast<uint8_t>(port >> 8),
        static_cast<uint8_t>(port & 0xff),
        static_cast<uint8_t>((ntohl(addr.s_addr) >> 24) & 0xff),
        static_cast<uint8_t>((ntohl(addr.s_addr) >> 16) & 0xff),
        static_cast<uint8_t>((ntohl(addr.s_addr) >>  8) & 0xff),
        static_cast<uint8_t>( ntohl(addr.s_addr)        & 0xff),
        0x00  // user ID (null)
    };
    platform.Send(socket, req.data(), req.size(), error);
    uint8_t resp[8]{};
    if (platform.Recv(socket, resp, 8, error) != 8 || resp[1] != 0x5a) {
        if (error) *error = "SOCKS4 handshake failed";
        return false;
    }
    return true;
}

bool SocksClientSocket::Socks5Handshake(const std::string& host,
                                         uint16_t port,
                                         std::string* error) {
    auto& platform = Platform::SocketPlatform::GetInstance();
    Platform::SocketHandle socket =
        Platform::SocketHandle::Borrowed(static_cast<uintptr_t>(fd_));

    // Auth negotiation
    const bool has_auth = !proxy_.username.empty();
    const uint8_t auth_method = has_auth ? 0x02 : 0x00;
    uint8_t greet[] = {0x05, 0x01, auth_method};
    platform.Send(socket, greet, 3, error);
    uint8_t resp[2]{};
    if (platform.Recv(socket, resp, 2, error) != 2 || resp[0] != 0x05) {
        if (error) *error = "SOCKS5 greeting failed";
        return false;
    }
    if (resp[1] == 0x02 && has_auth) {
        // Username/password auth (RFC 1929)
        std::vector<uint8_t> auth;
        auth.push_back(0x01);
        auth.push_back(static_cast<uint8_t>(proxy_.username.size()));
        for (char c : proxy_.username) auth.push_back(static_cast<uint8_t>(c));
        auth.push_back(static_cast<uint8_t>(proxy_.password.size()));
        for (char c : proxy_.password) auth.push_back(static_cast<uint8_t>(c));
        platform.Send(socket, auth.data(), auth.size(), error);
        uint8_t ar[2]{};
        if (platform.Recv(socket, ar, 2, error) != 2 || ar[1] != 0x00) {
            if (error) *error = "SOCKS5 auth rejected";
            return false;
        }
    }
    // Connect request
    std::vector<uint8_t> req = {0x05, 0x01, 0x00, 0x03};
    req.push_back(static_cast<uint8_t>(host.size()));
    for (char c : host) req.push_back(static_cast<uint8_t>(c));
    req.push_back(static_cast<uint8_t>(port >> 8));
    req.push_back(static_cast<uint8_t>(port & 0xff));
    platform.Send(socket, req.data(), req.size(), error);

    uint8_t conn[10]{};
    if (platform.Recv(socket, conn, 10, error) < 4 || conn[1] != 0x00) {
        if (error) *error = "SOCKS5 connect failed";
        return false;
    }
    return true;
}

}  // namespace XER::Network::Proxy
