/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "content/network/socket/tcp_client_socket.h"

namespace network::socket {

class TcpClientSocket;  // forward declaration

// Accept callback type: (client_socket, error_message)
using AcceptCallback = std::function<void(std::unique_ptr<TcpClientSocket>, const std::string&)>;

// TCP server socket for accepting incoming connections.
// Manages server socket lifecycle (listen, accept, close).
//
// Usage:
//   SocketServer server;
//   std::string error;
//   if (server.Listen("0.0.0.0", 8080, 5, &error)) {
//       auto client = server.Accept(&error);
//       if (client) {
//           // ... use client socket ...
//       }
//   }
class SocketServer {
public:
    SocketServer() = default;
    ~SocketServer();

    // Start listening for incoming TCP connections.
    // @param address  IP address to bind to ("0.0.0.0" for all interfaces)
    // @param port     Port number to listen on
    // @param backlog  Maximum pending connections
    // @param error    (out) Error message if listen fails
    // @return         True on success, false on error
    bool Listen(const std::string& address, uint16_t port,
                int backlog, std::string* error);

    // Accept an incoming connection (blocking).
    // @param error    (out) Error message if accept fails
    // @return         Connected TcpClientSocket, or nullptr on error
    std::unique_ptr<TcpClientSocket> Accept(std::string* error);

    // Accept an incoming connection asynchronously.
    // @param cb       Callback invoked with client socket or error
    void AcceptAsync(AcceptCallback cb);

    // Close the server socket.
    void Close();

    // Check if the server is currently listening.
    bool IsListening() const;

    // Get the listen address and port
    const std::string& GetListenAddress() const;
    uint16_t GetListenPort() const;

private:
    int fd_ = -1;
    std::string listen_address_;
    uint16_t listen_port_ = 0;
};

}  // namespace network::socket
