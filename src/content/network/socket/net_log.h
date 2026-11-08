/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <string>
#include "flux/core/logger.h"

namespace XER::Network::Socket {

// Thin adapter: routes socket-layer events to flux Logger.
class NetLog {
public:
    static void Debug(const std::string& msg)   { XER::Flux::Core::Logger().Debug("net", msg); }
    static void Info(const std::string& msg)    { XER::Flux::Core::Logger().Info("net", msg); }
    static void Warning(const std::string& msg) { XER::Flux::Core::Logger().Warning("net", msg); }
    static void Error(const std::string& msg)   { XER::Flux::Core::Logger().Error("net", msg); }
};

}  // namespace XER::Network::Socket

