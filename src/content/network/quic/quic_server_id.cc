/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/quic/quic_server_id.h"

namespace XER::network::quic {

std::string QuicServerId::ToString() const {
    return host + ":" + std::to_string(port);
}

bool QuicServerId::operator==(const QuicServerId& o) const {
    return host == o.host && port == o.port;
}
bool QuicServerId::operator<(const QuicServerId& o) const {
    return host < o.host || (host == o.host && port < o.port);
}

}  // namespace XER::network::quic
