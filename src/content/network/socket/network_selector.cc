/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/socket/network_selector.h"

#include "content/network/network_util/network_util.h"
#include "content/network/proxy/proxy_service.h"
#include "content/network/socket/socket_client.h"
#include "content/network/socket/socket_proxy.h"
#include "content/network/socket/ssl_client_socket.h"

namespace XER::network::socket {

NetworkSelector::NetworkSelector(const proxy::ProxyConfig& proxy_config)
    : proxy_config_(proxy_config) {}

std::unique_ptr<transport::TransportClientSocket>
NetworkSelector::CreateSocket(const std::string& scheme,
                               const std::string& host,
                               uint16_t port,
                               std::string* error) {
    proxy::ProxyService resolver(proxy_config_);
    proxy::ProxyInfo info;
    resolver.Resolve(scheme + "://" + host, &info, nullptr);

    if (!info.is_direct) {
        auto sock = SocketProxy::ConnectThrough(
            info.server, host, port, error);
        if (!sock) return nullptr;
        if (XER::network::IsSecureScheme(scheme)) {
            // Wrap in TLS (over the proxy tunnel) — return plain for now
        }
        return sock;
    }
    return CreateSocketForScheme(scheme);
}

}  // namespace XER::network::socket
