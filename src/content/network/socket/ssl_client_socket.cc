/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/socket/ssl_client_socket.h"

#include "content/network/async/async_operation_manager.h"
#include "content/network/logging/network_logger.h"
#include "content/network/socket/platform/socket_platform.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace XER::network::socket {

SslClientSocket::SslClientSocket() {
}

SslClientSocket::~SslClientSocket() {
    Disconnect();
}

bool SslClientSocket::TcpConnect(const std::string& host, uint16_t port, std::string* error) {
    // Use platform abstraction for TCP connection
    auto& platform = platform::SocketPlatform::GetInstance();

    // Resolve address
    std::vector<platform::AddressInfo> addresses;
    if (!platform.ResolveAddress(host, port, &addresses, error)) {
        return false;
    }

    // Try each address
    for (const auto& addr : addresses) {
        platform::SocketHandle socket;
        if (addr.is_ipv6) {
            socket = platform.CreateTcpSocket(true, error);
        } else {
            socket = platform.CreateTcpSocket(false, error);
        }

        if (!socket.IsValid()) {
            continue;
        }

        if (platform.Connect(socket, addr, error)) {
            tcp_fd_ = static_cast<int>(socket.Get());
            // Detach the handle so it won't be closed
            socket.Detach();
            return true;
        }
    }

    if (error && error->empty()) {
        *error = "Failed to connect to any address";
    }
    return false;
}

void SslClientSocket::TcpDisconnect() {
    if (tcp_fd_ >= 0) {
#ifdef _WIN32
        closesocket(tcp_fd_);
#else
        close(tcp_fd_);
#endif
        tcp_fd_ = -1;
    }
}

bool SslClientSocket::Connect(const std::string& host,
                               uint16_t port,
                               std::string* error) {
    logging::NetworkLogger::Instance().LogConnectionAttempt(host, port);

    // Initialize SSL context if not already
    auto& ssl_ctx = ssl::SslContext::Instance();
    if (!ssl_ctx.IsInitialized()) {
        std::string ca_path = ca_bundle_path_.empty() ? "" : ca_bundle_path_;
        if (!ssl_ctx.Initialize(ca_path, error)) {
            logging::NetworkLogger::Instance().LogConnectionFailure(host, port, *error);
            return false;
        }
    }

    // Set verify mode
    if (skip_verify_) {
        ssl_ctx.SetVerifyMode(SSL_VERIFY_NONE);
    }

    // TCP connection
    if (!TcpConnect(host, port, error)) {
        logging::NetworkLogger::Instance().LogConnectionFailure(host, port, *error);
        return false;
    }

    connected_host_ = host;
    connected_port_ = port;

    // Create SSL object
    SSL* raw_ssl = ssl_ctx.CreateSsl();
    if (raw_ssl == nullptr) {
        TcpDisconnect();
        if (error) *error = ssl::GetLastSslError();
        logging::NetworkLogger::Instance().LogConnectionFailure(host, port, *error);
        return false;
    }
    ssl_.reset(raw_ssl);

    // Set file descriptor
    SSL_set_fd(ssl_.get(), tcp_fd_);

    // Set SNI hostname
    if (!sni_hostname_.empty()) {
        SSL_set_tlsext_host_name(ssl_.get(), sni_hostname_.c_str());
    } else {
        SSL_set_tlsext_host_name(ssl_.get(), host.c_str());
    }

    if (!skip_verify_ && SSL_set1_host(ssl_.get(), host.c_str()) != 1) {
        if (error) *error = ssl::GetLastSslError();
        Disconnect();
        return false;
    }

    // Load cached session if enabled
    if (session_resumption_enabled_) {
        LoadCachedSession();
    }

    // Perform SSL handshake
    logging::NetworkLogger::Instance().LogSslHandshakeStart(host);
    if (!DoHandshake(error)) {
        TcpDisconnect();
        ssl_.reset();
        logging::NetworkLogger::Instance().LogSslHandshakeFailure(host, *error);
        return false;
    }

    connected_ = true;
    handshake_complete_ = true;

    // Save session for resumption
    if (session_resumption_enabled_) {
        SaveSession();
    }

    // Log success
    std::string cipher = GetCipherSuite();
    std::string protocol = GetProtocolVersion();
    logging::NetworkLogger::Instance().LogSslHandshakeSuccess(host, cipher, protocol);
    logging::NetworkLogger::Instance().LogConnectionSuccess(host, port, 0);

    return true;
}

void SslClientSocket::Disconnect() {
    if (ssl_) {
        SSL_shutdown(ssl_.get());
        ssl_.reset();
    }

    TcpDisconnect();

    connected_ = false;
    handshake_complete_ = false;
    connected_host_.clear();
    connected_port_ = 0;
}

bool SslClientSocket::DoHandshake(std::string* error) {
    ERR_clear_error();
    int result = SSL_connect(ssl_.get());
    if (result != 1) {
        const int ssl_error = SSL_get_error(ssl_.get(), result);
        if (error) {
            if (ssl_error == SSL_ERROR_SSL) {
                const long verify_result = SSL_get_verify_result(ssl_.get());
                *error = verify_result != X509_V_OK
                    ? X509_verify_cert_error_string(verify_result)
                    : ssl::GetLastSslError();
            } else {
                *error = "SSL_connect failed; SSL_get_error=" +
                        std::to_string(ssl_error);
            }
        }
        return false;
    }
    // Verify certificate if not skipped
    if (!skip_verify_) {
        X509* cert = SSL_get_peer_certificate(ssl_.get());
        if (cert == nullptr) {
            if (error) *error = "No peer certificate";
            return false;
        }

        long verify_result = SSL_get_verify_result(ssl_.get());
        if (verify_result != X509_V_OK) {
            if (error) *error = ssl::GetSslErrorString(verify_result);
            X509_free(cert);
            return false;
        }

        X509_free(cert);
    }

    return true;
}

int SslClientSocket::Read(uint8_t* buf, size_t len, std::string* error) {
    if (!ssl_ || !connected_) {
        if (error) *error = "Not connected";
        return -1;
    }

    int result = SSL_read(ssl_.get(), buf, static_cast<int>(len));
    if (result <= 0) {
        int ssl_error = SSL_get_error(ssl_.get(), result);
        if (error) {
            *error = ssl::GetSslErrorString(ssl_error);
        }
        return -1;
    }

    return result;
}

int SslClientSocket::Write(const uint8_t* buf, size_t len, std::string* error) {
    if (!ssl_ || !connected_) {
        if (error) *error = "Not connected";
        return -1;
    }

    int result = SSL_write(ssl_.get(), buf, static_cast<int>(len));
    if (result <= 0) {
        int ssl_error = SSL_get_error(ssl_.get(), result);
        if (error) {
            *error = ssl::GetSslErrorString(ssl_error);
        }
        return -1;
    }

    return result;
}

void SslClientSocket::ReadAsync(uint8_t* buf, size_t len,
                               transport::CompletionCallback cb) {
    async::AsyncOperationManager::Instance().Enqueue(
        [this, buf, len, cb = std::move(cb)]() {
            std::string error;
            int result = Read(buf, len, &error);
            cb(result, error);
        });
}

void SslClientSocket::WriteAsync(const uint8_t* buf, size_t len,
                                transport::CompletionCallback cb) {
    std::vector<uint8_t> data(buf, buf + len);
    async::AsyncOperationManager::Instance().Enqueue(
        [this, data = std::move(data), cb = std::move(cb)]() mutable {
            std::string error;
            int result = Write(data.data(), data.size(), &error);
            cb(result, error);
        });
}

std::string SslClientSocket::GetPeerCertificate() const {
    if (!ssl_) {
        return "";
    }

    X509* cert = SSL_get_peer_certificate(ssl_.get());
    if (cert == nullptr) {
        return "";
    }

    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(bio, cert);

    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);

    BIO_free(bio);
    X509_free(cert);

    return result;
}

std::string SslClientSocket::GetCipherSuite() const {
    if (!ssl_) {
        return "";
    }

    const char* cipher = SSL_get_cipher(ssl_.get());
    return cipher ? std::string(cipher) : "";
}

std::string SslClientSocket::GetProtocolVersion() const {
    if (!ssl_) {
        return "";
    }

    const char* version = SSL_get_version(ssl_.get());
    return version ? std::string(version) : "";
}

bool SslClientSocket::LoadCachedSession() {
    if (connected_host_.empty() || !ssl_) {
        return false;
    }

    // Use SslSessionCache
    auto& session_cache = ssl::SslSessionCache::Instance();
    SSL_SESSION* session = session_cache.GetSession(connected_host_);

    if (session) {
        SSL_set_session(ssl_.get(), session);
        SSL_SESSION_free(session);  // Decrement ref count after setting
        return true;
    }

    return false;
}

bool SslClientSocket::SaveSession() {
    if (connected_host_.empty() || !ssl_) {
        return false;
    }

    SSL_SESSION* session = SSL_get_session(ssl_.get());
    if (session == nullptr) {
        return false;
    }

    // Store in SslSessionCache
    auto& session_cache = ssl::SslSessionCache::Instance();
    session_cache.StoreSession(connected_host_, session);

    return true;
}

}  // namespace XER::network::socket
