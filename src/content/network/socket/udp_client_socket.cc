/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/socket/udp_client_socket.h"

#include "content/network/async/async_operation_manager.h"
#include "content/network/logging/network_logger.h"
#include "content/network/socket/platform/socket_platform.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace XER::Network::Socket {

UdpClientSocket::UdpClientSocket() {
}

UdpClientSocket::~UdpClientSocket() {
    Close();
}

bool UdpClientSocket::Bind(uint16_t local_port, std::string* error) {
    auto& platform = Platform::SocketPlatform::GetInstance();

    // Create UDP socket
    auto socket = platform.CreateUdpSocket(false, error);
    if (!socket.IsValid()) {
        return false;
    }

    // Bind to local port
    if (!platform.Bind(socket, "0.0.0.0", local_port, error)) {
        return false;
    }

    fd_ = static_cast<int>(socket.Get());
    socket.Detach();  // Prevent auto-close

    Logging::NetworkLogger::Instance().LogInfo("udp_socket",
        "Bound to port " + std::to_string(local_port));

    return true;
}

bool UdpClientSocket::Connect(const std::string& host, uint16_t port,
                               std::string* error) {
    auto& platform = Platform::SocketPlatform::GetInstance();

    // Resolve address
    std::vector<Platform::AddressInfo> addresses;
    if (!platform.ResolveAddress(host, port, &addresses, error)) {
        return false;
    }

    if (addresses.empty()) {
        if (error) *error = "No addresses resolved";
        return false;
    }

    // Create socket if not already created
    if (fd_ < 0) {
        auto socket = platform.CreateUdpSocket(addresses[0].is_ipv6, error);
        if (!socket.IsValid()) {
            return false;
        }
        fd_ = static_cast<int>(socket.Get());
        socket.Detach();
    }

    // For UDP, "connect" just sets the default destination
    // But we'll use sendto/recvfrom for full flexibility
    connected_host_ = host;
    connected_port_ = port;

    Logging::NetworkLogger::Instance().LogInfo("udp_socket",
        "Connected to " + host + ":" + std::to_string(port));

    return true;
}

void UdpClientSocket::Close() {
    if (fd_ >= 0) {
#ifdef _WIN32
        closesocket(fd_);
#else
        close(fd_);
#endif
        fd_ = -1;
    }
    connected_host_.clear();
    connected_port_ = 0;
}

int UdpClientSocket::SendTo(const uint8_t* buf, size_t len,
                             const std::string& host, uint16_t port,
                             std::string* error) {
    auto& platform = Platform::SocketPlatform::GetInstance();
    Platform::SocketHandle socket(static_cast<uintptr_t>(fd_));

    if (!socket.IsValid()) {
        if (error) *error = "Socket not created";
        return -1;
    }

    return platform.SendTo(socket, buf, len, host, port, error);
}

int UdpClientSocket::RecvFrom(std::vector<uint8_t>* buf,
                               std::string* from_host, uint16_t* from_port,
                               std::string* error) {
    auto& platform = Platform::SocketPlatform::GetInstance();
    Platform::SocketHandle socket(static_cast<uintptr_t>(fd_));

    if (!socket.IsValid()) {
        if (error) *error = "Socket not created";
        return -1;
    }

    buf->resize(65536);  // Max UDP packet size
    int result = platform.RecvFrom(socket, buf->data(), buf->size(),
                                   from_host, from_port, error);

    if (result > 0) {
        buf->resize(static_cast<size_t>(result));
    }

    return result;
}

void UdpClientSocket::SendToAsync(const uint8_t* buf, size_t len,
                                  const std::string& host, uint16_t port,
                                  UdpCallback cb) {
    std::vector<uint8_t> data(buf, buf + len);
    Async::AsyncOperationManager::Instance().Enqueue(
        [this, data = std::move(data), host, port, cb = std::move(cb)]() mutable {
            std::string error;
            int result = SendTo(data.data(), data.size(), host, port, &error);
            cb(result, error);
        });
}

void UdpClientSocket::RecvFromAsync(UdpCallback cb) {
    Async::AsyncOperationManager::Instance().Enqueue(
        [this, cb = std::move(cb)]() {
            std::vector<uint8_t> buf;
            std::string from_host;
            uint16_t from_port = 0;
            std::string error;
            int result = RecvFrom(&buf, &from_host, &from_port, &error);
            cb(result, error);
        });
}

}  // namespace XER::Network::Socket
