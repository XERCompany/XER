/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace network {

namespace transport {

// Completion callback: (bytes_transferred_or_error_code, error_message)
using CompletionCallback = std::function<void(int, const std::string&)>;

// Abstract client socket interface.
// Provides cross-platform async I/O support via platform-specific implementations.
class TransportClientSocket {
public:
    virtual ~TransportClientSocket() = default;

    // Connection operations
    virtual bool Connect(const std::string& host,
                         uint16_t port,
                         std::string* error) = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;

    // Synchronous I/O
    virtual int Read(uint8_t* buf, size_t len, std::string* error) = 0;
    virtual int Write(const uint8_t* buf, size_t len, std::string* error) = 0;

    // Asynchronous I/O (platform-specific: IOCP on Windows, epoll on Linux)
    virtual void ReadAsync(uint8_t* buf, size_t len, CompletionCallback cb) = 0;
    virtual void WriteAsync(const uint8_t* buf, size_t len, CompletionCallback cb) = 0;

    // Get socket file descriptor (for platform-specific operations)
    virtual int GetFd() const = 0;
};

}  // namespace transport

}  // namespace network
