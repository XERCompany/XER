# Async I/O subsystem

The `src/async_io` directory contains the asynchronous input/output primitives used across the project. It provides the buffering, scheduling, and concurrency building blocks used by the runtime, networking layers, and native integrations.

## What it provides

- asynchronous file read and write paths
- reusable buffer pools and memory management helpers
- task scheduling and queue-based execution
- synchronization primitives
- hardware capability detection and resource monitoring
- optional backend integrations for accelerated workloads

## Typical components

- `async_file_reader.*` / `async_file_writer.*`
- `async_buffer_pool.*` / `io_buffer_pool.*`
- `async_task_queue.*`
- `io_file_map.*`
- `io_prefetch_manager.*`
- `io_priority_queue.*`
- `hardware_abstraction/`
- `dnn_backends/`

## Design principles

- keep I/O off the main execution path when possible
- reuse byte buffers and scheduling resources
- isolate platform-specific behavior behind abstract interfaces
- allow optional acceleration through CPU/GPU-aware paths

## Hardware abstraction

The `hardware_abstraction` directory covers capabilities such as:

- CPU detection
- GPU information and detection
- topology mapping
- power and temperature monitoring
- memory tracking

This enables runtime components to adapt their behavior based on the local machine environment and available acceleration paths.

## Example usage

```cpp
#include "async_io/async_file_reader.h"

IO::AsyncIO::AsyncFileReader reader;
reader.OpenFile("data.bin");
reader.ReadAsync(4096, [](int error, size_t bytes, const uint8_t* data) {
    if (error == 0) {
        // handle data
    }
});
```

## Notes

The subsystem is designed to be reusable rather than application-specific. It serves as a foundation for file access, async execution, and hardware-aware optimization throughout the project.
