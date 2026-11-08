/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "content/network/quic/quic_connection.h"
#include "content/network/quic/quic_server_id.h"

namespace XER::Network::Quic {

class QuicNetworkSession {
public:
    static QuicNetworkSession& Instance();

    QuicConnection* CreateConnection(const QuicServerId& server);
    QuicConnection* FindConnection(const QuicServerId& server);
    void            CloseConnection(const QuicServerId& server);
    void            CloseAll();

private:
    QuicNetworkSession() = default;
    std::mutex mu_;
    std::map<std::string, std::unique_ptr<QuicConnection>> connections_;
    QuicConnectionId next_id_ = 1;
};

}  // namespace XER::Network::Quic
