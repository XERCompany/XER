/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

namespace network::socket::platform {

// Address information structure
struct AddressInfo {
    std::string ip_address;
    uint16_t port;
    bool is_ipv6;
};

// Socket handle wrapper
class SocketHandle {
public:
    SocketHandle();
    explicit SocketHandle(uintptr_t handle);
    ~SocketHandle();

    SocketHandle(SocketHandle&& other) noexcept;
    SocketHandle& operator=(SocketHandle&& other) noexcept;

    // Disable copy
    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    bool IsValid() const;
    void Close();
    uintptr_t Get() const { return handle_; }

    // Detach the handle (won't be closed by destructor)
    void Detach() { handle_ = reinterpret_cast<uintptr_t>(GetInvalidValue()); }

private:
    static uintptr_t GetInvalidValue();
    uintptr_t handle_;
};

// Platform-specific socket operations
class SocketPlatform {
public:
    static SocketPlatform& GetInstance();

    // Initialize socket library (WSAStartup on Windows)
    bool Initialize(std::string* error);
    void Shutdown();

    // Address resolution
    bool ResolveAddress(const std::string& host,
                       uint16_t port,
                       std::vector<AddressInfo>* addresses,
                       std::string* error);

    // Socket creation
    SocketHandle CreateTcpSocket(bool ipv6, std::string* error);
    SocketHandle CreateUdpSocket(bool ipv6, std::string* error);

    // Connection operations
    bool Connect(SocketHandle& socket,
                const AddressInfo& address,
                std::string* error);

    // Server operations
    bool Bind(SocketHandle& socket,
             const std::string& ip,
             uint16_t port,
             std::string* error);
    bool Listen(SocketHandle& socket, int backlog, std::string* error);
    SocketHandle Accept(SocketHandle& server_socket,
                        std::string* client_ip,
                        uint16_t* client_port,
                        std::string* error);

    // I/O operations
    int Send(SocketHandle& socket,
            const uint8_t* buf,
            size_t len,
            std::string* error);
    int Recv(SocketHandle& socket,
            uint8_t* buf,
            size_t len,
            std::string* error);

    // UDP operations
    int SendTo(SocketHandle& socket,
              const uint8_t* buf,
              size_t len,
              const std::string& ip,
              uint16_t port,
              std::string* error);
    int RecvFrom(SocketHandle& socket,
                uint8_t* buf,
                size_t len,
                std::string* from_ip,
                uint16_t* from_port,
                std::string* error);

    // Socket options
    bool SetOption(SocketHandle& socket,
                 int level,
                 int optname,
                 const void* optval,
                 int optlen,
                 std::string* error);
    bool SetNonBlocking(SocketHandle& socket, bool enable, std::string* error);

    // Error handling
    std::string GetLastError();
    bool WouldBlock();

    // Poll/select for async I/O
    bool Poll(SocketHandle& socket,
            bool check_read,
            bool check_write,
            int timeout_ms,
            bool* can_read,
            bool* can_write,
            std::string* error);

private:
    SocketPlatform() = default;
    ~SocketPlatform() = default;

    SocketPlatform(const SocketPlatform&) = delete;
    SocketPlatform& operator=(const SocketPlatform&) = delete;
};

} // namespace network::socket::platform
