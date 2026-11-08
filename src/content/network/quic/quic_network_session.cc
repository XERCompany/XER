/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/quic/quic_network_session.h"

namespace XER::Network::Quic {

QuicNetworkSession& QuicNetworkSession::Instance() {
    static QuicNetworkSession inst;
    return inst;
}

QuicConnection* QuicNetworkSession::CreateConnection(const QuicServerId& server) {
    std::lock_guard<std::mutex> lk(mu_);
    const std::string key = server.ToString();
    auto conn = std::make_unique<QuicConnection>(next_id_++, server.host, server.port);
    QuicConnection* ptr = conn.get();
    connections_[key] = std::move(conn);
    return ptr;
}

QuicConnection* QuicNetworkSession::FindConnection(const QuicServerId& server) {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = connections_.find(server.ToString());
    return it != connections_.end() ? it->second.get() : nullptr;
}

void QuicNetworkSession::CloseConnection(const QuicServerId& server) {
    std::lock_guard<std::mutex> lk(mu_);
    connections_.erase(server.ToString());
}

void QuicNetworkSession::CloseAll() {
    std::lock_guard<std::mutex> lk(mu_);
    connections_.clear();
}

}  // namespace XER::Network::Quic
