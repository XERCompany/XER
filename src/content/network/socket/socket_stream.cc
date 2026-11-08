/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/socket/socket_stream.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <cstring>

#include "content/network/socket/net_log.h"

namespace XER::Network::Socket {

namespace {

#ifdef _WIN32
using RecvRet = int;
using SendRet = int;
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
using RecvRet = ssize_t;
using SendRet = ssize_t;
constexpr int kNoSignal = MSG_NOSIGNAL;
inline int CloseSocket(int fd) { return ::close(fd); }
inline int SocketErrno() { return errno; }
inline std::string ErrnoStr(int e) { return std::strerror(e); }
#endif

}  // namespace

SocketStream::SocketStream(int fd) : fd_(fd) {}
SocketStream::~SocketStream() { if (fd_ >= 0) CloseSocket(fd_); }

bool SocketStream::ReadExact(std::vector<uint8_t>* buf,
                              size_t n,
                              std::string* error) {
    buf->resize(n);
    size_t got = 0;
    while (got < n) {
        const RecvRet r = ::recv(fd_, reinterpret_cast<char*>(buf->data() + got),
                                 static_cast<int>(n - got), MSG_WAITALL);
        if (r <= 0) {
            if (error) *error = r == 0 ? "connection closed" : ErrnoStr(SocketErrno());
            return false;
        }
        got += static_cast<size_t>(r);
    }
    return true;
}

bool SocketStream::ReadLine(std::string* line, std::string* error) {
    line->clear();
    char c;
    while (true) {
        const RecvRet r = ::recv(fd_, &c, 1, 0);
        if (r == 0) { if (error) *error = "connection closed"; return false; }
        if (r < 0)  { if (error) *error = ErrnoStr(SocketErrno()); return false; }
        line->push_back(c);
        if (line->size() >= 2 &&
            (*line)[line->size()-2] == '\r' &&
            (*line)[line->size()-1] == '\n') break;
    }
    return true;
}

bool SocketStream::WriteAll(const uint8_t* data, size_t len, std::string* error) {
    size_t sent = 0;
    while (sent < len) {
        const SendRet r = ::send(fd_, reinterpret_cast<const char*>(data + sent),
                                 static_cast<int>(len - sent), kNoSignal);
        if (r <= 0) {
            if (error) *error = ErrnoStr(SocketErrno());
            return false;
        }
        sent += static_cast<size_t>(r);
    }
    return true;
}

bool SocketStream::WriteAll(const std::string& data, std::string* error) {
    return WriteAll(reinterpret_cast<const uint8_t*>(data.data()),
                    data.size(), error);
}

}  // namespace XER::Network::Socket
