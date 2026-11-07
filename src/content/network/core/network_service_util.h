/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include <cstdint>

namespace XER::network::core {

// Returns true if the current process has outbound network access.
bool HasNetworkAccess();

// Lookup the public IPv4 of a hostname (blocking).
bool ResolvePublicIPv4(const std::string& host, std::string* ip);

// Format a network endpoint as "host:port".
std::string FormatEndpoint(const std::string& host, uint16_t port);

}  // namespace XER::network::core
