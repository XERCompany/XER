/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>

#include "flux/core/logger.h"

namespace network::logging {

// Network logger - integrates with Flux logger
class NetworkLogger {
public:
    static NetworkLogger& Instance();

    // Initialize with Flux logger
    void Initialize(flux::core::Logger* flux_logger);

    // Connection logging
    void LogConnectionAttempt(const std::string& host, uint16_t port);
    void LogConnectionSuccess(const std::string& host, uint16_t port, uint64_t duration_ms);
    void LogConnectionFailure(const std::string& host, uint16_t port, const std::string& error);

    // HTTP request logging
    void LogHttpRequest(const std::string& method,
                       const std::string& url,
                       const std::string& user_agent);
    void LogHttpResponse(const std::string& method,
                       const std::string& url,
                       int status_code,
                       uint64_t duration_ms,
                       size_t bytes_received);
    void LogHttpError(const std::string& method,
                     const std::string& url,
                     const std::string& error);

    // SSL/TLS logging
    void LogSslHandshakeStart(const std::string& host);
    void LogSslHandshakeSuccess(const std::string& host,
                                const std::string& cipher,
                                const std::string& protocol);
    void LogSslHandshakeFailure(const std::string& host, const std::string& error);
    void LogSslCertificate(const std::string& host,
                          const std::string& subject,
                          const std::string& issuer,
                          bool valid);

    // DNS logging
    void LogDnsQuery(const std::string& hostname);
    void LogDnsResponse(const std::string& hostname,
                       const std::vector<std::string>& ips,
                       bool from_cache,
                       uint64_t duration_ms);
    void LogDnsError(const std::string& hostname, const std::string& error);

    // Cache logging
    void LogCacheHit(const std::string& cache_type, const std::string& key);
    void LogCacheMiss(const std::string& cache_type, const std::string& key);
    void LogCacheStore(const std::string& cache_type, const std::string& key, size_t size);
    void LogCacheInvalidate(const std::string& cache_type, const std::string& key);

    // General error logging
    void LogError(const std::string& component, const std::string& error);
    void LogWarning(const std::string& component, const std::string& warning);
    void LogInfo(const std::string& component, const std::string& message);
    void LogDebug(const std::string& component, const std::string& message);

    // Enable/disable logging
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

    // Set log level
    void SetLogLevel(flux::core::LogLevel level) { log_level_ = level; }
    flux::core::LogLevel GetLogLevel() const { return log_level_; }

private:
    NetworkLogger();
    ~NetworkLogger() = default;

    NetworkLogger(const NetworkLogger&) = delete;
    NetworkLogger& operator=(const NetworkLogger&) = delete;

    flux::core::Logger* flux_logger_;
    bool enabled_;
    flux::core::LogLevel log_level_;
};

} // namespace network::logging
