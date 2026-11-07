/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/dns/dns_resolver.h"

#include <memory>

#include "content/network/dns/host_resolver_impl.h"

namespace XER::network::dns {

DnsResolver::DnsResolver(const DnsConfig& config)
    : config_(config),
      host_resolver_(std::make_unique<HostResolverImpl>(config)) {}

bool DnsResolver::Resolve(const std::string& host,
                          uint16_t port,
                          std::vector<ResolvedAddress>* out,
                          std::string* error) {
    return host_resolver_->Resolve(host, port, out, error);
}

void DnsResolver::ResolveAsync(const std::string& host,
                               uint16_t port,
                               ResolveCallback callback) {
    host_resolver_->ResolveAsync(host, port, std::move(callback));
}

void DnsResolver::ClearCache() {
    if (auto* impl = dynamic_cast<HostResolverImpl*>(host_resolver_.get())) {
        impl->ClearCache();
    }
}

}  // namespace XER::network::dns
