/**
 * XER::Media::Capture (design doc). "Capture" covers the features and UI
 * that let XER Desgin capture pixels and audio from itself or the OS, plus
 * the Web-facing APIs for Desgin UI applications to obtain captured media.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "media/base/media_types.h"

namespace XER::Media {

struct CapturedAudioBuffer {
    std::vector<float> samples;  // interleaved
    int sample_rate = 0;
    int channels = 0;
    TimeTicks timestamp = kNoTimestamp;
};

struct CapturedVideoFrame {
    std::vector<std::uint8_t> pixels;  // BGRA8
    int width = 0;
    int height = 0;
    int stride = 0;
    TimeTicks timestamp = kNoTimestamp;
};

// Push-model audio source (mirrors AudioInputDevice::AudioCapturerSource).
class AudioCapturerSource {
public:
    using Callback = std::function<void(CapturedAudioBuffer)>;
    virtual ~AudioCapturerSource() = default;
    virtual bool Start(Callback cb, std::string* error_out) = 0;
    virtual void Stop() = 0;
};

// Pixel capture of self or OS surfaces (screen/window/tab).
class VideoCapturer {
public:
    using Callback = std::function<void(CapturedVideoFrame)>;
    virtual ~VideoCapturer() = default;
    virtual bool Start(const std::string& target_id, Callback cb,
                       std::string* error_out) = 0;
    virtual void Stop() = 0;
    virtual std::vector<std::string> EnumerateTargets() = 0;
};

// Umbrella entry point for capture features.
class Capture {
public:
    static Capture& Instance();
    virtual ~Capture() = default;
    virtual std::unique_ptr<AudioCapturerSource> CreateAudioSource() = 0;
    virtual std::unique_ptr<VideoCapturer> CreateVideoCapturer() = 0;
};

}  // namespace XER::Media
