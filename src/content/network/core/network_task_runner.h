/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <functional>

namespace XER::network::core {

// Thin wrapper over IOThreadPool for posting network tasks.
class NetworkTaskRunner {
public:
    static NetworkTaskRunner& Instance();

    void PostTask(std::function<void()> task);

private:
    NetworkTaskRunner() = default;
};

}  // namespace XER::network::core
