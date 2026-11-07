/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_network_session.h"

namespace XER::network::http {

HttpNetworkSession& HttpNetworkSession::Default() {
    static HttpNetworkSession inst;
    return inst;
}

}  // namespace XER::network::http
