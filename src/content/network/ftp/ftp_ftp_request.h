/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace XER::Network::Ftp {

enum class FtpTransferMode { kActive, kPassive };

struct FtpRequest {
    std::string       host;
    uint16_t          port     = 21;
    std::string       username = "anonymous";
    std::string       password = "anonymous@";
    std::string       remote_path;
    FtpTransferMode   mode     = FtpTransferMode::kPassive;
    bool              use_tls  = false;
};

struct FtpResponse {
    bool                  ok = false;
    std::string           error;
    std::vector<uint8_t>  data;    // downloaded bytes
    std::vector<std::string> listing; // for LIST commands
};

using FtpRequestCallback = std::function<void(FtpResponse)>;

}  // namespace XER::Network::Ftp
