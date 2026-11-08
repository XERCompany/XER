#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace XER::Bridge::FFmpeg {

struct AudioFrameInfo;  // defined in ffmpeg_engine_bridge.h

// Streaming video decode session producing packed BGRA frames.
// This is the FFmpeg-backed implementation used by XER::Content::Video::VideoDecoder
// and any other consumer that needs frame-by-frame decode; all libav*/libyuv
// usage for this path lives here in the wrapper.
//
// Decode is synchronous — call NextFrame() from a worker thread; frames are
// NOT thread-safe to share except through the copied output buffer.
//
// Audio tap: when EnableAudioTap() is set before Open(), packets of the
// container's audio stream are queued during NextFrame() instead of being
// dropped, and ReadAudioFrames() decodes them on a second thread. Both sides
// share the single AVFormatContext opened by Open() — the file is demuxed
// exactly once. Seek() and Close() flush the audio side as well.
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // Call before Open() to route the audio stream into the packet queue.
    void EnableAudioTap(bool enable = true);

    bool Open(const std::string& path, std::string* error_out = nullptr);
    void Close();
    bool IsOpen() const;

    // Decodes the next frame into `bgra` (resized by caller-owned vector).
    // Returns false at end of stream or on fatal errors — callers that want
    // looping must Seek(0) themselves.
    bool NextFrame(std::vector<uint8_t>& bgra, int& width, int& height);

    // Seeks to approximately `seconds`: lands on the nearest keyframe at or
    // before the target and flushes decoder state, so the next NextFrame()
    // calls decode forward from there. Out-of-range values clamp to the
    // stream bounds. Returns false when not open or the seek fails.
    bool Seek(double seconds);

    // Presentation timestamp (seconds) of the most recently decoded frame,
    // or -1 before the first frame / when the stream has no timestamps.
    double GetPositionSeconds() const;

    // True when the container also carries an audio stream (probed at Open).
    bool HasAudio() const;

    double GetFps() const;
    double GetDurationSeconds() const;
    int GetWidth() const;
    int GetHeight() const;

    // ---- Audio tap API (audio worker thread) ----

    // Start offset (seconds) of the audio stream timeline — PCM produced by
    // ReadAudioFrames() begins at this media time.
    double GetAudioStartSeconds() const;

    // Audio stream timestamp units: pts * GetAudioTimeBase() = media
    // seconds. 0 when there is no audio stream.
    double GetAudioTimeBase() const;

    // Decodes queued audio packets into `out` (appended, up to max_frames).
    // Returns false only on decode errors; an empty result means "wait for
    // more packets or check AudioDrained()". Safe to call concurrently with
    // NextFrame()/Seek() — internal locking isolates the audio codec.
    bool ReadAudioFrames(std::vector<AudioFrameInfo>* out, int max_frames);

    // True once the demuxer hit EOF and the audio decoder fully drained —
    // no more audio will ever arrive for this session (until Seek()).
    bool AudioDrained() const;

    // Monotonic counter bumped every time the audio side is flushed
    // (Seek/Close). Audio consumers stamp decoded chunks with it to drop
    // stale pre-seek data that was still in flight.
    std::uint64_t GetAudioEpoch() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::Bridge::FFmpeg
