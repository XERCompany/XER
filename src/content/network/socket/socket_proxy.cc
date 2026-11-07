/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/socket/socket_proxy.h"

#include "content/network/proxy/socks_client_socket.h"

namespace XER::network::socket {

// static
std::unique_ptr<TcpClientSocket>
SocketProxy::ConnectThrough(const proxy::ProxyServer& proxy,
                             const std::string& target_host,
                             uint16_t target_port,
                             std::string* error) {
    auto sock = std::make_unique<TcpClientSocket>();
    if (!sock->Connect(proxy.host, proxy.port, error)) return nullptr;

    if (proxy.type == proxy::ProxyType::kHttp ||
        proxy.type == proxy::ProxyType::kHttps) {
        // HTTP CONNECT tunnel
        const std::string req =
            "CONNECT " + target_host + ":" + std::to_string(target_port) +
            " HTTP/1.1\r\nHost: " + target_host + "\r\n\r\n";
        std::string err;
        sock->Write(reinterpret_cast<const uint8_t*>(req.data()),
                    req.size(), &err);
        // Read response line
        uint8_t buf[256]{};
        sock->Read(buf, sizeof(buf) - 1, &err);
        const std::string resp(reinterpret_cast<char*>(buf));
        if (resp.find("200") == std::string::npos) {
            if (error) *error = "HTTP CONNECT failed: " + resp;
            return nullptr;
        }
    } else if (proxy.type == proxy::ProxyType::kSocks4 ||
               proxy.type == proxy::ProxyType::kSocks5) {
        proxy::SocksClientSocket socks(sock->stream()->fd(), proxy);
        if (!socks.Handshake(target_host, target_port, error)) return nullptr;
    }
    return sock;
}

}  // namespace XER::network::socket
