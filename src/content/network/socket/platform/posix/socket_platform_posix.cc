/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "../socket_platform.h"

#ifndef _WIN32

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

namespace XER::Network {
namespace Socket {
namespace Platform {

// SocketHandle implementation for POSIX
SocketHandle::SocketHandle() : handle_(static_cast<uintptr_t>(-1)) {}

SocketHandle::SocketHandle(uintptr_t handle) : handle_(handle) {}

SocketHandle::~SocketHandle() {
    Close();
}

SocketHandle::SocketHandle(SocketHandle&& other) noexcept
    : handle_(other.handle_) {
    other.handle_ = static_cast<uintptr_t>(-1);
}

SocketHandle& SocketHandle::operator=(SocketHandle&& other) noexcept {
    if (this != &other) {
        Close();
        handle_ = other.handle_;
        other.handle_ = static_cast<uintptr_t>(-1);
    }
    return *this;
}

uintptr_t SocketHandle::GetInvalidValue() {
    return static_cast<uintptr_t>(-1);
}

bool SocketHandle::IsValid() const {
    return handle_ != static_cast<uintptr_t>(-1);
}

void SocketHandle::Close() {
    if (IsValid()) {
        close(static_cast<int>(handle_));
        handle_ = static_cast<uintptr_t>(-1);
    }
}

// SocketPlatform implementation for POSIX
bool SocketPlatform::Initialize(std::string* error) {
    // No initialization needed on POSIX
    return true;
}

void SocketPlatform::Shutdown() {
    // No shutdown needed on POSIX
}

bool SocketPlatform::ResolveAddress(const std::string& host,
                                      uint16_t port,
                                      std::vector<AddressInfo>* addresses,
                                      std::string* error) {
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const std::string port_str = std::to_string(port);
    const int rc = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
    if (rc != 0) {
        if (error) *error = gai_strerror(rc);
        return false;
    }

    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        AddressInfo info;
        info.port = port;
        info.is_ipv6 = (ptr->ai_family == AF_INET6);

        char ip_str[INET6_ADDRSTRLEN] = {};
        if (ptr->ai_family == AF_INET) {
            sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(ptr->ai_addr);
            inet_ntop(AF_INET, &ipv4->sin_addr, ip_str, sizeof(ip_str));
        } else if (ptr->ai_family == AF_INET6) {
            sockaddr_in6* ipv6 = reinterpret_cast<sockaddr_in6*>(ptr->ai_addr);
            inet_ntop(AF_INET6, &ipv6->sin6_addr, ip_str, sizeof(ip_str));
        }
        info.ip_address = ip_str;
        addresses->push_back(info);
    }

    freeaddrinfo(result);
    return !addresses->empty();
}

SocketHandle SocketPlatform::CreateTcpSocket(bool ipv6, std::string* error) {
    int sock = socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        if (error) *error = strerror(errno);
        return SocketHandle();
    }
    return SocketHandle(static_cast<uintptr_t>(sock));
}

SocketHandle SocketPlatform::CreateUdpSocket(bool ipv6, std::string* error) {
    int sock = socket(ipv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        if (error) *error = strerror(errno);
        return SocketHandle();
    }
    return SocketHandle(static_cast<uintptr_t>(sock));
}

bool SocketPlatform::Connect(SocketHandle& socket,
                              const AddressInfo& address,
                              std::string* error) {
    if (!socket.IsValid()) {
        if (error) *error = "Invalid socket handle";
        return false;
    }

    sockaddr_storage addr = {};
    int addr_len = 0;

    if (address.is_ipv6) {
        sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(&addr);
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(address.port);
        inet_pton(AF_INET6, address.ip_address.c_str(), &addr6->sin6_addr);
        addr_len = sizeof(sockaddr_in6);
    } else {
        sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&addr);
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(address.port);
        inet_pton(AF_INET, address.ip_address.c_str(), &addr4->sin_addr);
        addr_len = sizeof(sockaddr_in);
    }

    const int rc = connect(static_cast<int>(socket.Get()),
                           reinterpret_cast<sockaddr*>(&addr),
                           addr_len);
    if (rc < 0) {
        if (error) *error = strerror(errno);
        return false;
    }
    return true;
}

bool SocketPlatform::Bind(SocketHandle& socket,
                          const std::string& ip,
                          uint16_t port,
                          std::string* error) {
    if (!socket.IsValid()) {
        if (error) *error = "Invalid socket handle";
        return false;
    }

    sockaddr_storage addr = {};
    int addr_len = 0;

    if (ip.empty() || ip == "0.0.0.0" || ip == "::") {
        // Bind to any address
        sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&addr);
        addr4->sin_family = AF_INET;
        addr4->sin_addr.s_addr = INADDR_ANY;
        addr4->sin_port = htons(port);
        addr_len = sizeof(sockaddr_in);
    } else {
        // Check if IPv6
        if (ip.find(':') != std::string::npos) {
            sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(&addr);
            addr6->sin6_family = AF_INET6;
            addr6->sin6_port = htons(port);
            inet_pton(AF_INET6, ip.c_str(), &addr6->sin6_addr);
            addr_len = sizeof(sockaddr_in6);
        } else {
            sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&addr);
            addr4->sin_family = AF_INET;
            addr4->sin_port = htons(port);
            inet_pton(AF_INET, ip.c_str(), &addr4->sin_addr);
            addr_len = sizeof(sockaddr_in);
        }
    }

    const int rc = bind(static_cast<int>(socket.Get()),
                        reinterpret_cast<sockaddr*>(&addr),
                        addr_len);
    if (rc < 0) {
        if (error) *error = strerror(errno);
        return false;
    }
    return true;
}

bool SocketPlatform::Listen(SocketHandle& socket, int backlog, std::string* error) {
    if (!socket.IsValid()) {
        if (error) *error = "Invalid socket handle";
        return false;
    }

    const int rc = listen(static_cast<int>(socket.Get()), backlog);
    if (rc < 0) {
        if (error) *error = strerror(errno);
        return false;
    }
    return true;
}

SocketHandle SocketPlatform::Accept(SocketHandle& server_socket,
                                    std::string* client_ip,
                                    uint16_t* client_port,
                                    std::string* error) {
    if (!server_socket.IsValid()) {
        if (error) *error = "Invalid server socket handle";
        return SocketHandle();
    }

    sockaddr_storage client_addr = {};
    socklen_t addr_len = sizeof(client_addr);

    int client_sock = accept(static_cast<int>(server_socket.Get()),
                             reinterpret_cast<sockaddr*>(&client_addr),
                             &addr_len);
    if (client_sock < 0) {
        if (error) *error = strerror(errno);
        return SocketHandle();
    }

    if (client_ip && client_port) {
        char ip_str[INET6_ADDRSTRLEN] = {};
        if (client_addr.ss_family == AF_INET) {
            sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&client_addr);
            inet_ntop(AF_INET, &addr4->sin_addr, ip_str, sizeof(ip_str));
            *client_port = ntohs(addr4->sin_port);
        } else if (client_addr.ss_family == AF_INET6) {
            sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(&client_addr);
            inet_ntop(AF_INET6, &addr6->sin6_addr, ip_str, sizeof(ip_str));
            *client_port = ntohs(addr6->sin6_port);
        }
        *client_ip = ip_str;
    }

    return SocketHandle(static_cast<uintptr_t>(client_sock));
}

int SocketPlatform::Send(SocketHandle& socket,
                         const uint8_t* buf,
                         size_t len,
                         std::string* error) {
    if (!socket.IsValid()) {
        if (error) *error = "Invalid socket handle";
        return -1;
    }

    const ssize_t rc = send(static_cast<int>(socket.Get()),
                           buf,
                           len,
                           0);
    if (rc < 0) {
        if (error) *error = strerror(errno);
        return -1;
    }
    return static_cast<int>(rc);
}

int SocketPlatform::Recv(SocketHandle& socket,
                         uint8_t* buf,
                         size_t len,
                         std::string* error) {
    if (!socket.IsValid()) {
        if (error) *error = "Invalid socket handle";
        return -1;
    }

    const ssize_t rc = recv(static_cast<int>(socket.Get()),
                           buf,
                           len,
                           0);
    if (rc < 0) {
        if (error) *error = strerror(errno);
        return -1;
    }
    return static_cast<int>(rc);
}

int SocketPlatform::SendTo(SocketHandle& socket,
                           const uint8_t* buf,
                           size_t len,
                           const std::string& ip,
                           uint16_t port,
                           std::string* error) {
    if (!socket.IsValid()) {
        if (error) *error = "Invalid socket handle";
        return -1;
    }

    sockaddr_storage addr = {};
    int addr_len = 0;

    if (ip.find(':') != std::string::npos) {
        sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(&addr);
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(port);
        inet_pton(AF_INET6, ip.c_str(), &addr6->sin6_addr);
        addr_len = sizeof(sockaddr_in6);
    } else {
        sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&addr);
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr4->sin_addr);
        addr_len = sizeof(sockaddr_in);
    }

    const ssize_t rc = sendto(static_cast<int>(socket.Get()),
                              buf,
                              len,
                              0,
                              reinterpret_cast<sockaddr*>(&addr),
                              addr_len);
    if (rc < 0) {
        if (error) *error = strerror(errno);
        return -1;
    }
    return static_cast<int>(rc);
}

int SocketPlatform::RecvFrom(SocketHandle& socket,
                              uint8_t* buf,
                              size_t len,
                              std::string* from_ip,
                              uint16_t* from_port,
                              std::string* error) {
    if (!socket.IsValid()) {
        if (error) *error = "Invalid socket handle";
        return -1;
    }

    sockaddr_storage from_addr = {};
    socklen_t addr_len = sizeof(from_addr);

    const ssize_t rc = recvfrom(static_cast<int>(socket.Get()),
                                buf,
                                len,
                                0,
                                reinterpret_cast<sockaddr*>(&from_addr),
                                &addr_len);
    if (rc < 0) {
        if (error) *error = strerror(errno);
        return -1;
    }

    if (from_ip && from_port) {
        char ip_str[INET6_ADDRSTRLEN] = {};
        if (from_addr.ss_family == AF_INET) {
            sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&from_addr);
            inet_ntop(AF_INET, &addr4->sin_addr, ip_str, sizeof(ip_str));
            *from_port = ntohs(addr4->sin_port);
        } else if (from_addr.ss_family == AF_INET6) {
            sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(&from_addr);
            inet_ntop(AF_INET6, &addr6->sin6_addr, ip_str, sizeof(ip_str));
            *from_port = ntohs(addr6->sin6_port);
        }
        *from_ip = ip_str;
    }

    return static_cast<int>(rc);
}

bool SocketPlatform::SetOption(SocketHandle& socket,
                               int level,
                               int optname,
                               const void* optval,
                               int optlen,
                               std::string* error) {
    if (!socket.IsValid()) {
        if (error) *error = "Invalid socket handle";
        return false;
    }

    const int rc = setsockopt(static_cast<int>(socket.Get()),
                              level,
                              optname,
                              optval,
                              optlen);
    if (rc < 0) {
        if (error) *error = strerror(errno);
        return false;
    }
    return true;
}

bool SocketPlatform::SetNonBlocking(SocketHandle& socket, bool enable, std::string* error) {
    if (!socket.IsValid()) {
        if (error) *error = "Invalid socket handle";
        return false;
    }

    int flags = fcntl(static_cast<int>(socket.Get()), F_GETFL, 0);
    if (flags < 0) {
        if (error) *error = strerror(errno);
        return false;
    }

    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(static_cast<int>(socket.Get()), F_SETFL, flags) < 0) {
        if (error) *error = strerror(errno);
        return false;
    }
    return true;
}

std::string SocketPlatform::GetLastError() {
    return strerror(errno);
}

bool SocketPlatform::WouldBlock() {
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

bool SocketPlatform::Poll(SocketHandle& socket,
                          bool check_read,
                          bool check_write,
                          int timeout_ms,
                          bool* can_read,
                          bool* can_write,
                          std::string* error) {
    if (!socket.IsValid()) {
        if (error) *error = "Invalid socket handle";
        return false;
    }

    fd_set read_fds, write_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    int fd = static_cast<int>(socket.Get());
    if (check_read) FD_SET(fd, &read_fds);
    if (check_write) FD_SET(fd, &write_fds);

    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int rc = select(fd + 1,
                   check_read ? &read_fds : nullptr,
                   check_write ? &write_fds : nullptr,
                   nullptr,
                   &tv);

    if (rc < 0) {
        if (error) *error = strerror(errno);
        return false;
    }

    if (can_read) *can_read = FD_ISSET(fd, &read_fds) != 0;
    if (can_write) *can_write = FD_ISSET(fd, &write_fds) != 0;
    return true;
}

SocketPlatform& SocketPlatform::GetInstance() {
    static SocketPlatform instance;
    return instance;
}

}  // namespace Platform
}  // namespace Socket
}  // namespace Network

#endif // !_WIN32
