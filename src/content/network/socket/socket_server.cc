/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/socket/socket_server.h"

#include "content/network/async/async_operation_manager.h"
#include "content/network/logging/network_logger.h"
#include "content/network/socket/platform/socket_platform.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace XER::Network::Socket {

SocketServer::~SocketServer() {
    Close();
}

bool SocketServer::Listen(const std::string& address, uint16_t port,
                          int backlog, std::string* error) {
    auto& platform = Platform::SocketPlatform::GetInstance();

    // Create TCP socket
    auto socket = platform.CreateTcpSocket(false, error);
    if (!socket.IsValid()) {
        return false;
    }

    // Set SO_REUSEADDR
    int opt = 1;
    platform.SetOption(socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt), error);

    // Bind to address
    std::string bind_addr = address.empty() ? "0.0.0.0" : address;
    if (!platform.Bind(socket, bind_addr, port, error)) {
        return false;
    }

    // Listen
    if (!platform.Listen(socket, backlog, error)) {
        return false;
    }

    fd_ = static_cast<int>(socket.Get());
    socket.Detach();  // Prevent auto-close

    listen_address_ = bind_addr;
    listen_port_ = port;

    Logging::NetworkLogger::Instance().LogInfo("socket_server",
        "Listening on " + bind_addr + ":" + std::to_string(port));

    return true;
}

std::unique_ptr<TcpClientSocket> SocketServer::Accept(std::string* error) {
    auto& platform = Platform::SocketPlatform::GetInstance();
    Platform::SocketHandle server_socket =
        Platform::SocketHandle::Borrowed(static_cast<uintptr_t>(fd_));

    if (!server_socket.IsValid()) {
        if (error) *error = "Server socket not listening";
        return nullptr;
    }

    std::string client_ip;
    uint16_t client_port = 0;

    auto client_socket = platform.Accept(server_socket, &client_ip, &client_port, error);
    if (!client_socket.IsValid()) {
        return nullptr;
    }

    Logging::NetworkLogger::Instance().LogInfo("socket_server",
        "Accepted connection from " + client_ip + ":" + std::to_string(client_port));

    // Create TcpClientSocket with the accepted fd
    int client_fd = static_cast<int>(client_socket.Get());
    client_socket.Detach();  // Prevent auto-close

    return std::make_unique<TcpClientSocket>(client_fd);
}

void SocketServer::AcceptAsync(AcceptCallback cb) {
    Async::AsyncOperationManager::Instance().Enqueue(
        [this, cb = std::move(cb)]() {
            std::string error;
            auto client_socket = Accept(&error);
            cb(std::move(client_socket), error);
        });
}

void SocketServer::Close() {
    if (fd_ >= 0) {
#ifdef _WIN32
        closesocket(fd_);
#else
        close(fd_);
#endif
        fd_ = -1;
    }
    listen_address_.clear();
    listen_port_ = 0;
}

bool SocketServer::IsListening() const {
    return fd_ >= 0;
}

const std::string& SocketServer::GetListenAddress() const {
    return listen_address_;
}

uint16_t SocketServer::GetListenPort() const {
    return listen_port_;
}

}  // namespace XER::Network::Socket
