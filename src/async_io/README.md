# async_io

Low-level async and buffered I/O primitives used by higher-level modules and core utility layer.

## Architecture Overview

The async_io module provides a comprehensive set of asynchronous I/O utilities organized into several sub-modules:

### Core I/O Components
- `async_file_reader.*` — Async/sync file read operations with callback support
- `async_file_writer.*` — Async/sync file write operations
- `io_buffer_pool.*` — Reusable byte buffer pool with bucket-based allocation
- `io_cache_manager.*` — In-memory cache with eviction policies
- `io_thread_pool.*` — Shared thread pool for async task execution
- `io_stream_reader.*` / `io_stream_writer.*` — Stream-based I/O
- `io_file_map.*` — Memory-mapped file operations
- `io_prefetch_manager.*` — Prefetching for improved I/O performance
- `io_priority_queue.*` — Priority-based task scheduling
- `async_buffer_pool.*` — Async buffer management
- `async_task_queue.*` — Task queue with async execution

### Hardware Abstraction Layer
Located in `hardware_abstraction/`:
- `cpu_info.*` — CPU detection, core management, performance monitoring
- `gpu_info.*` — GPU detection and capabilities
- `hw_capability_scanner.*` — Hardware capability detection
- `hw_direct_io.*` — Direct I/O operations
- `hw_power_monitor.*` — Power consumption monitoring
- `hw_temp_sensor.*` — Temperature sensor interface
- `hw_topology_map.*` — Hardware topology mapping
- `memory_monitor.*` — Memory usage tracking

### DNN Backends
Located in `dnn_backends/` (when ENABLE_CUDNN=ON):
- `cudnn_bridge.*` — cuDNN integration bridge
- `cudnn_activation_engine.*` — Activation functions
- `cudnn_convolution_engine.*` — Convolution operations
- `cudnn_pooling_engine.*` — Pooling operations

### Synchronization Primitives
Located in `sync_primitives/`:
- `barrier.*` — Thread barrier synchronization
- `condition_variable_wrapper.*` — Condition variable with RAII
- `mutex_wrapper.*` — Mutex with statistics and naming
- `read_write_lock.*` — Read-write lock implementation
- `sync_barrier.*` — Synchronization barrier
- `sync_event_listener.*` — Event listener pattern
- `sync_semaphore_cpu.*` — CPU-based semaphore

### Logging and Debugging
Located in `log_and_debug/`:
- `io_dump_helper.*` — Dump utilities for debugging
- `io_gpu_usage_logger.*` — GPU usage logging
- `io_memory_tracker.*` — Memory allocation tracking
- `io_perf_counter.*` — Performance counters

## Design Patterns

### Singleton Pattern
- `IOThreadPool::GetSharedInstance()` — Global thread pool
- `IOBufferPool::Instance()` — Global buffer pool

### RAII Pattern
- `MutexWrapper::ScopedLock` — Automatic lock management
- Buffer pointers use `std::shared_ptr` for automatic cleanup

### Callback Pattern
- `ReadCallback` — Async read completion callback
- Event-driven async operations

## Thread Safety

Most components use:
- `std::mutex` for internal synchronization
- `std::condition_variable` for thread coordination
- RAII locks to prevent deadlocks
- Atomic operations where appropriate

## Namespace Organization

Primary namespace: `IO::AsyncIO`

Sub-namespaces:
- `IO::AsyncIO::Hardware` — Hardware abstraction
- `IO::AsyncIO::Sync` — Synchronization primitives
- `AsyncIO::IO::Hardware` — Alternative namespace (inconsistent)

**Note**: Namespace inconsistency exists between `IO::AsyncIO` and `AsyncIO::IO`. This should be standardized to `IO::AsyncIO` throughout.

## Project Integration

`async_io` is used in:

- `src/modules/io_async_module.cc` — JavaScript async I/O module
- `src/tool_to.cc` — Core I/O utilities
- `src/flow_script.cc` — FlowScript runtime (through ToolTo)
- `src/runtime_live_interface_compiler.cc` — Runtime compilation (through ToolTo)
- `src/compiler_api.cc` — Compiler API (through ToolTo)

## Usage Examples

### Thread Pool
```cpp
auto& pool = IO::AsyncIO::IOThreadPool::GetSharedInstance();
pool.Start();
pool.Enqueue([]() {
    // Async task
});
pool.Stop();
```

### Buffer Pool
```cpp
auto& buffer_pool = IO::AsyncIO::IOBufferPool::Instance();
auto buffer = buffer_pool.Acquire(4096);
// Use buffer
buffer_pool.Release(buffer.get());
```

### Async File Reading
```cpp
IO::AsyncIO::AsyncFileReader reader;
reader.OpenFile("data.bin");
reader.ReadAsync(1024, [](int error, size_t bytes, const uint8_t* data) {
    // Handle completion
});
```

## Known Issues and Improvements

### High Priority
1. **Namespace Consistency**: Standardize all namespaces to `IO::AsyncIO`
2. **Header Guard Consistency**: Use `#pragma once` or `#ifndef` consistently
3. **Documentation**: Add detailed API documentation to headers
4. **Error Handling**: Standardize error handling across components

### Medium Priority
5. **Thread Safety Documentation**: Explicitly document thread-safety guarantees
6. **Performance Monitoring**: Add built-in performance metrics
7. **Unit Tests**: Add comprehensive unit tests for each component
8. **Resource Limits**: Add configurable resource limits (buffer sizes, thread counts)

### Low Priority
9. **Alternative Implementations**: Consider lock-free data structures for high-contention scenarios
10. **Platform-Specific Optimizations**: Add platform-specific optimizations (Windows I/O completion ports, Linux io_uring)

## Dependencies

- Standard library: `<mutex>`, `<condition_variable>`, `<thread>`, `<vector>`, etc.
- cuDNN (optional, when ENABLE_CUDNN=ON)
- Platform-specific headers for hardware detection

## Build Configuration

Components are conditionally compiled based on:
- `ENABLE_CUDNN` — Enables DNN backends
- `ENABLE_CUDA` — Enables CUDA support in hardware abstraction
- `ENABLE_AI` — Enables AI-related components
