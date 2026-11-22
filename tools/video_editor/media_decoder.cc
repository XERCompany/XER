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
#include "gpu_video/cuda_decode.h"  // YA.CUDADecode — runtime-probed hwaccel

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include "io/still_image.h"
#include "helper/path_utils.h"

#include <cctype>

namespace XER::VideoEditor {
namespace Path = XER::Helper::Path;
namespace {

namespace FFmpeg = XER::Bridge::FFmpeg;

// Production IMediaDecoder: one instance wraps one
// XER::Bridge::FFmpeg::VideoDecoder held BY VALUE — its destructor
// calls Close(), so decoder lifetime is RAII. Every method forwards
// unchanged; only NextFrame adapts the bridge's reference-out
// signature to pointers.
// Nominal still-image duration/fps — matches the catalog's still
// import defaults; seeking just clamps, every frame is the same.
constexpr double kStillDurationSeconds = 5.0;
constexpr double kStillFps = 25.0;

// Still-image fallback shared by both decoder adapters — kept as a
// free helper so BridgeVideoDecoder and GpuVideoDecoder stay
// behavior-identical on non-video inputs.
bool TryOpenStill(const std::string& utf8_path,
                  std::vector<std::uint8_t>* bgra, int* w, int* h,
                  std::string* error_out) {
    const std::filesystem::path path = Path::FromUtf8(utf8_path);
    std::string ext = path.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));
    if (!Io::StillImageHandlesExtension(ext)) {
        return false;
    }
    const Io::Image image = Io::ReadImage(path);
    if (!image.ok || image.bgra.empty() || image.width <= 0 ||
        image.height <= 0) {
        if (error_out) *error_out = "still image decode failed";
        return false;
    }
    *bgra = image.bgra;
    *w = image.width;
    *h = image.height;
    if (error_out) error_out->clear();
    return true;
}

class BridgeVideoDecoder final : public IMediaDecoder {
public:
    bool Open(const std::string& utf8_path,
              std::string* error_out) override {
        still_ = false;
        still_bgra_.clear();
        still_pos_ = 0.0;
        if (decoder_.Open(utf8_path, error_out)) {
            return true;
        }
        // FFmpeg couldn't parse it — try the still-image formats
        // (EXR/DPX/TIFF/RAW/PSD/HDR via ImageLoader + the narrow OIIO
        // residual). The still is a constant frame: every NextFrame
        // returns the same picture.
        if (!TryOpenStill(utf8_path, &still_bgra_, &still_w_,
                          &still_h_, error_out)) {
            return false;
        }
        still_ = true;
        return true;
    }

    void Close() override {
        decoder_.Close();
        still_ = false;
        still_bgra_.clear();
        still_pos_ = 0.0;
    }

    bool IsOpen() const override { return still_ || decoder_.IsOpen(); }

    bool NextFrame(std::vector<std::uint8_t>* bgra,
                   int* width, int* height) override {
        // The three out-params are required by the IMediaDecoder
        // contract — a null call is a programming error, report it as
        // a failed decode rather than dereferencing.
        if (bgra == nullptr || width == nullptr || height == nullptr) {
            return false;
        }
        if (still_) {
            *bgra = still_bgra_;
            *width = still_w_;
            *height = still_h_;
            return true;
        }
        // Bridge signature is (std::vector<uint8_t>&, int&, int&) —
        // *bgra/*width/*height bind directly.
        return decoder_.NextFrame(*bgra, *width, *height);
    }

    bool Seek(double seconds) override {
        if (still_) {
            still_pos_ = std::clamp(seconds, 0.0, kStillDurationSeconds);
            return true;
        }
        return decoder_.Seek(seconds);
    }

    double GetPositionSeconds() const override {
        return still_ ? still_pos_ : decoder_.GetPositionSeconds();
    }

    double GetFps() const override {
        return still_ ? kStillFps : decoder_.GetFps();
    }

    double GetDurationSeconds() const override {
        return still_ ? kStillDurationSeconds
                      : decoder_.GetDurationSeconds();
    }

    int GetWidth() const override {
        return still_ ? still_w_ : decoder_.GetWidth();
    }

    int GetHeight() const override {
        return still_ ? still_h_ : decoder_.GetHeight();
    }

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
    // Still-image fallback: a single decoded frame replayed
    // forever. Active only when FFmpeg couldn't open the file.
    bool still_ = false;
    std::vector<std::uint8_t> still_bgra_;
    int still_w_ = 0;
    int still_h_ = 0;
    double still_pos_ = 0.0;
};

// GPU-backed IMediaDecoder over GpuVideo::CudaVideoDecoder
// (YA.CUDADecode). Same contract as BridgeVideoDecoder — the wrapped
// decoder degrades to software decode internally when the CUDA hwaccel
// cannot run, so delegation never does worse than the CPU path. Only
// created when ProbeCudaDecode().Available() — see the factory below.
class GpuVideoDecoder final : public IMediaDecoder {
public:
    bool Open(const std::string& utf8_path,
              std::string* error_out) override {
        still_ = false;
        still_bgra_.clear();
        still_pos_ = 0.0;
        if (decoder_.Open(utf8_path, error_out)) {
            return true;
        }
        if (!TryOpenStill(utf8_path, &still_bgra_, &still_w_,
                          &still_h_, error_out)) {
            return false;
        }
        still_ = true;
        if (error_out) error_out->clear();
        return true;
    }

    void Close() override {
        decoder_.Close();
        still_ = false;
        still_bgra_.clear();
        still_pos_ = 0.0;
    }

    bool IsOpen() const override { return still_ || decoder_.IsOpen(); }

    bool NextFrame(std::vector<std::uint8_t>* bgra,
                   int* width, int* height) override {
        if (bgra == nullptr || width == nullptr || height == nullptr) {
            return false;
        }
        if (still_) {
            *bgra = still_bgra_;
            *width = still_w_;
            *height = still_h_;
            return true;
        }
        // The module produces video::Frame (BGRA8); the IMediaDecoder
        // contract speaks in a borrowed vector — swap the payload
        // instead of copying a second time.
        video::Frame frame;
        if (!decoder_.NextFrame(&frame)) {
            return false;
        }
        *width = frame.Width();
        *height = frame.Height();
        bgra->assign(frame.Data(),
                     frame.Data() + frame.DataSize());
        return true;
    }

    bool Seek(double seconds) override {
        if (still_) {
            still_pos_ = std::clamp(seconds, 0.0, kStillDurationSeconds);
            return true;
        }
        return decoder_.Seek(seconds);
    }

    double GetPositionSeconds() const override {
        return still_ ? still_pos_ : decoder_.GetPositionSeconds();
    }

    double GetFps() const override {
        return still_ ? kStillFps : decoder_.GetFps();
    }

    double GetDurationSeconds() const override {
        return still_ ? kStillDurationSeconds
                      : decoder_.GetDurationSeconds();
    }

    int GetWidth() const override {
        return still_ ? still_w_ : decoder_.GetWidth();
    }

    int GetHeight() const override {
        return still_ ? still_h_ : decoder_.GetHeight();
    }

    void EnableAudioTap(bool enable) override {
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
    XER::GpuVideo::CudaVideoDecoder decoder_;
    bool still_ = false;
    std::vector<std::uint8_t> still_bgra_;
    int still_w_ = 0;
    int still_h_ = 0;
    double still_pos_ = 0.0;
};

}  // namespace

MediaDecoderPtr MakeBridgeDecoder() {
    return std::make_unique<BridgeVideoDecoder>();
}

MediaDecoderPtr MakeGpuDecoder() {
    return std::make_unique<GpuVideoDecoder>();
}

MediaDecoderFactory DefaultDecoderFactory() {
    return MediaDecoderFactory{[] {
        // Decoder backend selection — XER_VE_DECODER=cpu pins the plain
        // FFmpeg bridge; =cuda forces the YA.CUDADecode session (which
        // still degrades per-file to software decode); unset = auto:
        // delegate to the GPU path only when the runtime probe reports
        // a usable CUDA hwaccel, else stay on the proven CPU path.
        const char* mode = std::getenv("XER_VE_DECODER");
        if (mode != nullptr && std::string(mode) == "cpu") {
            return MakeBridgeDecoder();
        }
        if (mode != nullptr && std::string(mode) == "cuda") {
            return MakeGpuDecoder();
        }
        if (XER::GpuVideo::ProbeCudaDecode().Available()) {
            return MakeGpuDecoder();
        }
        return MakeBridgeDecoder();
    }};
}

}  // namespace XER::VideoEditor
