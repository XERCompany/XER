/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#ifdef _WIN32

#include "content/network/async/iocp/iocp_completion_port.h"

#include <thread>

namespace XER::network::async::iocp {

IocpCompletionPort::IocpCompletionPort()
    : completion_port_(nullptr),
      completion_thread_(nullptr),
      running_(false),
      should_stop_(false) {
}

IocpCompletionPort::~IocpCompletionPort() {
    Shutdown();
}

bool IocpCompletionPort::Initialize(std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (completion_port_ != nullptr) {
        if (error) *error = "Completion port already initialized";
        return false;
    }

    // Create I/O completion port
    completion_port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (completion_port_ == nullptr) {
        if (error) *error = "CreateIoCompletionPort failed: " + std::to_string(GetLastError());
        return false;
    }

    return true;
}

void IocpCompletionPort::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        StopCompletionThread();
    }

    if (completion_port_ != nullptr) {
        CloseHandle(completion_port_);
        completion_port_ = nullptr;
    }
}

bool IocpCompletionPort::RegisterSocket(SOCKET socket,
                                      ULONG_PTR completion_key,
                                      std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (completion_port_ == nullptr) {
        if (error) *error = "Completion port not initialized";
        return false;
    }

    HANDLE result = CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket),
                                          completion_port_,
                                          completion_key,
                                          0);
    if (result == nullptr) {
        if (error) *error = "CreateIoCompletionPort registration failed: " + std::to_string(GetLastError());
        return false;
    }

    return true;
}

bool IocpCompletionPort::PostCompletion(ULONG_PTR completion_key,
                                        DWORD bytes_transferred,
                                        OVERLAPPED* overlapped,
                                        std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (completion_port_ == nullptr) {
        if (error) *error = "Completion port not initialized";
        return false;
    }

    BOOL result = PostQueuedCompletionStatus(completion_port_,
                                              bytes_transferred,
                                              completion_key,
                                              overlapped);
    if (!result) {
        if (error) *error = "PostQueuedCompletionStatus failed: " + std::to_string(GetLastError());
        return false;
    }

    return true;
}

bool IocpCompletionPort::GetCompletionStatus(DWORD* bytes_transferred,
                                             ULONG_PTR* completion_key,
                                             OVERLAPPED** overlapped,
                                             DWORD milliseconds,
                                             std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (completion_port_ == nullptr) {
        if (error) *error = "Completion port not initialized";
        return false;
    }

    BOOL result = GetQueuedCompletionStatus(completion_port_,
                                             bytes_transferred,
                                             completion_key,
                                             overlapped,
                                             milliseconds);
    if (!result) {
        DWORD error_code = GetLastError();
        if (error_code != WAIT_TIMEOUT) {
            if (error) *error = "GetQueuedCompletionStatus failed: " + std::to_string(error_code);
            return false;
        }
    }

    return true;
}

void IocpCompletionPort::StartCompletionThread() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        return;  // Already running
    }

    should_stop_ = false;
    running_ = true;

    completion_thread_ = CreateThread(nullptr,
                                       0,
                                       [](LPVOID param) -> DWORD {
                                           auto* port = static_cast<IocpCompletionPort*>(param);
                                           port->CompletionThreadLoop();
                                           return 0;
                                       },
                                       this,
                                       0,
                                       nullptr);

    if (completion_thread_ == nullptr) {
        running_ = false;
    }
}

void IocpCompletionPort::StopCompletionThread() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_) {
        return;
    }

    should_stop_ = true;

    // Post a dummy completion to wake up the thread
    PostQueuedCompletionStatus(completion_port_, 0, 0, nullptr);

    // Wait for thread to finish
    if (completion_thread_ != nullptr) {
        WaitForSingleObject(completion_thread_, INFINITE);
        CloseHandle(completion_thread_);
        completion_thread_ = nullptr;
    }

    running_ = false;
}

void IocpCompletionPort::CompletionThreadLoop() {
    DWORD bytes_transferred = 0;
    ULONG_PTR completion_key = 0;
    OVERLAPPED* overlapped = nullptr;

    while (!should_stop_) {
        BOOL result = GetQueuedCompletionStatus(completion_port_,
                                                 &bytes_transferred,
                                                 &completion_key,
                                                 &overlapped,
                                                 100);  // 100ms timeout

        if (should_stop_) {
            break;
        }

        if (!result) {
            DWORD error_code = GetLastError();
            if (error_code == WAIT_TIMEOUT) {
                continue;  // Timeout, loop again
            }

            // Handle error
            if (overlapped != nullptr) {
                // Extract context and call callback with error
                auto* context = reinterpret_cast<IocpOverlappedContext*>(
                    reinterpret_cast<char*>(overlapped) -
                    offsetof(IocpOverlappedContext, overlapped));

                if (context->callback) {
                    context->callback(error_code, 0);
                }

                // Clean up context
                delete[] context->buffer;
                delete context;
            }
            continue;
        }

        if (overlapped == nullptr) {
            // Dummy completion, just continue
            continue;
        }

        // Extract context from overlapped structure
        auto* context = reinterpret_cast<IocpOverlappedContext*>(
            reinterpret_cast<char*>(overlapped) -
            offsetof(IocpOverlappedContext, overlapped));

        context->bytes_transferred = bytes_transferred;
        context->error_code = 0;

        // Call the completion callback
        if (context->callback) {
            context->callback(context->error_code, context->bytes_transferred);
        }

        // Clean up context
        delete[] context->buffer;
        delete context;
    }
}

IocpCompletionPort& GetGlobalIocp() {
    static IocpCompletionPort instance;
    return instance;
}

} // namespace XER::network::async::iocp

#endif // _WIN32
