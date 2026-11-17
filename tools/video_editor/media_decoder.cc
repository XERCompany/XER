// media_decoder.cc — see media_decoder.h for the contract.
// BridgeVideoDecoder is a pure pass-through adapter over
// XER::Bridge::FFmpeg::VideoDecoder: it adds no state, no locking and
// no behavior of its own — the decode/audio-tap semantics documented
// on IMediaDecoder are exactly ffmpeg_video_decoder.h's. Off-Windows
// the bridge methods are stubs returning false/0, so this file needs
// no platform ifdefs (same pattern as clip_poster.cc).
#include "media_decoder.h"

#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"  // AudioFrameInfo def
#include "wrapper/ffmpeg/ffmpeg_video_decoder.h"

namespace XER::VideoEditor {
namespace {

namespace FFmpeg = XER::Bridge::FFmpeg;

// Production IMediaDecoder: one instance wraps one
// XER::Bridge::FFmpeg::VideoDecoder held BY VALUE — its destructor
// calls Close(), so decoder lifetime is RAII. Every method forwards
// unchanged; only NextFrame adapts the bridge's reference-out
// signature to pointers.
class BridgeVideoDecoder final : public IMediaDecoder {
public:
    bool Open(const std::string& utf8_path,
              std::string* error_out) override {
        return decoder_.Open(utf8_path, error_out);
    }

    void Close() override { decoder_.Close(); }

    bool IsOpen() const override { return decoder_.IsOpen(); }

    bool NextFrame(std::vector<std::uint8_t>* bgra,
                   int* width, int* height) override {
        // The three out-params are required by the IMediaDecoder
        // contract — a null call is a programming error, report it as
        // a failed decode rather than dereferencing.
        if (bgra == nullptr || width == nullptr || height == nullptr) {
            return false;
        }
        // Bridge signature is (std::vector<uint8_t>&, int&, int&) —
        // *bgra/*width/*height bind directly.
        return decoder_.NextFrame(*bgra, *width, *height);
    }

    bool Seek(double seconds) override { return decoder_.Seek(seconds); }

    double GetPositionSeconds() const override {
        return decoder_.GetPositionSeconds();
    }

    double GetFps() const override { return decoder_.GetFps(); }

    double GetDurationSeconds() const override {
        return decoder_.GetDurationSeconds();
    }

    int GetWidth() const override { return decoder_.GetWidth(); }

    int GetHeight() const override { return decoder_.GetHeight(); }

    void EnableAudioTap(bool enable) override {
        // Ordering constraint (EnableAudioTap BEFORE Open) is enforced
        // by the wrapped decoder itself — impl_->audio_tap is sampled
        // during Open()'s PrepareAudioStream().
        decoder_.EnableAudioTap(enable);
    }

    bool HasAudio() const override { return decoder_.HasAudio(); }

    double GetAudioStartSeconds() const override {
        return decoder_.GetAudioStartSeconds();
    }

    double GetAudioTimeBase() const override {
        return decoder_.GetAudioTimeBase();
    }

    bool ReadAudioFrames(
        std::vector<FFmpeg::AudioFrameInfo>* out,
        int max_frames) override {
        return decoder_.ReadAudioFrames(out, max_frames);
    }

    bool AudioDrained() const override { return decoder_.AudioDrained(); }

    std::uint64_t GetAudioEpoch() const override {
        return decoder_.GetAudioEpoch();
    }

private:
    FFmpeg::VideoDecoder decoder_;
};

}  // namespace

MediaDecoderPtr MakeBridgeDecoder() {
    return std::make_unique<BridgeVideoDecoder>();
}

MediaDecoderFactory DefaultDecoderFactory() {
    return MediaDecoderFactory{[] { return MakeBridgeDecoder(); }};
}

}  // namespace XER::VideoEditor
