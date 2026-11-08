/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/core/network_context.h"

namespace XER::Network::Core {

NetworkContext& NetworkContext::Instance() {
    static NetworkContext inst;
    return inst;
}

void NetworkContext::Reset() {
    request_ctx_ = Url::URLRequestContext();
    Cache().Clear();
    ServerProperties().Clear();
}

}  // namespace XER::Network::Core
