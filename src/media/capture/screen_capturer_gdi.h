/**
 * ScreenCapturerGdi — desktop pixel capture via GDI BitBlt (Windows).
 * Implements VideoCapturer: captures the primary display (target_id ""
 * or "primary") on a timer thread and pushes CapturedVideoFrame BGRA8.
 */
#pragma once

#include <atomic>
#include <thread>

#include "media/capture/capture.h"

namespace XER::Media {

class ScreenCapturerGdi final : public VideoCapturer {
public:
    explicit ScreenCapturerGdi(int fps = 15) : fps_(fps > 0 ? fps : 15) {}
    ~ScreenCapturerGdi() override { Stop(); }

    bool Start(const std::string& target_id, Callback cb,
               std::string* error_out) override;
    void Stop() override;
    std::vector<std::string> EnumerateTargets() override;

    // Single-frame grab (also used by tests). Returns false w/ error.
    static bool CaptureOnce(CapturedVideoFrame* out, std::string* error_out);

private:
    int fps_;
    Callback cb_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace XER::Media
