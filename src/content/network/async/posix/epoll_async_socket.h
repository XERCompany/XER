/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#if defined(__linux__) || defined(__ANDROID__)

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <sys/epoll.h>

namespace XER::network::async::posix {

// Async operation type
enum class EpollOperationType {
    kRead,
    kWrite,
    kConnect,
    kAccept
};

// Epoll event context
struct EpollEventContext {
    int fd;
    uint32_t events;
    EpollOperationType operation_type;
    std::function<void(int, size_t)> callback;
    uint8_t* buffer;
    size_t buffer_size;
    size_t bytes_transferred;
    int error_code;

    EpollEventContext()
        : fd(-1),
          events(0),
          operation_type(EpollOperationType::kRead),
          buffer(nullptr),
          buffer_size(0),
          bytes_transferred(0),
          error_code(0) {
    }
};

// Epoll-based async socket manager
class EpollAsyncManager {
public:
    EpollAsyncManager();
    ~EpollAsyncManager();

    // Initialize epoll
    bool Initialize(std::string* error);

    // Shutdown epoll
    void Shutdown();

    // Get epoll file descriptor
    int GetEpollFd() const { return epoll_fd_; }

    // Add a file descriptor to epoll
    bool AddFd(int fd, uint32_t events, EpollEventContext* context, std::string* error);

    // Modify events for a file descriptor
    bool ModifyFd(int fd, uint32_t events, std::string* error);

    // Remove a file descriptor from epoll
    bool RemoveFd(int fd, std::string* error);

    // Start the epoll event loop thread
    void StartEventLoop();

    // Stop the epoll event loop thread
    void StopEventLoop();

    // Check if running
    bool IsRunning() const { return running_; }

private:
    // Event loop thread function
    void EventLoopThread();

    int epoll_fd_;
    pthread_t event_thread_;
    bool running_;
    bool should_stop_;
    mutable std::mutex mutex_;
};

// Global epoll manager instance
EpollAsyncManager& GetGlobalEpollManager();

} // namespace XER::network::async::posix

#endif // __linux__ || __ANDROID__
