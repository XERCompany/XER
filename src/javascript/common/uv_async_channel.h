#pragma once

#include <uv.h>

#include <functional>
#include <mutex>
#include <queue>

namespace XER::JavaScript::Common {

class UvAsyncChannel {
public:
    UvAsyncChannel();
    ~UvAsyncChannel();

    UvAsyncChannel(const UvAsyncChannel&) = delete;
    UvAsyncChannel& operator=(const UvAsyncChannel&) = delete;

    bool Start(uv_loop_t* loop);
    void Stop();
    bool Post(std::function<void()> task);

private:
    static void OnAsync(uv_async_t* handle);
    void DrainPending();

    uv_async_t async_{};
    uv_loop_t* loop_ = nullptr;
    std::mutex mutex_;
    std::queue<std::function<void()>> tasks_;
    bool running_ = false;
};

}  // namespace XER::JavaScript::Common
