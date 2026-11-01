# Cache subsystem

The `src/cache` directory contains the project’s caching layer for temporary and persistent runtime data.

## Purpose

Caching helps reduce repeated computation, speeds up data access, and improves runtime responsiveness in data-heavy pipelines. The subsystem centralizes storage policy and lifecycle behavior instead of scattering it across modules.

## Core ideas

- temporary cache for short-lived runtime data
- persistent cache for longer-lived values and metadata
- validation and expiry policy for entries
- optional storage and serialization helpers
- integration-friendly API for other runtime modules

## Typical responsibilities

- cache entry lifecycle management
- storage backend selection
- cleanup and invalidation workflows
- metadata and TTL handling
- disk-based persistence and serialization strategies

## Example

```cpp
#include "cache/cache_manager.h"

using namespace flux;

auto& cache = context.get_cache_manager();
cache.put_temporary("session_token", "abc123", std::chrono::minutes(30));

auto token = cache.get_temporary<std::string>("session_token");
if (token) {
    // use token
}
```

## Notes

The exact storage backends and policies depend on the build and runtime configuration. The subsystem is intentionally general so it can support multiple types of cached data without forcing one implementation shape across the entire project.
