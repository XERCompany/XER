#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace video {

enum class PixelFormat : uint8_t { BGRA8 = 0, RGBA8 = 1, NV12 = 2, YUV420P = 3 };

struct FrameMetadata {
    int width{0};
    int height{0};
    PixelFormat format{PixelFormat::BGRA8};
    int64_t pts{0};   ///< presentation timestamp (microseconds)
    int64_t dts{0};   ///< decode timestamp
    double fps{0.0};
    bool is_keyframe{false};
};

class Frame {
public:
    Frame() = default;
    Frame(int w, int h, PixelFormat fmt);
    ~Frame() = default;

    bool Allocate(int w, int h, PixelFormat fmt);
    // Adopts a caller-owned packed pixel buffer (moves it in — no
    // copy), replacing the current contents. `pixels.size()` must be
    // exactly the format's byte count for w*h (w*h*4 for BGRA8/RGBA8,
    // w*h*3/2 for NV12/YUV420P); a mismatch fails and releases the
    // frame. Use when the producer already owns a sized buffer — it
    // kills the Allocate+memcpy pair on hot hand-off paths.
    bool Adopt(int w, int h, PixelFormat fmt,
               std::vector<std::uint8_t>&& pixels);
    void Release();
    bool IsValid() const { return !data_.empty(); }

    int Width()  const { return meta_.width; }
    int Height() const { return meta_.height; }
    PixelFormat Format() const { return meta_.format; }
    int64_t Pts() const { return meta_.pts; }
    void SetPts(int64_t pts) { meta_.pts = pts; }

    uint8_t*       Data()       { return data_.data(); }
    const uint8_t* Data() const { return data_.data(); }
    size_t         DataSize() const { return data_.size(); }

    int Stride() const;   ///< bytes per row for plane 0
    const FrameMetadata& Meta() const { return meta_; }
    FrameMetadata& Meta() { return meta_; }

private:
    FrameMetadata meta_;
    std::vector<uint8_t> data_;
};

}  // namespace video