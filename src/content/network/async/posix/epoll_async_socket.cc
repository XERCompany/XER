/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#if defined(__linux__) || defined(__ANDROID__)

#include "content/network/async/posix/epoll_async_socket.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

namespace XER::Network::Async::Posix {

EpollAsyncManager::EpollAsyncManager()
    : epoll_fd_(-1),
      event_thread_(0),
      running_(false),
      should_stop_(false) {
}

EpollAsyncManager::~EpollAsyncManager() {
    Shutdown();
}

bool EpollAsyncManager::Initialize(std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (epoll_fd_ != -1) {
        if (error) *error = "Epoll already initialized";
        return false;
    }

    // Create epoll instance
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) {
        if (error) *error = "epoll_create1 failed: " + std::string(strerror(errno));
        return false;
    }

    return true;
}

void EpollAsyncManager::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        StopEventLoop();
    }

    if (epoll_fd_ != -1) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

bool EpollAsyncManager::AddFd(int fd, uint32_t events, EpollEventContext* context, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (epoll_fd_ == -1) {
        if (error) *error = "Epoll not initialized";
        return false;
    }

    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = context;

    int result = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    if (result == -1) {
        if (error) *error = "epoll_ctl ADD failed: " + std::string(strerror(errno));
        return false;
    }

    return true;
}

bool EpollAsyncManager::ModifyFd(int fd, uint32_t events, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (epoll_fd_ == -1) {
        if (error) *error = "Epoll not initialized";
        return false;
    }

    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;

    int result = epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
    if (result == -1) {
        if (error) *error = "epoll_ctl MOD failed: " + std::string(strerror(errno));
        return false;
    }

    return true;
}

bool EpollAsyncManager::RemoveFd(int fd, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (epoll_fd_ == -1) {
        if (error) *error = "Epoll not initialized";
        return false;
    }

    int result = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    if (result == -1) {
        if (error) *error = "epoll_ctl DEL failed: " + std::string(strerror(errno));
        return false;
    }

    return true;
}

void EpollAsyncManager::StartEventLoop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        return;  // Already running
    }

    should_stop_ = false;
    running_ = true;

    int result = pthread_create(&event_thread_, nullptr, [](void* arg) -> void* {
        auto* manager = static_cast<EpollAsyncManager*>(arg);
        manager->EventLoopThread();
        return nullptr;
    }, this);

    if (result != 0) {
        running_ = false;
    }
}

void EpollAsyncManager::StopEventLoop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_) {
        return;
    }

    should_stop_ = true;

    // Wake up the event loop by writing to the epoll fd
    if (epoll_fd_ != -1) {
        uint64_t dummy = 1;
        write(epoll_fd_, &dummy, sizeof(dummy));
    }

    // Wait for thread to finish
    pthread_join(event_thread_, nullptr);
    running_ = false;
}

void EpollAsyncManager::EventLoopThread() {
    const int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];

    while (!should_stop_) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, 100);  // 100ms timeout

        if (should_stop_) {
            break;
        }

        if (nfds == -1) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal
            }
            // Log error
            continue;
        }

        for (int i = 0; i < nfds; ++i) {
            auto* context = static_cast<EpollEventContext*>(events[i].data.ptr);

            if (context == nullptr) {
                continue;
            }

            context->error_code = 0;

            // Handle read event
            if (events[i].events & EPOLLIN) {
                if (context->operation_type == EpollOperationType::kRead) {
                    ssize_t bytes = read(context->fd, context->buffer, context->buffer_size);
                    if (bytes == -1) {
                        context->error_code = errno;
                        context->bytes_transferred = 0;
                    } else {
                        context->bytes_transferred = bytes;
                    }
                } else if (context->operation_type == EpollOperationType::kAccept) {
                    // Accept handled separately
                }
            }

            // Handle write event
            if (events[i].events & EPOLLOUT) {
                if (context->operation_type == EpollOperationType::kWrite) {
                    ssize_t bytes = write(context->fd, context->buffer, context->buffer_size);
                    if (bytes == -1) {
                        context->error_code = errno;
                        context->bytes_transferred = 0;
                    } else {
                        context->bytes_transferred = bytes;
                    }
                } else if (context->operation_type == EpollOperationType::kConnect) {
                    // Check if connection succeeded
                    int error = 0;
                    socklen_t len = sizeof(error);
                    getsockopt(context->fd, SOL_SOCKET, SO_ERROR, &error, &len);
                    context->error_code = error;
                    context->bytes_transferred = 0;
                }
            }

            // Handle error event
            if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                int error = 0;
                socklen_t len = sizeof(error);
                getsockopt(context->fd, SOL_SOCKET, SO_ERROR, &error, &len);
                context->error_code = error;
                context->bytes_transferred = 0;
            }

            // Call the completion callback
            if (context->callback) {
                context->callback(context->error_code, context->bytes_transferred);
            }

            // Clean up buffer if allocated
            if (context->buffer) {
                delete[] context->buffer;
                context->buffer = nullptr;
            }

            // Remove fd from epoll after handling
            RemoveFd(context->fd, nullptr);
        }
    }
}

EpollAsyncManager& GetGlobalEpollManager() {
    static EpollAsyncManager instance;
    return instance;
}

} // namespace XER::Network::Async::Posix

#endif // __linux__ || __ANDROID__
