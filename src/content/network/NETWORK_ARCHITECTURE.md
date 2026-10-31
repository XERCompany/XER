# Network Module Architecture v2.0

## Overview

This document describes the enhanced architecture of the XER network module with full integration of async_io, cache, and flux systems.

## Design Principles

1. **Hybrid Async I/O**: IOCP for Windows (true async), thread pool for POSIX
2. **Cache Integration**: HTTP cache, DNS cache, SSL session cache using Engine::Cache
3. **Flux Logging**: Structured logging via Flux logger for debugging
4. **OpenSSL for TLS**: Replace NSS with OpenSSL for SSL/TLS
5. **Platform Abstraction**: Clean separation of Windows/POSIX implementations
6. **Zero-Copy**: Minimize data copying where possible
7. **Backwards Compatible**: Maintain existing API where possible

## Architecture Layers

```
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                      │
│              (HTTP/FTP/WebSocket/DNS/Proxy)              │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│                   Transport Layer                         │
│              (TransportClientSocket/ServerSocket)         │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│                   Socket Layer                            │
│         (TcpClientSocket/UdpClientSocket/SslSocket)      │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│                Platform Abstraction Layer                │
│         (SocketPlatformWin / SocketPlatformPosix)         │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│                 Async I/O Integration Layer               │
│   (IOCP for Windows / IOThreadPool for POSIX)           │
└─────────────────────────────────────────────────────────┘
                            │
┌─────────────────────────────────────────────────────────┐
│               Cross-Cutting Concerns                     │
│    (Cache Manager / Flux Logger / Error Handling)        │
└─────────────────────────────────────────────────────────┘
```

## Component Details

### 1. Platform Abstraction Layer

#### Windows Implementation (platform/windows/)

**IOCP-based Async I/O**:
- `IocpAsyncSocket` - Wrapper for Windows IOCP
- `IocpCompletionPort` - Manages IOCP handles
- Overlapped I/O operations for true async
- Thread pool for completion callbacks

**WinAPI Functions**:
- `WSAStartup/WSACleanup` - Socket initialization
- `socket()` - Socket creation
- `connect()` - Async connect via WSAEventSelect or IOCP
- `WSASend/WSARecv` - Async send/receive with overlapped I/O
- `CreateIoCompletionPort` - IOCP management
- `GetQueuedCompletionStatus` - Completion notification

#### POSIX Implementation (platform/posix/)

**Thread Pool-based Async I/O**:
- `EpollAsyncSocket` (Linux) - epoll-based async
- `KqueueAsyncSocket` (macOS/BSD) - kqueue-based async
- Fallback to IOThreadPool for simple cases

**POSIX Functions**:
- `socket()` - Socket creation
- `connect()` - Non-blocking connect
- `send/recv` - Non-blocking I/O
- `epoll_create/epoll_wait` (Linux)
- `kqueue/kevent` (macOS/BSD)

### 2. Async I/O Integration

#### IOThreadPool Integration

```cpp
namespace network::async {

class AsyncOperationManager {
public:
    static AsyncOperationManager& Instance();

    // Enqueue async operation to thread pool
    void Enqueue(std::function<void()> operation);

    // Get shared IOThreadPool instance
    IO::AsyncIO::IOThreadPool& GetThreadPool();

private:
    IO::AsyncIO::IOThreadPool& thread_pool_;
};

} // namespace network::async
```

#### IOCP Integration (Windows)

```cpp
namespace network::platform::windows {

class IocpCompletionPort {
public:
    bool Initialize(std::string* error);
    void Shutdown();
    HANDLE GetHandle() const { return completion_port_; }

    bool RegisterSocket(SOCKET socket, ULONG_PTR completion_key, std::string* error);
    bool PostCompletion(ULONG_PTR completion_key, DWORD bytes_transferred, OVERLAPPED* overlapped);

private:
    HANDLE completion_port_ = nullptr;
};

} // namespace network::platform::windows
```

### 3. Cache Integration

#### Network Cache Manager

```cpp
namespace network::cache {

class NetworkCacheManager {
public:
    static NetworkCacheManager& Instance();

    // HTTP Response Cache
    bool CacheHttpResponse(const std::string& url,
                          const HttpResponse& response,
                          std::chrono::seconds ttl);
    std::optional<HttpResponse> GetCachedResponse(const std::string& url);

    // DNS Cache
    bool CacheDnsEntry(const std::string& hostname,
                      const std::vector<std::string>& ips,
                      std::chrono::seconds ttl);
    std::optional<std::vector<std::string>> GetCachedDns(const std::string& hostname);

    // SSL Session Cache
    bool CacheSslSession(const std::string& host,
                        const SSL_SESSION* session);
    SSL_SESSION* GetCachedSslSession(const std::string& host);

private:
    Engine::Cache::CacheManager& cache_manager_;
};

} // namespace network::cache
```

Cache Scope Structure:
- `network/http_response` - HTTP responses
- `network/dns` - DNS entries
- `network/ssl_session` - SSL/TLS sessions
- `network/proxy_config` - Proxy configurations

### 4. Flux Logging Integration

```cpp
namespace network::logging {

class NetworkLogger {
public:
    static NetworkLogger& Instance();

    void LogConnection(const std::string& host, uint16_t port, bool success);
    void LogHttpRequest(const std::string& method, const std::string& url, int status_code);
    void LogSslHandshake(const std::string& host, bool success, const std::string& cipher);
    void LogError(const std::string& component, const std::string& error);

private:
    flux::core::Logger& flux_logger_;
};

} // namespace network::logging
```

### 5. OpenSSL Integration

#### SSL Context Management

```cpp
namespace network::ssl {

class SslContext {
public:
    static SslContext& Instance();

    bool Initialize(const std::string& ca_bundle_path, std::string* error);
    SSL_CTX* GetContext() const { return ctx_; }

    // Configuration
    void SetVerifyMode(int mode);
    void SetCipherList(const std::string& cipher_list);
    void SetProtocolVersion(int min_version, int max_version);

private:
    SSL_CTX* ctx_ = nullptr;
    int verify_mode_ = SSL_VERIFY_PEER;
};

} // namespace network::ssl
```

#### SSL Socket Implementation

```cpp
namespace network::socket {

class SslClientSocket : public TransportClientSocket {
public:
    SslClientSocket();
    ~SslClientSocket() override;

    bool Connect(const std::string& host, uint16_t port, std::string* error) override;
    void Disconnect() override;
    bool IsConnected() const override;

    int Read(uint8_t* buf, size_t len, std::string* error) override;
    int Write(const uint8_t* buf, size_t len, std::string* error) override;

    void ReadAsync(uint8_t* buf, size_t len, CompletionCallback cb) override;
    void WriteAsync(const uint8_t* buf, size_t len, CompletionCallback cb) override;

    // SSL-specific
    bool DoHandshake(std::string* error);
    std::string GetPeerCertificate() const;

private:
    SSL* ssl_ = nullptr;
    std::string host_;
    bool handshake_complete_ = false;
};

} // namespace network::socket
```

### 6. Enhanced Socket Layer

#### Async Socket Base Class

```cpp
namespace network::socket {

class AsyncSocket {
public:
    using AsyncCallback = std::function<void(int, size_t)>;

    virtual ~AsyncSocket() = default;

    // Async operations
    virtual void ConnectAsync(const std::string& host,
                             uint16_t port,
                             AsyncCallback cb) = 0;
    virtual void ReadAsync(uint8_t* buf, size_t len, AsyncCallback cb) = 0;
    virtual void WriteAsync(const uint8_t* buf, size_t len, AsyncCallback cb) = 0;

protected:
    IO::AsyncIO::IOThreadPool& thread_pool_;
};

} // namespace network::socket
```

### 7. HTTP Layer Enhancements

#### Async HTTP Client

```cpp
namespace network::http {

class AsyncHttpClient {
public:
    using ResponseCallback = std::function<void(const HttpResponse&, const std::string&)>;

    AsyncHttpClient();
    ~AsyncHttpClient();

    void GetAsync(const std::string& url, ResponseCallback cb);
    void PostAsync(const std::string& url,
                  const std::string& body,
                  ResponseCallback cb);

    // Configuration
    void SetCacheEnabled(bool enabled);
    void SetTimeout(std::chrono::milliseconds timeout);
    void SetFollowRedirects(bool follow);

private:
    network::cache::NetworkCacheManager& cache_;
    network::logging::NetworkLogger& logger_;
    std::chrono::milliseconds timeout_ = std::chrono::seconds(30);
    bool cache_enabled_ = true;
    bool follow_redirects_ = true;
};

} // namespace network::http
```

### 8. DNS Layer Enhancements

#### Async DNS Resolver

```cpp
namespace network::dns {

class AsyncDnsResolver {
public:
    using ResolveCallback = std::function<void(const std::vector<std::string>&, const std::string&)>;

    static AsyncDnsResolver& Instance();

    void ResolveAsync(const std::string& hostname, ResolveCallback cb);

    // Synchronous with cache
    std::vector<std::string> Resolve(const std::string& hostname, std::string* error);

private:
    network::cache::NetworkCacheManager& cache_;
    network::logging::NetworkLogger& logger_;
};

} // namespace network::dns
```

## File Structure

```
src/content/network/
├── NETWORK_ARCHITECTURE.md      (this file)
├── README.md                    (updated)
├── CMakeLists.txt              (updated)
├── async/                      (NEW - async I/O integration)
│   ├── async_operation_manager.h/cc
│   ├── async_socket.h/cc
│   └── iocp/                   (Windows IOCP)
│       ├── iocp_completion_port.h/cc
│       ├── iocp_async_socket.h/cc
│       └── iocp_overlapped.h
├── cache/                      (NEW - cache integration)
│   ├── network_cache_manager.h/cc
│   ├── http_cache.h/cc
│   ├── dns_cache.h/cc
│   └── ssl_cache.h/cc
├── logging/                    (NEW - Flux logging)
│   ├── network_logger.h/cc
│   └── log_formatter.h/cc
├── ssl/                        (NEW - OpenSSL integration)
│   ├── ssl_context.h/cc
│   ├── ssl_utils.h/cc
│   └── ssl_cert_verifier.h/cc
├── socket/
│   ├── tcp_client_socket.h/cc   (enhanced)
│   ├── udp_client_socket.h/cc   (enhanced)
│   ├── ssl_client_socket.h/cc   (rewritten with OpenSSL)
│   ├── socket_server.h/cc       (enhanced)
│   └── platform/
│       ├── socket_platform.h   (interface)
│       ├── windows/
│       │   ├── socket_platform_win.cc      (enhanced with IOCP)
│       │   ├── iocp_socket.cc              (NEW)
│       │   └── win_helpers.h/cc
│       └── posix/
│           ├── socket_platform_posix.cc    (enhanced with epoll/kqueue)
│           ├── epoll_socket.cc              (NEW)
│           └── kqueue_socket.cc            (NEW)
├── http/
│   ├── http_network_session.h/cc   (enhanced with async)
│   ├── http_transaction.h/cc        (enhanced)
│   ├── http_cache.h/cc             (moved to cache/)
│   └── async_http_client.h/cc       (NEW)
├── dns/
│   ├── host_resolver.h/cc           (enhanced with async)
│   ├── dns_resolver.h/cc            (enhanced)
│   └── async_dns_resolver.h/cc     (NEW)
└── transport/
    ├── transport_client_socket.h/cc
    └── transport_server_socket.h/cc
```

## Implementation Phases

### Phase 1: Foundation (Socket Layer)
1. Create async operation manager
2. Implement IOCP for Windows
3. Implement epoll/kqueue for POSIX
4. Integrate IOThreadPool
5. Update platform abstraction layer

### Phase 2: SSL/TLS
1. Replace NSS with OpenSSL
2. Implement SSL context management
3. Rewrite SslClientSocket with OpenSSL
4. Add SSL session caching
5. Implement certificate verification

### Phase 3: Cache Integration
1. Create NetworkCacheManager
2. Implement HTTP response caching
3. Implement DNS caching
4. Implement SSL session caching
5. Add cache invalidation logic

### Phase 4: Flux Logging
1. Create NetworkLogger
2. Integrate with Flux logger
3. Add logging to all network operations
4. Add log filtering and levels

### Phase 5: HTTP Layer
1. Implement AsyncHttpClient
2. Add async HTTP requests
3. Integrate with cache
4. Add connection pooling
5. Implement HTTP/2 support

### Phase 6: DNS Layer
1. Implement AsyncDnsResolver
2. Add async DNS resolution
3. Integrate with cache
4. Add DNS over HTTPS support (optional)

### Phase 7: Testing
1. Unit tests for all components
2. Integration tests
3. Performance benchmarks
4. Cross-platform testing

### Phase 8: Documentation
1. Update README.md
2. Add API documentation
3. Add usage examples
4. Update AGENTS.md

## Dependencies

### Required
- OpenSSL (libssl, libcrypto) - for SSL/TLS
- async_io module - for thread pool and async primitives
- cache module - for caching
- flux module - for logging
- vcpkg libraries: libuv, absl, fmt

### Platform-Specific
- Windows: Winsock2, ws2tcpip, mswsock
- Linux: epoll
- macOS/BSD: kqueue

## Performance Considerations

1. **Zero-Copy**: Use IOBufferPool for buffer management
2. **Connection Pooling**: Reuse TCP connections for HTTP
3. **DNS Caching**: Cache DNS entries with TTL
4. **HTTP Caching**: Cache responses per Cache-Control
5. **Async I/O**: Use platform-specific async (IOCP/epoll/kqueue)
6. **Thread Pool**: Limit thread count based on CPU cores
7. **Memory Pooling**: Reuse SSL contexts and buffers

## Security Considerations

1. **Certificate Verification**: Always verify SSL certificates
2. **TLS Version**: Support TLS 1.2 and 1.3 only
3. **Cipher Suites**: Use strong cipher suites
4. **Certificate Pinning**: Optional certificate pinning for critical hosts
5. **HSTS**: Support HTTP Strict Transport Security
6. **CORS**: Respect CORS headers
7. **Input Validation**: Validate all inputs (URLs, headers, etc.)

## Error Handling Strategy

1. **Never Throw**: Use error codes and output parameters
2. **Detailed Errors**: Provide detailed error messages
3. **Error Logging**: Log all errors via NetworkLogger
4. **Error Recovery**: Attempt recovery where possible
5. **Resource Cleanup**: Ensure cleanup on errors

## Thread Safety

1. **Thread Pool**: Thread-safe via mutex
2. **Cache**: Thread-safe (already implemented in cache module)
3. **Logger**: Thread-safe (Flux logger is thread-safe)
4. **Sockets**: Each socket instance is single-threaded
5. **DNS Resolver**: Thread-safe via mutex

## Backwards Compatibility

1. **Maintain Existing API**: Keep existing synchronous APIs
2. **Add Async APIs**: New async APIs alongside sync
3. **Deprecation Notice**: Mark old APIs as deprecated
4. **Migration Guide**: Provide migration guide

## Testing Strategy

1. **Unit Tests**: Test each component in isolation
2. **Integration Tests**: Test component interactions
3. **Platform Tests**: Test on Windows, Linux, macOS
4. **Performance Tests**: Benchmark critical paths
5. **Security Tests**: Test SSL/TLS implementations
6. **Mock Tests**: Mock network operations for testing

## Metrics and Monitoring

1. **Connection Count**: Track active connections
2. **Request Latency**: Measure request/response times
3. **Cache Hit Rate**: Track cache effectiveness
4. **Error Rate**: Track error occurrences
5. **Throughput**: Measure bytes transferred
6. **Thread Pool Utilization**: Monitor thread pool usage

## Future Enhancements

1. **HTTP/3 (QUIC)**: Add QUIC protocol support
2. **DNS over HTTPS**: Add DoH support
3. **HTTP/2 Push**: Add server push support
4. **Connection Migration**: Support connection migration
5. **Advanced Caching**: Add stale-while-revalidate
6. **Brotli Compression**: Add Brotli support
7. **WebTransport**: Add WebTransport API support
