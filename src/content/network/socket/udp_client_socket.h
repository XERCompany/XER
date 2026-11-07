/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace XER::network::socket {

// UDP callback type: (bytes_sent/received, error_message)
using UdpCallback = std::function<void(int, const std::string&)>;

class UdpClientSocket {
public:
    UdpClientSocket();
    ~UdpClientSocket();

    bool Bind(uint16_t local_port, std::string* error);
    bool Connect(const std::string& host, uint16_t port, std::string* error);
    void Close();
    bool IsOpen() const { return fd_ >= 0; }

    // Synchronous operations
    int SendTo(const uint8_t* buf, size_t len,
               const std::string& host, uint16_t port, std::string* error);
    int RecvFrom(std::vector<uint8_t>* buf,
                 std::string* from_host, uint16_t* from_port,
                 std::string* error);

    // Asynchronous operations
    void SendToAsync(const uint8_t* buf, size_t len,
                    const std::string& host, uint16_t port,
                    UdpCallback cb);
    void RecvFromAsync(UdpCallback cb);

private:
    int fd_ = -1;
    std::string connected_host_;
    uint16_t connected_port_ = 0;
};

}  // namespace XER::network::socket
