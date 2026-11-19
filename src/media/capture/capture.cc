#include "media/capture/capture.h"

#include "media/audio/audio_input_device.h"

#ifdef _WIN32
#include "media/capture/screen_capturer_gdi.h"
#endif

namespace XER::Media {
namespace {

#ifndef _WIN32
class NullVideoCapturer final : public VideoCapturer {
public:
    bool Start(const std::string&, Callback cb,
               std::string* error_out) override {
        cb_ = std::move(cb);
        if (error_out) *error_out = "video capture backend not available";
        return false;
    }
    void Stop() override {}
    std::vector<std::string> EnumerateTargets() override { return {}; }

private:
    Callback cb_;
};
#endif

class CaptureImpl final : public Capture {
public:
    std::unique_ptr<AudioCapturerSource> CreateAudioSource() override {
        return std::make_unique<AudioInputDevice>();
    }
    std::unique_ptr<VideoCapturer> CreateVideoCapturer() override {
#ifdef _WIN32
        return std::make_unique<ScreenCapturerGdi>();
#else
        return std::make_unique<NullVideoCapturer>();
#endif
    }
};

}  // namespace

Capture& Capture::Instance() {
    static CaptureImpl impl;
    return impl;
}

}  // namespace XER::Media
