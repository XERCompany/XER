/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/transport/transport_network_session.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstring>

namespace XER::Network::Transport {

namespace {

#ifdef _WIN32
using IORecv = int;
using IOSend = int;
constexpr int kNoSignal = 0;
inline int CloseSocket(int fd) { return ::closesocket(fd); }
inline int SocketErrno() { return ::WSAGetLastError(); }
inline std::string ErrnoStr(int e) {
    char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr, static_cast<DWORD>(e), 0, buf, sizeof(buf), nullptr);
    return std::string(buf);
}
#else
using IORecv = ssize_t;
using IOSend = ssize_t;
constexpr int kNoSignal = MSG_NOSIGNAL;
inline int CloseSocket(int fd) { return ::close(fd); }
inline int SocketErrno() { return errno; }
inline std::string ErrnoStr(int e) { return std::strerror(e); }
#endif

}  // namespace

TransportNetworkSession::TransportNetworkSession(
    TransportSessionParams params,
    TransportNetworkDelegate* delegate)
    : params_(std::move(params)), delegate_(delegate) {}

TransportNetworkSession::~TransportNetworkSession() {
    Close();
}

bool TransportNetworkSession::Open(std::string* error) {
    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    const std::string port_str = std::to_string(params_.port);
    const int rc = ::getaddrinfo(params_.host.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0) {
        if (error) *error = ::gai_strerror(rc);
        return false;
    }

    int fd = -1;
    for (auto* p = res; p; p = p->ai_next) {
        fd = static_cast<int>(::socket(p->ai_family, p->ai_socktype, p->ai_protocol));
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) break;
        CloseSocket(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);

    if (fd < 0) {
        if (error) *error = "connect failed: " + ErrnoStr(SocketErrno());
        return false;
    }
    fd_ = fd;
    if (delegate_) delegate_->OnConnected(params_.host, params_.port);
    return true;
}

void TransportNetworkSession::Close() {
    if (fd_ >= 0) {
        CloseSocket(fd_);
        if (delegate_) delegate_->OnDisconnected(params_.host, params_.port);
        fd_ = -1;
    }
}

bool TransportNetworkSession::IsOpen() const { return fd_ >= 0; }

int TransportNetworkSession::Read(uint8_t* buf, size_t len, std::string* error) {
    const IORecv n = ::recv(fd_, reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
    if (n < 0) {
        if (error) *error = ErrnoStr(SocketErrno());
        return -1;
    }
    if (delegate_) delegate_->OnBytesReceived(static_cast<size_t>(n));
    return static_cast<int>(n);
}

int TransportNetworkSession::Write(const uint8_t* buf, size_t len, std::string* error) {
    const IOSend n = ::send(fd_, reinterpret_cast<const char*>(buf),
                             static_cast<int>(len), kNoSignal);
    if (n < 0) {
        if (error) *error = ErrnoStr(SocketErrno());
        return -1;
    }
    if (delegate_) delegate_->OnBytesSent(static_cast<size_t>(n));
    return static_cast<int>(n);
}

}  // namespace XER::Network::Transport
