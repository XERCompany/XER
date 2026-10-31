/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "async_io/io_thread_pool.h"

namespace network::async {

// Callback type for async operations
// Parameters: (error_code, bytes_transferred)
using AsyncCallback = std::function<void(int, size_t)>;

// Async operation type
enum class AsyncOperationType {
    kConnect,
    kRead,
    kWrite,
    kAccept,
    kDnsResolve
};

// Async operation result
struct AsyncResult {
    int error_code = 0;
    size_t bytes_transferred = 0;
    std::string error_message;
};

// Async operation manager - integrates with IOThreadPool
class AsyncOperationManager {
public:
    static AsyncOperationManager& Instance();

    // Enqueue an async operation to the thread pool
    void Enqueue(std::function<void()> operation);

    // Get the shared IOThreadPool instance
    IO::AsyncIO::IOThreadPool& GetThreadPool();

    // Start/stop the thread pool
    void Start();
    void Stop();

    // Get statistics
    size_t GetPendingTaskCount() const;
    bool IsRunning() const;

private:
    AsyncOperationManager();
    ~AsyncOperationManager() = default;

    AsyncOperationManager(const AsyncOperationManager&) = delete;
    AsyncOperationManager& operator=(const AsyncOperationManager&) = delete;

    IO::AsyncIO::IOThreadPool& thread_pool_;
    mutable std::mutex mutex_;
};

// Helper class for wrapping async operations with error handling
class AsyncOperationWrapper {
public:
    static void WrapWithCallback(std::function<AsyncResult()> operation,
                                AsyncCallback callback);

    static void WrapVoid(std::function<void()> operation,
                        AsyncCallback callback);
};

} // namespace network::async
