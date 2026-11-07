/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/core/network_task_runner.h"
#include "async_io/io_thread_pool.h"

namespace network::core {

NetworkTaskRunner& NetworkTaskRunner::Instance() {
    static NetworkTaskRunner inst;
    return inst;
}

void NetworkTaskRunner::PostTask(std::function<void()> task) {
    AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue(std::move(task));
}

}  // namespace network::core
