# Network subsystem

The `src/content/network` directory contains the project’s networking stack. It is intended to provide a reusable, layered networking foundation for runtime and application components.

## Scope

- HTTP and HTTPS logic
- URL handling and request/response lifecycle
- sockets and transport abstractions
- DNS, proxy, and certificate support
- event-driven or async styles of network operation
- integration with the project’s broader I/O and runtime infrastructure

## Layered structure

- `core/` — network context and service runtime
- `url/` — request/response and URL handling
- `http/` — HTTP protocol behavior
- `socket/` — low-level socket abstractions
- `dns/` — DNS resolution, lookups, and caching
- `proxy/` — proxy configuration and tunneling
- `cert/` — certificate management and verification

## Design goals

- clean separation between protocol, transport, and policy layers
- compatibility with asynchronous execution patterns
- safe integration with the rest of the runtime
- support for modern, layered networking scenarios without forcing a single API style

## Example

```cpp
#include "content/network/url/url_request_util.h"

std::string response = network::url::SyncGet("https://example.com");
```

## Notes

This module is part of the broader runtime architecture and should be viewed as a reusable networking foundation rather than a standalone web server implementation. Specific protocol support and feature depth depend on the project’s active build configuration and dependencies.
