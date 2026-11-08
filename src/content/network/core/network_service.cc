/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/core/network_service.h"
#include "content/network/url/url_request_job.h"
#include "content/network/core/network_task_runner.h"

namespace XER::Network::Core {

NetworkService& NetworkService::Instance() {
    static NetworkService inst;
    return inst;
}

void NetworkService::SetDelegate(std::unique_ptr<NetworkServiceDelegate> d) {
    delegate_ = std::move(d);
}

Url::URLResponse NetworkService::Fetch(const Url::URLRequest& request,
                                        std::string* error) {
    auto& ctx = NetworkContext::Instance().RequestContext();
    Url::URLRequestJob job(ctx);
    Url::URLResponse resp;
    std::string local_err;
    const bool ok = job.Run(request, &resp, error ? error : &local_err);
    if (!ok && delegate_)
        delegate_->OnNetworkError(error ? *error : local_err);
    return resp;
}

void NetworkService::FetchAsync(const Url::URLRequest& request,
                                 Url::URLResponseCallback callback) {
    NetworkTaskRunner::Instance().PostTask(
        [this, request, cb = std::move(callback)]() mutable {
            std::string err;
            auto resp = Fetch(request, &err);
            cb(!err.empty() ? false : true, resp, err);
        });
}

void NetworkService::Shutdown() {
    NetworkContext::Instance().Reset();
    if (delegate_) delegate_->OnNetworkStopped();
}

}  // namespace XER::Network::Core
