/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "content/network/http/http_router.h"
#include "content/network/socket/socket_server.h"

namespace XER::Network::Http {

// Minimal HTTP/1.1 server built on SocketServer.
//
// - One detached thread per accepted connection.
// - Keep-alive supported; each connection is served until close/error.
// - Requests are dispatched through an HttpRouter.
// - GET/HEAD requests that miss the router fall through to optional
//   static file serving rooted at SetStaticRoot().
//
// Not intended for untrusted networks: no TLS, no timeouts, no rate
// limiting. Bind to 127.0.0.1 unless you know what you are doing.
class HttpServer {
public:
    HttpServer() = default;
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Start listening. Call Run() on a dedicated thread afterwards.
    bool Listen(const std::string& bind_address, uint16_t port,
                int backlog, std::string* error);

    void SetRouter(std::shared_ptr<HttpRouter> router);

    // Directory served for GET/HEAD requests that no route matches.
    // Empty string disables static file serving.
    void SetStaticRoot(std::string root);

    // Cap for a single request body (Content-Length or chunked).
    void SetMaxBodyBytes(size_t bytes) { max_body_bytes_ = bytes; }
    // Cap for the accumulated request head (request line + headers).
    void SetMaxHeaderBytes(size_t bytes) { max_header_bytes_ = bytes; }

    // Blocking accept loop. Returns when the listen socket is closed
    // (Stop() or Listen failure).
    void Run();
    void Stop();

    bool IsRunning() const { return running_; }
    bool IsListening() const { return server_.IsListening(); }

private:
    void ServeClient(std::unique_ptr<Socket::TcpClientSocket> client);
    bool TryServeStatic(const HttpRequest& request, HttpResponse* response) const;

    Socket::SocketServer server_;
    std::shared_ptr<HttpRouter> router_;
    std::string static_root_;
    std::atomic<bool> running_{false};
    size_t max_body_bytes_ = 256ull * 1024 * 1024;
    size_t max_header_bytes_ = 1024 * 1024;
};

}  // namespace XER::Network::Http
