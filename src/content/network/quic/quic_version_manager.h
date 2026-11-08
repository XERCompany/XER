/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <vector>

namespace XER::Network::Quic {

class QuicVersionManager {
public:
    static const std::vector<uint32_t>& SupportedVersions();
    static bool IsSupported(uint32_t version);
    static uint32_t LatestVersion();
};

}  // namespace XER::Network::Quic
