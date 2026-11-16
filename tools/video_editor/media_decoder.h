// Frame/audio decode abstraction for the VideoEditor core (XER Studio
// Phase 1C). One instance == one open media file. The compositor and
// mixer consume IMediaDecoder so their logic (track stacking,
// transitions, transforms, PTS selection, gain/fade envelopes) stays
// unit-testable WITHOUT FFmpeg — tests inject fakes through
// MediaDecoderFactory; production uses MakeBridgeDecoder() /
// DefaultDecoderFactory() over XER::Bridge::FFmpeg::VideoDecoder.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace XER::Bridge::FFmpeg {
struct AudioFrameInfo;  // fwd — full def lives in
                        // ffmpeg_engine_bridge.h; callers that touch
                        // audio fields include that header themselves.
}  // namespace XER::Bridge::FFmpeg

namespace XER::VideoEditor {

// Frame/audio decode abstraction over XER::Bridge::FFmpeg::VideoDecoder.
// Mirrors its contract exactly: NextFrame() decodes forward and
// reports the decoded frame's media time via GetPositionSeconds()
// (call it AFTER a successful NextFrame — Seek() presets it to the
// requested target until the next frame lands); with the audio tap
// enabled, NextFrame() also pumps demux packets into the tap queue —
// audio-only files return false but still pump (bounded batch/call);
// drive audio by alternating NextFrame()/ReadAudioFrames() until
// AudioDrained(). NOT internally synchronized — one consumer thread.
class IMediaDecoder {
public:
    virtual ~IMediaDecoder() = default;

    virtual bool Open(const std::string& utf8_path,
                      std::string* error_out = nullptr) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;

    // ---- video ----
    // NextFrame fills `bgra` with packed BGRA8 (B,G,R,A byte order,
    // stride == width*4), `width`/`height` with the decoded dims, and
    // returns true; false at EOF/fatal error. All three out-params are
    // REQUIRED (non-null) — passing nullptr is a contract violation.
    virtual bool NextFrame(std::vector<std::uint8_t>* bgra,
                           int* width, int* height) = 0;
    virtual bool Seek(double seconds) = 0;
    virtual double GetPositionSeconds() const = 0;  // last frame's pts
    virtual double GetFps() const = 0;
    virtual double GetDurationSeconds() const = 0;
    virtual int GetWidth() const = 0;    // 0 = audio-only container
    virtual int GetHeight() const = 0;

    // ---- audio tap (EnableAudioTap BEFORE Open) ----
    // EnableAudioTap must be called before Open() to route the
    // container's audio stream into the tap queue; calling it after
    // Open has no effect on the already-probed session.
    virtual void EnableAudioTap(bool enable = true) = 0;
    virtual bool HasAudio() const = 0;
    virtual double GetAudioStartSeconds() const = 0;
    virtual double GetAudioTimeBase() const = 0;  // s per pts tick; 0 none
    virtual bool ReadAudioFrames(
        std::vector<XER::Bridge::FFmpeg::AudioFrameInfo>* out,
        int max_frames) = 0;
    virtual bool AudioDrained() const = 0;
    virtual std::uint64_t GetAudioEpoch() const = 0;
};

using MediaDecoderPtr = std::unique_ptr<IMediaDecoder>;
// Called with no args; the returned decoder is Open()ed by the caller.
// Production default wraps XER::Bridge::FFmpeg::VideoDecoder.
using MediaDecoderFactory = std::function<MediaDecoderPtr()>;

// Production factory — BridgeVideoDecoder (defined in media_decoder.cc).
MediaDecoderPtr MakeBridgeDecoder();
MediaDecoderFactory DefaultDecoderFactory();

}  // namespace XER::VideoEditor
