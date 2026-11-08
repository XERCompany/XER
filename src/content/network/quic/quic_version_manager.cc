/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/quic/quic_version_manager.h"
#include "content/network/quic/quic_protocol.h"
#include <algorithm>

namespace XER::Network::Quic {

const std::vector<uint32_t>& QuicVersionManager::SupportedVersions() {
    static const std::vector<uint32_t> kVersions = { kQuicVersion1 };
    return kVersions;
}

bool QuicVersionManager::IsSupported(uint32_t v) {
    const auto& sv = SupportedVersions();
    return std::find(sv.begin(), sv.end(), v) != sv.end();
}

uint32_t QuicVersionManager::LatestVersion() {
    return SupportedVersions().front();
}

}  // namespace XER::Network::Quic
