/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>

namespace XER::network::quic {

struct QuicServerId {
    std::string host;
    uint16_t    port        = 443;
    bool        privacy_mode = false;

    std::string ToString() const;
    bool operator==(const QuicServerId& other) const;
    bool operator<(const QuicServerId& other) const;
};

}  // namespace XER::network::quic
