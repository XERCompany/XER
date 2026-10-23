# Network Module (src/content/network)

## Overview

The `src/content/network` module is the foundation of the Flux framework's network subsystem. It provides a comprehensive, production-grade network stack inspired by Google Chromium's architecture, enabling powerful, flexible, and secure handling of all network requests and interactions.

## Key Features

- **Full Network Stack**: Support for HTTP/1.1, HTTP/2, HTTP/3 (QUIC), WebSocket, FTP
- **URL Handling**: Complete URL parsing, validation, and normalization
- **Request/Response Management**: Create, send, and process network requests and responses
- **Caching**: Integrated HTTP cache for accelerated resource loading
- **Security**:
  - SSL/TLS support for HTTPS and secure connections
  - SSL certificate verification
  - Integration with certificate management systems
- **DNS Resolution**: Efficient domain name resolution with DNS caching
- **Proxy Support**: Support for HTTP, SOCKS proxies and automatic proxy detection
- **Low-Level Sockets**: TCP, UDP, SSL/TLS socket operations
- **Network Logging**: System for logging all network events for debugging
- **Asynchronous Model**: Built on asynchronous architecture for high performance

## Architecture

The Flux network module has a multi-layer architecture inspired by Chromium:

### Core (`core/`)
Manages network context, service components, and tasks:
- `NetworkContext`: Context for isolating network resources
- `NetworkService`: Main service providing network APIs
- `NetworkServiceDelegate`: Observer pattern for events

### URL (`url/`)
URL handling and request execution:
- `URLRequest` / `URLResponse`: Request/response structures
- `URLRequestContext`: Per-request configuration (cookies, proxy, auth)
- `URLRequestJob`: Synchronous request executor
- `URLRequestInterceptingJob`: Hook for mocking/caching

### Protocols (`http/`, `ftp/`, `websocket/`, `quic/`, `spdy/`)
Protocol implementations:
- `HttpNetworkSession`: Manages HTTP/1.1 and HTTP/2 connections
- `HttpStream`: Abstract protocol stream
- `HttpStreamFactory`: Creates protocol-specific streams
- `QuicStream`: QUIC protocol streams
- `WebSocketStream`: WebSocket bidirectional communication
- `FtpStream`: FTP file transfer

### Sockets (`socket/`)
Low-level socket operations:
- `TcpClientSocket` / `SocketServer`: TCP client and server
- `UdpClientSocket`: UDP datagram socket
- `SslClientSocket`: TLS/SSL over TCP
- `SocketStream`: Buffered I/O wrapper
- `SocketProxy`: HTTP CONNECT and SOCKS tunneling

### Proxy (`proxy/`)
Proxy configuration and handling:
- `ProxyConfig`: Configuration for proxies and bypass rules
- `ProxyService`: Service for finding applicable proxy
- `ProxyResolver`: PAC file handling
- `SocksClientSocket`: SOCKS4/5 client implementation

### Certificates (`cert/`)
SSL/TLS certificate management:
- `CertVerifier`: X509 certificate verification
- `CertStorage`: PEM bundle loading and caching
- `CertDatabase`: Certificate lookup with expiration
- `CertTransparency`: SCT log verification

### DNS (`dns/`)
Domain Name System resolution:
- `HostResolver`: Async DNS hostname lookup with cache
- `DnsResolver`: High-level interface
- `DnsClient`: Low-level UDP protocol
- `DnsConfigService`: Reads `/etc/resolv.conf`
- `DnsRecordType`: A, AAAA, CNAME, MX, SRV, etc.

### Transport (`transport/`)
Transport-level abstractions:
- `TransportClientSocket`: Abstract client interface
- `TransportServerSocket`: Abstract server interface
- `TransportNetworkDelegate`: Observer for I/O events
- `TransportNetworkSession`: Concrete POSIX TCP implementation
- `TransportIoBuffer`: Buffer management

### Utilities (`network_util/`)
Utility functions:
- IP address parsing and formatting (IPv4, IPv6)
- URL encoding/decoding
- Header parsing
- Standard port lookup

## Protocols

### HTTP(S)
- **HTTP/1.1**: Full support
- **HTTP/2**: Implemented for optimized multiplexing and reduced latency
- **HTTP/3 (QUIC)**: QUIC protocol for faster, more reliable UDP-based transport
- **HTTPS**: Uses SSL/TLS for secure connections

### WebSocket
Implements WebSocket protocol (RFC 6455) for real-time bidirectional communication.

### FTP
Basic FTP protocol support for file transfers.

## Security

- **SSL/TLS**: Support for modern TLS versions for traffic encryption
- **Certificate Verification**: X509 certificate verification to prevent MITM attacks
- **Certificate Management**: Integration with system or custom certificate stores

## DNS Resolution

- **DNS Caching**: Accelerates repeated requests
- **Parallel Resolution**: Ability to query multiple DNS servers simultaneously
- **IPv6 Support**: Full IPv6 address support

## Proxy

- **Format Support**: PAC files, direct proxy configuration
- **Proxy Types**: HTTP, HTTPS, SOCKS4, SOCKS5

## Asynchronous Model

The entire network stack is built on an asynchronous, event-driven architecture. This allows handling multiple network operations simultaneously without blocking the main application thread, using concepts similar to Chromium's `MessageLoop` and `NetLog`.

## Usage

### Creating a Network Context

```cpp
#include "content/network/core/network_context.h"
#include "content/network/url/url_request_context.h"
#include "content/network/network_service.h"
#include "content/network/http/http_network_session.h"
#include "content/network/socket/tcp_client_socket.h"
#include "content/network/dns/host_resolver.h"

// Initialize network context
network::NetworkContext network_context(network_service.get(),
                                       network_context_getter,
                                       network_service_delegate);
```

### HTTP GET Request

```cpp
#include "content/network/url/url_request_util.h"

// High-level: synchronous GET
std::string response = network::url::SyncGet("http://example.com/api");
```

### WebSocket Connection

```cpp
#include "content/network/websocket/websocket_stream.h"

network::websocket::WebSocketStream ws;
std::string error;

// Handshake
if (!ws.DoHandshake("ws://echo.websocket.org", {}, &error)) {
    // handle error
}

// Send message
ws.SendText("Hello", [](bool ok) { /* callback */ });

// Receive
ws.OnDataFrameReceived = [](const WebSocketFrame& frame) {
    if (frame.opcode == WebSocketOpcode::kText) {
        // process text message
    }
};
```

### Custom Socket Connection

```cpp
#include "content/network/socket/tcp_client_socket.h"

network::socket::TcpClientSocket socket;
std::string error;

if (socket.Connect("example.com", 443, &error)) {
    uint8_t buf[1024];
    int n = socket.Read(buf, sizeof(buf), &error);
    // process data
    socket.Disconnect();
}
```

### DNS Lookup

```cpp
#include "content/network/dns/dns_resolver.h"

auto& resolver = network::dns::DnsResolver::Instance();
std::vector<std::string> ips;
std::string error;

if (resolver.Resolve("example.com", &ips, &error)) {
    for (const auto& ip : ips) {
        // use IP address
    }
}
```

## Module Statistics

- **88 header files** - Comprehensive interface definitions
- **88 implementation files** - Full implementations for all modules
- **12 subdirectories** - Well-organized module structure
- **8 protocols supported** - HTTP/1.1, HTTP/2, HTTP/3, WebSocket, FTP, SPDY, QUIC, DNS

## Thread Safety

- **Main thread**: All synchronous APIs
- **Thread pool**: Async callbacks run on `IOThreadPool`
- **Singletons**: Thread-safe via mutex or atomic patterns
- **Buffers**: Thread-safe reference counting

## Error Handling

The module uses a three-tier error strategy:

1. **Connection errors** — reported to TransportNetworkDelegate
2. **Protocol errors** — logged to NetLog, reported via callbacks
3. **I/O errors** — included in completion callbacks

Never throws exceptions; all errors reported via output parameters or callbacks.

## Performance Features

- **Connection pooling** — HTTP/1.1 sockets are reused
- **Multiplexing** — HTTP/2, QUIC, SPDY share single connection
- **DNS caching** — entries cached with TTL
- **HTTP caching** — responses cached per Cache-Control
- **Buffer pooling** — `IOBufferPool` reuses allocations
- **Async I/O** — prevents main thread blocking

## Security Features

- **Certificate verification** — X509 validation with CA bundle
- **TLS 1.2/1.3** — via OpenSSL
- **HSTS** — enforced via HttpServerProperties
- **CORS** — respected (app enforces)
- **CSRF tokens** — app responsible for validation

## Testing

Use smoke tests in `demo_app/`:
```bash
./out/build/default/EngineBuilder run demo_app/bridge_multimedia.js
```

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) - Detailed architecture and design
- [README.md](README.md) - This file

## Contributing

When adding new features:
1. Follow the existing deep module architecture
2. Keep interfaces small and implementations deep
3. Use PIMPL pattern for ABI stability
4. Document the interface clearly
5. Add tests for the interface
6. Update this README

## License

Part of XER project. See main project LICENSE.
