/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/core/network_service_util.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace XER::Network::Core {

bool HasNetworkAccess() {
    // Try a UDP "connect" to a public IP (no actual packet sent).
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    struct sockaddr_in sin{};
    sin.sin_family      = AF_INET;
    sin.sin_port        = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &sin.sin_addr);
    const bool ok = ::connect(fd, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) == 0;
#ifdef _WIN32
    ::closesocket(fd);
#else
    ::close(fd);
#endif
    return ok;
}

bool ResolvePublicIPv4(const std::string& host, std::string* ip) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    char buf[INET_ADDRSTRLEN];
    auto* sa = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
    inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
    if (ip) *ip = buf;
    freeaddrinfo(res);
    return true;
}

std::string FormatEndpoint(const std::string& host, uint16_t port) {
    return host + ":" + std::to_string(port);
}

}  // namespace XER::Network::Core
