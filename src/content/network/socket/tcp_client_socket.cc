/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/socket/tcp_client_socket.h"

#include "content/network/async/async_operation_manager.h"
#include "content/network/logging/network_logger.h"
#include "content/network/socket/platform/socket_platform.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace XER::Network::Socket {

TcpClientSocket::~TcpClientSocket() {
    Disconnect();
}

bool TcpClientSocket::Connect(const std::string& host,
                               uint16_t port,
                               std::string* error) {
    Logging::NetworkLogger::Instance().LogConnectionAttempt(host, port);

    auto& platform = Platform::SocketPlatform::GetInstance();

    // Resolve address
    std::vector<Platform::AddressInfo> addresses;
    if (!platform.ResolveAddress(host, port, &addresses, error)) {
        Logging::NetworkLogger::Instance().LogConnectionFailure(host, port, *error);
        return false;
    }

    // Try each address
    for (const auto& addr : addresses) {
        Platform::SocketHandle socket;
        if (addr.is_ipv6) {
            socket = platform.CreateTcpSocket(true, error);
        } else {
            socket = platform.CreateTcpSocket(false, error);
        }

        if (!socket.IsValid()) {
            continue;
        }

        if (platform.Connect(socket, addr, error)) {
            fd_ = static_cast<int>(socket.Get());
            // Detach the handle so it won't be closed by SocketHandle destructor
            socket.Detach();
            stream_ = std::make_unique<SocketStream>(fd_);

            Logging::NetworkLogger::Instance().LogConnectionSuccess(host, port, 0);
            return true;
        }
    }

    if (error && error->empty()) {
        *error = "Failed to connect to any address";
    }
    Logging::NetworkLogger::Instance().LogConnectionFailure(host, port, *error);
    return false;
}

void TcpClientSocket::Disconnect() {
    // ~SocketStream closes fd_; only close it directly if no stream exists.
    if (stream_) {
        stream_.reset();
    } else if (fd_ >= 0) {
#ifdef _WIN32
        closesocket(fd_);
#else
        close(fd_);
#endif
    }
    fd_ = -1;
}

bool TcpClientSocket::IsConnected() const {
    return fd_ >= 0;
}

int TcpClientSocket::Read(uint8_t* buf, size_t len, std::string* error) {
    auto& platform = Platform::SocketPlatform::GetInstance();
    Platform::SocketHandle socket =
        Platform::SocketHandle::Borrowed(static_cast<uintptr_t>(fd_));

    int result = platform.Recv(socket, buf, len, error);
    return result;
}

int TcpClientSocket::Write(const uint8_t* buf, size_t len, std::string* error) {
    auto& platform = Platform::SocketPlatform::GetInstance();
    Platform::SocketHandle socket =
        Platform::SocketHandle::Borrowed(static_cast<uintptr_t>(fd_));

    int result = platform.Send(socket, buf, len, error);
    return result;
}

void TcpClientSocket::ReadAsync(uint8_t* buf, size_t len,
                                 Transport::CompletionCallback cb) {
    Async::AsyncOperationManager::Instance().Enqueue(
        [this, buf, len, cb = std::move(cb)]() {
            std::string error;
            int result = Read(buf, len, &error);
            cb(result, error);
        });
}

void TcpClientSocket::WriteAsync(const uint8_t* buf, size_t len,
                                  Transport::CompletionCallback cb) {
    std::vector<uint8_t> data(buf, buf + len);
    Async::AsyncOperationManager::Instance().Enqueue(
        [this, data = std::move(data), cb = std::move(cb)]() mutable {
            std::string error;
            int result = Write(data.data(), data.size(), &error);
            cb(result, error);
        });
}

}  // namespace XER::Network::Socket
