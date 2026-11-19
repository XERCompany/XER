/**
 * AudioDeviceThread — real-time audio thread (design doc: callbacks run
 * on high-priority threads; code there must not block, allocate, or call
 * back into the stream). The thread loops a fixed-interval Callback.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace XER::Media {

class AudioDeviceThread {
public:
    // Invoked each period on the device thread. Must respect RT rules.
    using Callback = std::function<void()>;

    AudioDeviceThread() = default;
    ~AudioDeviceThread() { Stop(); }
    AudioDeviceThread(const AudioDeviceThread&) = delete;
    AudioDeviceThread& operator=(const AudioDeviceThread&) = delete;

    // Starts a thread ticking |cb| every |period_us| microseconds. The
    // thread is bumped to realtime priority where the OS allows.
    bool Start(Callback cb, std::uint32_t period_us);
    void Stop();
    bool running() const { return running_.load(); }

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace XER::Media
