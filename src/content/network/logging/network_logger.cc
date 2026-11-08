/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/logging/network_logger.h"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_join.h>

namespace XER::Network::Logging {

NetworkLogger::NetworkLogger()
    : flux_logger_(nullptr),
      enabled_(true),
      log_level_(XER::Flux::Core::LogLevel::kInfo) {
}

NetworkLogger& NetworkLogger::Instance() {
    static NetworkLogger instance;
    return instance;
}

void NetworkLogger::Initialize(XER::Flux::Core::Logger* flux_logger) {
    flux_logger_ = flux_logger;
}

void NetworkLogger::LogConnectionAttempt(const std::string& host, uint16_t port) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Info("network",
        absl::StrCat("Connecting to ", host, ":", port));
}

void NetworkLogger::LogConnectionSuccess(const std::string& host,
                                        uint16_t port,
                                        uint64_t duration_ms) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Info("network",
        absl::StrCat("Connected to ", host, ":", port, " in ", duration_ms, "ms"));
}

void NetworkLogger::LogConnectionFailure(const std::string& host,
                                         uint16_t port,
                                         const std::string& error) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Error("network",
        absl::StrCat("Failed to connect to ", host, ":", port, " - ", error));
}

void NetworkLogger::LogHttpRequest(const std::string& method,
                                  const std::string& url,
                                  const std::string& user_agent) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Info("network",
        absl::StrCat(method, " ", url, " (User-Agent: ", user_agent, ")"));
}

void NetworkLogger::LogHttpResponse(const std::string& method,
                                   const std::string& url,
                                   int status_code,
                                   uint64_t duration_ms,
                                   size_t bytes_received) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Info("network",
        absl::StrCat(method, " ", url, " -> ", status_code,
                    " (", duration_ms, "ms, ", bytes_received, " bytes)"));
}

void NetworkLogger::LogHttpError(const std::string& method,
                                const std::string& url,
                                const std::string& error) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Error("network",
        absl::StrCat(method, " ", url, " failed - ", error));
}

void NetworkLogger::LogSslHandshakeStart(const std::string& host) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Info("network",
        absl::StrCat("SSL handshake started for ", host));
}

void NetworkLogger::LogSslHandshakeSuccess(const std::string& host,
                                           const std::string& cipher,
                                           const std::string& protocol) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Info("network",
        absl::StrCat("SSL handshake succeeded for ", host,
                    " (", protocol, ", ", cipher, ")"));
}

void NetworkLogger::LogSslHandshakeFailure(const std::string& host,
                                           const std::string& error) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Error("network",
        absl::StrCat("SSL handshake failed for ", host, " - ", error));
}

void NetworkLogger::LogSslCertificate(const std::string& host,
                                      const std::string& subject,
                                      const std::string& issuer,
                                      bool valid) {
    if (!enabled_ || !flux_logger_) return;

    if (valid) {
        flux_logger_->Info("network",
            absl::StrCat("SSL certificate valid for ", host,
                        " (Subject: ", subject, ", Issuer: ", issuer, ")"));
    } else {
        flux_logger_->Error("network",
            absl::StrCat("SSL certificate invalid for ", host,
                        " (Subject: ", subject, ", Issuer: ", issuer, ")"));
    }
}

void NetworkLogger::LogDnsQuery(const std::string& hostname) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Debug("network",
        absl::StrCat("DNS query for ", hostname));
}

void NetworkLogger::LogDnsResponse(const std::string& hostname,
                                  const std::vector<std::string>& ips,
                                  bool from_cache,
                                  uint64_t duration_ms) {
    if (!enabled_ || !flux_logger_) return;

    std::string ip_list = absl::StrJoin(ips, ", ");
    flux_logger_->Info("network",
        absl::StrCat("DNS response for ", hostname,
                    " (", (from_cache ? "cached" : "resolved"),
                    ", ", duration_ms, "ms) -> ", ip_list));
}

void NetworkLogger::LogDnsError(const std::string& hostname, const std::string& error) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Error("network",
        absl::StrCat("DNS query failed for ", hostname, " - ", error));
}

void NetworkLogger::LogCacheHit(const std::string& cache_type, const std::string& key) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Debug("network",
        absl::StrCat("Cache hit [", cache_type, "]: ", key));
}

void NetworkLogger::LogCacheMiss(const std::string& cache_type, const std::string& key) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Debug("network",
        absl::StrCat("Cache miss [", cache_type, "]: ", key));
}

void NetworkLogger::LogCacheStore(const std::string& cache_type,
                                 const std::string& key,
                                 size_t size) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Debug("network",
        absl::StrCat("Cache store [", cache_type, "]: ", key,
                    " (", size, " bytes)"));
}

void NetworkLogger::LogCacheInvalidate(const std::string& cache_type,
                                      const std::string& key) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Debug("network",
        absl::StrCat("Cache invalidate [", cache_type, "]: ", key));
}

void NetworkLogger::LogError(const std::string& component, const std::string& error) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Error(component, error);
}

void NetworkLogger::LogWarning(const std::string& component, const std::string& warning) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Warning(component, warning);
}

void NetworkLogger::LogInfo(const std::string& component, const std::string& message) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Info(component, message);
}

void NetworkLogger::LogDebug(const std::string& component, const std::string& message) {
    if (!enabled_ || !flux_logger_) return;

    flux_logger_->Debug(component, message);
}

} // namespace XER::Network::Logging
