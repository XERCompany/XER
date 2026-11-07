#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace XER::Content::Video {

// FFmpeg-based video decoder producing BGRA frames. Decode is synchronous —
// call NextFrame() from a worker thread; frames are NOT thread-safe to share
// except through the copied output buffer.
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool Open(const std::string& path, std::string* error_out = nullptr);
    void Close();
    bool IsOpen() const;

    // Decodes the next frame into `bgra` (resized by caller-owned vector).
    // Returns false at end of stream or on fatal errors — callers that want
    // looping must Seek(0) themselves.
    bool NextFrame(std::vector<uint8_t>& bgra, int& width, int& height);

    // Seeks to approximately `seconds` (keyframe-accurate; decode continues
    // forward from there). False when not open or the seek fails.
    bool Seek(double seconds);

    // PTS (seconds) of the last decoded frame, -1 when unknown.
    double GetPositionSeconds() const;

    // True when the media also carries an audio stream.
    bool HasAudio() const;

    double GetFps() const;
    double GetDurationSeconds() const;
    int GetWidth() const;
    int GetHeight() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::Content::Video
