#include "javascript/common/uv_async_channel.h"

#include <utility>

namespace XER::JavaScript::Common {

UvAsyncChannel::UvAsyncChannel() = default;

UvAsyncChannel::~UvAsyncChannel() {
    Stop();
}

bool UvAsyncChannel::Start(uv_loop_t* loop) {
    if (running_ || loop == nullptr) {
        return false;
    }

    loop_ = loop;
    async_.data = this;
    if (uv_async_init(loop_, &async_, &UvAsyncChannel::OnAsync) != 0) {
        loop_ = nullptr;
        return false;
    }

    running_ = true;
    return true;
}

void UvAsyncChannel::Stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    uv_close(reinterpret_cast<uv_handle_t*>(&async_), nullptr);

    std::scoped_lock lock(mutex_);
    while (!tasks_.empty()) {
        tasks_.pop();
    }
}

bool UvAsyncChannel::Post(std::function<void()> task) {
    if (!running_ || !task) {
        return false;
    }

    {
        std::scoped_lock lock(mutex_);
        tasks_.push(std::move(task));
    }

    return uv_async_send(&async_) == 0;
}

void UvAsyncChannel::OnAsync(uv_async_t* handle) {
    auto* self = static_cast<UvAsyncChannel*>(handle->data);
    if (self != nullptr) {
        self->DrainPending();
    }
}

void UvAsyncChannel::DrainPending() {
    std::queue<std::function<void()>> local;
    {
        std::scoped_lock lock(mutex_);
        std::swap(local, tasks_);
    }

    while (!local.empty()) {
        std::function<void()> task = std::move(local.front());
        local.pop();
        if (task) {
            task();
        }
    }
}

}  // namespace XER::JavaScript::Common
