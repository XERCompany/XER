/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/core/network_context.h"

namespace XER::network::core {

NetworkContext& NetworkContext::Instance() {
    static NetworkContext inst;
    return inst;
}

void NetworkContext::Reset() {
    request_ctx_ = url::URLRequestContext();
    Cache().Clear();
    ServerProperties().Clear();
}

}  // namespace XER::network::core
