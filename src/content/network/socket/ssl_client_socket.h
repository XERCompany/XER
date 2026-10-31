/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "content/network/async/async_operation_manager.h"
#include "content/network/transport/transport_client_socket.h"
#include "content/network/ssl/ssl_context.h"
#include "content/network/ssl/ssl_session_cache.h"

namespace network::socket {

class SslClientSocket : public transport::TransportClientSocket {
public:
    SslClientSocket();
    ~SslClientSocket() override;

    // Configuration
    void SetCaBundlePath(const std::string& path) { ca_bundle_path_ = path; }
    void SetSkipVerify(bool skip) { skip_verify_ = skip; }
    void SetSniHostname(const std::string& hostname) { sni_hostname_ = hostname; }

    // Connection
    bool Connect(const std::string& host,
                 uint16_t port,
                 std::string* error) override;
    void Disconnect() override;
    bool IsConnected() const override { return ssl_ != nullptr && connected_; }

    // Synchronous I/O
    int Read(uint8_t* buf, size_t len, std::string* error) override;
    int Write(const uint8_t* buf, size_t len, std::string* error) override;

    // Asynchronous I/O
    void ReadAsync(uint8_t* buf, size_t len,
                   transport::CompletionCallback cb) override;
    void WriteAsync(const uint8_t* buf, size_t len,
                    transport::CompletionCallback cb) override;

    // SSL-specific operations
    bool DoHandshake(std::string* error);
    std::string GetPeerCertificate() const;
    std::string GetCipherSuite() const;
    std::string GetProtocolVersion() const;

    // Session resumption
    bool EnableSessionResumption(bool enable) { session_resumption_enabled_ = enable; return true; }

private:
    // Internal TCP socket operations
    bool TcpConnect(const std::string& host, uint16_t port, std::string* error);
    void TcpDisconnect();

    // SSL session cache integration
    bool LoadCachedSession();
    bool SaveSession();

    std::string ca_bundle_path_;
    bool skip_verify_ = false;
    std::string sni_hostname_;
    std::string connected_host_;
    uint16_t connected_port_ = 0;

    int tcp_fd_ = -1;
    bool connected_ = false;
    bool handshake_complete_ = false;
    bool session_resumption_enabled_ = true;

    ssl::ScopedSsl ssl_;
};

}  // namespace network::socket
