#include "media/audio/audio_device_thread.h"

#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

namespace XER::Media {

bool AudioDeviceThread::Start(Callback cb, std::uint32_t period_us) {
    if (running_.exchange(true)) return false;
    thread_ = std::thread([this, cb = std::move(cb), period_us] {
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
        auto next = std::chrono::steady_clock::now();
        while (running_.load()) {
            cb();
            next += std::chrono::microseconds(period_us);
            std::this_thread::sleep_until(next);
        }
    });
    return true;
}

void AudioDeviceThread::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

}  // namespace XER::Media
