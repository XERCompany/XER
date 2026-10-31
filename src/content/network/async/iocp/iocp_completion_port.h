/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#ifdef _WIN32

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

namespace network::async::iocp {

// Completion key for IOCP operations
struct IocpCompletionKey {
    void* socket_ptr;
    uint32_t operation_type;  // AsyncOperationType enum
};

// Overlapped context for async operations
struct IocpOverlappedContext {
    OVERLAPPED overlapped;
    WSABUF data_buf;
    uint8_t* buffer;
    size_t buffer_size;
    size_t bytes_transferred;
    int error_code;
    std::function<void(int, size_t)> callback;
    uint32_t operation_type;

    IocpOverlappedContext()
        : buffer(nullptr),
          buffer_size(0),
          bytes_transferred(0),
          error_code(0),
          operation_type(0) {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
    }
};

// IOCP Completion Port Manager
class IocpCompletionPort {
public:
    IocpCompletionPort();
    ~IocpCompletionPort();

    // Initialize the completion port
    bool Initialize(std::string* error);

    // Shutdown the completion port
    void Shutdown();

    // Get the completion port handle
    HANDLE GetHandle() const { return completion_port_; }

    // Register a socket with the completion port
    bool RegisterSocket(SOCKET socket,
                       ULONG_PTR completion_key,
                       std::string* error);

    // Post a completion status manually (for testing/simulation)
    bool PostCompletion(ULONG_PTR completion_key,
                       DWORD bytes_transferred,
                       OVERLAPPED* overlapped,
                       std::string* error);

    // Get completion status (blocking)
    bool GetCompletionStatus(DWORD* bytes_transferred,
                            ULONG_PTR* completion_key,
                            OVERLAPPED** overlapped,
                            DWORD milliseconds,
                            std::string* error);

    // Start the completion thread
    void StartCompletionThread();

    // Stop the completion thread
    void StopCompletionThread();

    // Check if running
    bool IsRunning() const { return running_; }

private:
    // Completion thread loop
    void CompletionThreadLoop();

    HANDLE completion_port_;
    HANDLE completion_thread_;
    bool running_;
    bool should_stop_;
    mutable std::mutex mutex_;
};

// Global IOCP instance
IocpCompletionPort& GetGlobalIocp();

} // namespace network::async::iocp

#endif // _WIN32
