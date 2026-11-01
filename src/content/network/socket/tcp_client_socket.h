/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "content/network/transport/transport_client_socket.h"
#include "content/network/socket/socket_stream.h"

namespace network {
namespace socket {

class SocketServer;  // forward declaration

// TCP client socket implementation using POSIX socket API.
// Manages socket lifecycle (connect, read, write, disconnect) with both
// synchronous and asynchronous I/O operations.
// 
// Usage:
//   TcpClientSocket sock;
//   std::string error;
//   if (sock.Connect("example.com", 80, &error)) {
//       uint8_t buf[1024];
//       int n = sock.Read(buf, sizeof(buf), &error);
//       // ...
//       sock.Disconnect();
//   } else {
//       // Handle connection error
//   }
class TcpClientSocket : public transport::TransportClientSocket {
public:
    TcpClientSocket() = default;
    ~TcpClientSocket() override;

    // Connect to a remote TCP server.
    // @param host     Target hostname or IP address
    // @param port     Target port number
    // @param error    (out) Error message if connection fails
    // @return         True on success, false on error
    bool Connect(const std::string& host,
                 uint16_t port,
                 std::string* error) override;
    
    // Disconnect and close the underlying socket.
    void Disconnect() override;
    
    // Check if this socket is currently connected.
    bool IsConnected() const override;

    // Synchronously read data from the socket.
    // @param buf      Buffer to read into
    // @param len      Maximum bytes to read
    // @param error    (out) Error message if read fails
    // @return         Number of bytes read, -1 on error, 0 on EOF
    int Read(uint8_t* buf, size_t len, std::string* error) override;
    
    // Synchronously write data to the socket.
    // @param buf      Data to write
    // @param len      Number of bytes to write
    // @param error    (out) Error message if write fails
    // @return         Number of bytes written, -1 on error
    int Write(const uint8_t* buf, size_t len, std::string* error) override;

    // Asynchronously read data from the socket.
    // Operation is dispatched to the thread pool.
    void ReadAsync(uint8_t* buf, size_t len,
                   transport::CompletionCallback cb) override;
    
    // Asynchronously write data to the socket.
    // Operation is dispatched to the thread pool.
    void WriteAsync(const uint8_t* buf, size_t len,
                    transport::CompletionCallback cb) override;

    // Get the underlying stream wrapper (for advanced usage).
    SocketStream* stream() { return stream_.get(); }

    // Get socket file descriptor
    int GetFd() const override { return fd_; }

    // Constructor for injecting an existing file descriptor.
    // Package-internal use only (called by SocketServer::Accept()).
    explicit TcpClientSocket(int fd) : fd_(fd) {
        stream_ = std::make_unique<SocketStream>(fd_);
    }

private:
    int fd_ = -1;
    std::unique_ptr<SocketStream> stream_;
};

}  // namespace socket
}  // namespace network
