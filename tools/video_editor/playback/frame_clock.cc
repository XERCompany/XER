// frame_clock.cc — see frame_clock.h for the contract.
#include "frame_clock.h"

#include <chrono>
#include <thread>

namespace XER::VideoEditor {
namespace {

class SystemFrameClock final : public FrameClock {
public:
    SystemFrameClock() : epoch_(std::chrono::steady_clock::now()) {}

    double NowSeconds() const override {
        return std::chrono::duration<double>(
                   std::chrono::steady_clock::now() - epoch_)
            .count();
    }

    void SleepSeconds(double seconds) override {
        if (seconds <= 0.0) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    }

private:
    std::chrono::steady_clock::time_point epoch_;
};

}  // namespace

std::shared_ptr<FrameClock> MakeSystemFrameClock() {
    return std::make_shared<SystemFrameClock>();
}

}  // namespace XER::VideoEditor
