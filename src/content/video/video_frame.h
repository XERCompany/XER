#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace XER::Content::Video {

// One decoded video frame in packed 32-bit BGRA — the pixel order D2D/D3D
// expects (bytes B, G, R, A per pixel), produced by the FFmpeg wrapper.
struct VideoFrame {
    std::vector<std::uint8_t> bgra;
    int width = 0;
    int height = 0;
    double position_seconds = -1.0;  // frame PTS; -1 when the stream has none
    std::uint64_t sequence = 0;      // session-scoped, monotonically increasing
};

// Shared ownership lets the session publish frames without copying pixel
// data; renderers hold the pointer only for the duration of a draw.
using VideoFramePtr = std::shared_ptr<const VideoFrame>;

}  // namespace XER::Content::Video
