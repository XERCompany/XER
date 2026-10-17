/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/dns/host_resolver_impl.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <chrono>
#include <cstring>

namespace network::dns {

namespace {
uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
}  // namespace

HostResolverImpl::HostResolverImpl(const DnsConfig& config) : config_(config) {
    pool_.Start();
}

bool HostResolverImpl::Resolve(const std::string& host,
                                uint16_t /*port*/,
                                std::vector<ResolvedAddress>* out,
                                std::string* error) {
    if (LookupCache(host, out)) return true;

    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0) {
        if (error) *error = ::gai_strerror(rc);
        return false;
    }

    std::vector<ResolvedAddress> addrs;
    for (auto* p = res; p != nullptr; p = p->ai_next) {
        char buf[INET6_ADDRSTRLEN]{};
        void* addr_ptr = nullptr;
        if (p->ai_family == AF_INET)
            addr_ptr = &reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr;
        else if (p->ai_family == AF_INET6)
            addr_ptr = &reinterpret_cast<sockaddr_in6*>(p->ai_addr)->sin6_addr;
        if (addr_ptr && ::inet_ntop(p->ai_family, addr_ptr, buf, sizeof(buf))) {
            addrs.push_back({buf, 60});
        }
    }
    ::freeaddrinfo(res);

    StoreCache(host, addrs);
    *out = std::move(addrs);
    return true;
}

void HostResolverImpl::ResolveAsync(const std::string& host,
                                    uint16_t port,
                                    ResolveCallback callback) {
    pool_.Enqueue([this, host, port, cb = std::move(callback)]() mutable {
        std::vector<ResolvedAddress> addrs;
        std::string error;
        const bool ok = Resolve(host, port, &addrs, &error);
        cb(ok, addrs, error);
    });
}

bool HostResolverImpl::LookupCache(const std::string& host,
                                    std::vector<ResolvedAddress>* out) const {
    std::lock_guard<std::mutex> lock(cache_mu_);
    auto it = cache_.find(host);
    if (it == cache_.end()) return false;
    if (NowMs() > it->second.expires_at_ms) return false;
    *out = it->second.addrs;
    return true;
}

void HostResolverImpl::StoreCache(const std::string& host,
                                   const std::vector<ResolvedAddress>& addrs) {
    if (addrs.empty()) return;
    const uint32_t ttl = addrs[0].ttl_sec ? addrs[0].ttl_sec : 60;
    std::lock_guard<std::mutex> lock(cache_mu_);
    cache_[host] = {addrs, NowMs() + static_cast<uint64_t>(ttl) * 1000ULL};
}

void HostResolverImpl::ClearCache() {
    std::lock_guard<std::mutex> lock(cache_mu_);
    cache_.clear();
}

}  // namespace network::dns
