/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/async/async_operation_manager.h"

namespace XER::network::async {

AsyncOperationManager::AsyncOperationManager()
    : thread_pool_(XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance()) {
}

AsyncOperationManager& AsyncOperationManager::Instance() {
    static AsyncOperationManager instance;
    return instance;
}

void AsyncOperationManager::Enqueue(std::function<void()> operation) {
    std::lock_guard<std::mutex> lock(mutex_);
    thread_pool_.Enqueue(std::move(operation));
}

XER::AsyncIO::IO::Async::IOThreadPool& AsyncOperationManager::GetThreadPool() {
    return thread_pool_;
}

void AsyncOperationManager::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!thread_pool_.IsRunning()) {
        thread_pool_.Start();
    }
}

void AsyncOperationManager::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (thread_pool_.IsRunning()) {
        thread_pool_.Stop();
    }
}

size_t AsyncOperationManager::GetPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return thread_pool_.GetPendingTaskCount();
}

bool AsyncOperationManager::IsRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return thread_pool_.IsRunning();
}

void AsyncOperationWrapper::WrapWithCallback(std::function<AsyncResult()> operation,
                                            AsyncCallback callback) {
    AsyncOperationManager::Instance().Enqueue([operation = std::move(operation),
                                               callback = std::move(callback)]() {
        AsyncResult result = operation();
        callback(result.error_code, result.bytes_transferred);
    });
}

void AsyncOperationWrapper::WrapVoid(std::function<void()> operation,
                                    AsyncCallback callback) {
    AsyncOperationManager::Instance().Enqueue([operation = std::move(operation),
                                               callback = std::move(callback)]() {
        operation();
        callback(0, 0);  // Success
    });
}

} // namespace XER::network::async
