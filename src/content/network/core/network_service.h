/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include "content/network/core/network_context.h"
#include "content/network/core/network_service_delegate.h"
#include "content/network/url/url_request.h"

namespace XER::Network::Core {

// Facade over the entire network stack.
class NetworkService {
public:
    static NetworkService& Instance();

    void SetDelegate(std::unique_ptr<NetworkServiceDelegate> delegate);

    // Synchronous HTTP(S) fetch.
    Url::URLResponse Fetch(const Url::URLRequest& request,
                            std::string* error = nullptr);

    // Asynchronous HTTP(S) fetch.
    void FetchAsync(const Url::URLRequest& request,
                    Url::URLResponseCallback callback);

    void Shutdown();

private:
    NetworkService() = default;
    std::unique_ptr<NetworkServiceDelegate> delegate_;
};

}  // namespace XER::Network::Core
