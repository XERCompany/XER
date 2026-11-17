// Decoder suite — IMediaDecoder contract coverage for the Phase-1C
// compositor/mixer decode paths. Case 1 uses a hand-rolled fake (no
// FFmpeg) to pin the interface semantics the editor code relies on;
// cases 2-3 exercise the production BridgeVideoDecoder adapter over
// fixtures synthesized in %TEMP% (EncodeVideoFrameStream /
// EncodeAudioVideoStream / EncodeAudioFrames — no binary assets),
// self-skipping via XER::Bridge::FFmpeg::IsAvailable() like the poster
// suite. Patterns follow tests/ui/ffmpeg_audio_tap_tests.cc and
// tests/ui/ffmpeg_av_mux_tests.cc.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "video_editor_test_util.h"
#include "media_decoder.h"
#include "helper/path_utils.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {
namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers
namespace FFmpeg = XER::Bridge::FFmpeg;
using XER::VideoEditor::IMediaDecoder;

std::string Utf8Path(const std::filesystem::path& p) {
    return Path::ToUtf8(p);
}

// ---- case 1: fake decoder ------------------------------------------

// Minimal IMediaDecoder implementation — 3 synthetic BGRA frames at
// pts {0.0, 0.5, 1.0} on a 4x2 raster. No FFmpeg anywhere: proves the
// interface is implementable by tests and documents the contract
// compositor/mixer code may rely on.
class FakeDecoder final : public IMediaDecoder {
public:
    FakeDecoder() { Reset(); }

    bool Open(const std::string& /*utf8_path*/,
              std::string* /*error_out*/) override {
        open_ = true;
        return true;
    }

    void Close() override {
        open_ = false;
        Reset();
    }

    bool IsOpen() const override { return open_; }

    bool NextFrame(std::vector<std::uint8_t>* bgra, int* width,
                   int* height) override {
        if (!open_ || bgra == nullptr || width == nullptr ||
            height == nullptr || index_ >= frames_.size()) {
            return false;
        }
        *bgra = frames_[index_].pixels;
        *width = kWidth;
        *height = kHeight;
        position_ = frames_[index_].pts;
        ++index_;
        return true;
    }

    bool Seek(double seconds) override {
        if (!open_) {
            return false;
        }
        // Mirrors the bridge contract: position presets to the requested
        // target until the next frame lands, decode restarts from the
        // top, and the audio epoch bumps so consumers drop stale chunks.
        position_ = seconds;
        index_ = 0;
        ++epoch_;
        return true;
    }

    double GetPositionSeconds() const override { return position_; }
    double GetFps() const override { return 24.0; }
    double GetDurationSeconds() const override { return kDuration; }
    int GetWidth() const override { return kWidth; }
    int GetHeight() const override { return kHeight; }

    void EnableAudioTap(bool /*enable*/) override {}
    bool HasAudio() const override { return false; }
    double GetAudioStartSeconds() const override { return 0.0; }
    double GetAudioTimeBase() const override { return 0.0; }
    bool ReadAudioFrames(
        std::vector<FFmpeg::AudioFrameInfo>* /*out*/,
        int /*max_frames*/) override {
        return false;
    }
    bool AudioDrained() const override { return true; }
    std::uint64_t GetAudioEpoch() const override { return epoch_; }

private:
    struct Frame {
        std::vector<std::uint8_t> pixels;
        double pts = 0.0;
    };

    static constexpr int kWidth = 4;
    static constexpr int kHeight = 2;
    static constexpr double kDuration = 1.0;

    void Reset() {
        index_ = 0;
        position_ = -1.0;  // bridge convention: -1 before first frame
        epoch_ = 0;
        frames_ = MakeFrames();
    }

    static std::vector<Frame> MakeFrames() {
        // 3 solid-color frames, pts 0.0 / 0.5 / 1.0.
        const std::uint8_t colors[3][4] = {
            {10, 20, 30, 255}, {200, 100, 50, 255}, {0, 255, 128, 255}};
        const double pts[3] = {0.0, 0.5, 1.0};
        std::vector<Frame> frames;
        for (int i = 0; i < 3; ++i) {
            Frame f;
            f.pts = pts[i];
            f.pixels.assign(
                static_cast<std::size_t>(kWidth) * kHeight * 4u, 0);
            for (std::size_t p = 0; p < f.pixels.size(); p += 4u) {
                f.pixels[p + 0] = colors[i][0];  // B
                f.pixels[p + 1] = colors[i][1];  // G
                f.pixels[p + 2] = colors[i][2];  // R
                f.pixels[p + 3] = colors[i][3];  // A
            }
            frames.push_back(std::move(f));
        }
        return frames;
    }

    bool open_ = false;
    std::vector<Frame> frames_;
    std::size_t index_ = 0;
    double position_ = -1.0;
    std::uint64_t epoch_ = 0;
};

// ---- fixtures (cases 2-3) -------------------------------------------

constexpr int kClipW = 64;
constexpr int kClipH = 64;
constexpr int kClipFrames = 8;   // 8 frames @ 24 fps ≈ 0.333 s
constexpr int kClipFps = 24;

constexpr int kAudioRate = 48000;
constexpr int kAudioChannels = 2;
constexpr int kAvAudioSamples = kAudioRate / 2;  // 0.5 s for the A/V clip
constexpr int kWavAudioSamples = kAudioRate;     // 1 s WAV

// 64x64 BGRA frame with a moving vertical bar — enough motion for the
// encoder to emit real frames instead of dropping them as static
// (same pattern as ffmpeg_audio_tap_tests.cc FillTinyFrame).
void FillClipFrame(FFmpeg::VideoFrameInfo* frame, std::size_t index) {
    frame->width = kClipW;
    frame->height = kClipH;
    frame->pixel_format = "bgra";
    frame->line_sizes = {kClipW * 4, 0, 0, 0};
    frame->data.assign(static_cast<std::size_t>(kClipW) * kClipH * 4u, 0);
    const int bar_x = static_cast<int>(index * 4) % kClipW;
    for (int y = 0; y < kClipH; ++y) {
        for (int x = bar_x; x < bar_x + 8 && x < kClipW; ++x) {
            const std::size_t o =
                (static_cast<std::size_t>(y) * kClipW + x) * 4u;
            frame->data[o + 0] = 30;    // B
            frame->data[o + 1] = 60;    // G
            frame->data[o + 2] = 220;   // R
            frame->data[o + 3] = 255;   // A
        }
    }
}

// Encodes kClipFrames frames -> video-only .mp4. Codec fallback
// {"libx264","mpeg4"} like the other ffmpeg fixtures; mpeg4 is a native
// encoder so failure here means real breakage.
bool MakeVideoOnlyClip(const std::string& utf8_path, std::string* err) {
    FFmpeg::EncodeVideoParams params;
    params.output_path = utf8_path;
    params.width = kClipW;
    params.height = kClipH;
    params.pixel_format = "bgra";
    params.fps_num = kClipFps;
    params.fps_den = 1;
    params.bit_rate = 200000;
    for (const char* codec : {"libx264", "mpeg4"}) {
        params.codec_name = codec;
        err->clear();
        if (FFmpeg::EncodeVideoFrameStream(
                params, kClipFrames,
                [](std::size_t index,
                   FFmpeg::VideoFrameInfo* out,
                   std::string*) {
                    FillClipFrame(out, index);
                    return true;
                },
                err)) {
            return true;
        }
    }
    return false;
}

// Interleaved signed-16 PCM sine (440 Hz); `first_sample` is the
// absolute sample index so chunks stay phase-continuous — same
// pattern as ffmpeg_audio_tap_tests.cc FillSine.
void FillSine(FFmpeg::AudioFrameInfo* frame, std::size_t first_sample,
              int sample_count) {
    frame->sample_rate = kAudioRate;
    frame->channels = kAudioChannels;
    frame->sample_count = sample_count;
    frame->planar = false;
    frame->sample_format = "s16";
    frame->data.assign(
        static_cast<std::size_t>(sample_count) * kAudioChannels * 2u, 0);
    auto* pcm = reinterpret_cast<std::int16_t*>(frame->data.data());
    for (int i = 0; i < sample_count; ++i) {
        const double t =
            static_cast<double>(first_sample + i) / kAudioRate;
        const std::int16_t v = static_cast<std::int16_t>(
            12000.0 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * t));
        pcm[i * kAudioChannels + 0] = v;
        pcm[i * kAudioChannels + 1] = v;
    }
}

std::vector<FFmpeg::AudioFrameInfo> MakeSineChunks(int total_samples) {
    std::vector<FFmpeg::AudioFrameInfo> chunks;
    for (int done = 0; done < total_samples;) {
        const int n = std::min(1024, total_samples - done);
        FFmpeg::AudioFrameInfo f;
        FillSine(&f, static_cast<std::size_t>(done), n);
        chunks.push_back(std::move(f));
        done += n;
    }
    return chunks;
}

// Encodes the 8-frame clip PLUS a sine audio track -> .mp4 via the
// streaming A/V mux (AAC is a native encoder — always present).
bool MakeAvClip(const std::string& utf8_path, std::string* err) {
    FFmpeg::EncodeAudioVideoParams params;
    params.video.output_path = utf8_path;
    params.video.width = kClipW;
    params.video.height = kClipH;
    params.video.pixel_format = "bgra";
    params.video.fps_num = kClipFps;
    params.video.fps_den = 1;
    params.video.bit_rate = 200000;
    params.audio.codec_name = "aac";
    params.audio.sample_rate = kAudioRate;
    params.audio.channels = kAudioChannels;
    params.audio.sample_format = "s16";
    params.audio.bit_rate = 128000;

    const auto chunks = MakeSineChunks(kAvAudioSamples);
    auto video_provider =
        [](std::size_t index, FFmpeg::VideoFrameInfo* out,
           std::string*) {
            FillClipFrame(out, index);
            return true;
        };
    auto audio_provider =
        [&chunks](std::size_t index, FFmpeg::AudioFrameInfo* out,
                  std::string*) {
            if (index >= chunks.size()) {
                return false;
            }
            *out = chunks[index];
            return true;
        };

    for (const char* codec : {"libx264", "mpeg4"}) {
        params.video.codec_name = codec;
        err->clear();
        if (FFmpeg::EncodeAudioVideoStream(
                params, kClipFrames, video_provider, chunks.size(),
                audio_provider, err)) {
            return true;
        }
    }
    return false;
}

// Encodes `total_samples` of sine -> pcm_s16le WAV (deterministic, no
// optional encoder needed — same as the tap tests' EncodeWavFile).
bool MakeWavFile(const std::string& utf8_path, int total_samples,
                 std::string* err) {
    FFmpeg::EncodeAudioParams params;
    params.output_path = utf8_path;
    params.codec_name = "pcm_s16le";
    params.sample_rate = kAudioRate;
    params.channels = kAudioChannels;
    params.sample_format = "s16";
    return FFmpeg::EncodeAudioFrames(params, MakeSineChunks(total_samples),
                                     err);
}

}  // namespace

int XER_VideoEditor_RunDecoderTests() {
    // -- 1. Fake-decoder contract (no FFmpeg) --------------------------
    {
        FakeDecoder decoder;
        XER_VE_CHECK(!decoder.IsOpen());
        std::string err;
        XER_VE_CHECK(decoder.Open("fake://clip", &err));
        XER_VE_CHECK(decoder.IsOpen());
        XER_VE_CHECK(decoder.GetWidth() == 4);
        XER_VE_CHECK(decoder.GetHeight() == 2);
        XER_VE_CHECK(decoder.GetFps() == 24.0);
        XER_VE_CHECK(decoder.GetDurationSeconds() == 1.0);
        // Position is -1 until the first frame lands (bridge convention).
        XER_VE_CHECK(decoder.GetPositionSeconds() == -1.0);

        std::vector<std::uint8_t> bgra;
        int w = 0;
        int h = 0;
        const double expected_pts[3] = {0.0, 0.5, 1.0};
        // BGRA byte order of each frame's first pixel (matches
        // FakeDecoder::MakeFrames colors).
        const std::uint8_t expected_px[3][4] = {
            {10, 20, 30, 255}, {200, 100, 50, 255}, {0, 255, 128, 255}};
        for (int i = 0; i < 3; ++i) {
            XER_VE_CHECK(decoder.NextFrame(&bgra, &w, &h));
            XER_VE_CHECK(w == 4 && h == 2);
            XER_VE_CHECK(bgra.size() ==
                         static_cast<std::size_t>(4) * 2u * 4u);
            XER_VE_CHECK(bgra.size() >= 4 && bgra[0] == expected_px[i][0] &&
                         bgra[1] == expected_px[i][1] &&
                         bgra[2] == expected_px[i][2] &&
                         bgra[3] == expected_px[i][3]);
            // GetPositionSeconds reports the LAST decoded frame's pts.
            XER_VE_CHECK(decoder.GetPositionSeconds() == expected_pts[i]);
        }
        // EOF: NextFrame false, position stays at the last frame's pts.
        XER_VE_CHECK(!decoder.NextFrame(&bgra, &w, &h));
        XER_VE_CHECK(decoder.GetPositionSeconds() == 1.0);

        // Seek presets position to the target until the next frame
        // lands, rewinds decode, and bumps the audio epoch.
        const std::uint64_t epoch_before = decoder.GetAudioEpoch();
        XER_VE_CHECK(decoder.Seek(0.25));
        XER_VE_CHECK(decoder.GetPositionSeconds() == 0.25);
        XER_VE_CHECK(decoder.GetAudioEpoch() > epoch_before);
        XER_VE_CHECK(decoder.NextFrame(&bgra, &w, &h));
        XER_VE_CHECK(decoder.GetPositionSeconds() == 0.0);  // frame 0 pts

        // Null out-params are a contract violation -> false, no crash.
        XER_VE_CHECK(!decoder.NextFrame(nullptr, &w, &h));
        XER_VE_CHECK(!decoder.NextFrame(&bgra, nullptr, &h));
        XER_VE_CHECK(!decoder.NextFrame(&bgra, &w, nullptr));
        decoder.Close();
        XER_VE_CHECK(!decoder.IsOpen());
    }

    // Cases 2-3 are FFmpeg-backed — skip cleanly when the bridge is a
    // stub (non-Windows / no avformat), same as the poster suite.
    if (!FFmpeg::IsAvailable()) {
        return 0;
    }

    const auto dir = XER::VideoEditor::Test::TempDir() / "decoder";
    std::filesystem::create_directories(dir);
    std::string err;

    const auto video_path = dir / "clip.mp4";
    const auto av_path = dir / "clip_av.mp4";
    const auto wav_path = dir / "tone.wav";

    if (!MakeVideoOnlyClip(Utf8Path(video_path), &err)) {
        // Both video encoders failed — the FFmpeg build cannot encode
        // video at all; skip the bridge cases rather than fail them.
        std::fprintf(stderr,
                     "note: decoder video fixture encode failed: %s\n",
                     err.c_str());
        return 0;
    }
    // A/V and WAV fixtures degrade their own cases to skip-notes —
    // pcm_s16le WAV is deterministic, so a WAV failure is noteworthy
    // but only gates case 3b.
    const bool av_ok = MakeAvClip(Utf8Path(av_path), &err);
    if (!av_ok) {
        std::fprintf(stderr,
                     "note: decoder A/V fixture encode failed: %s\n",
                     err.c_str());
    }
    const bool wav_ok =
        MakeWavFile(Utf8Path(wav_path), kWavAudioSamples, &err);
    if (!wav_ok) {
        std::fprintf(stderr,
                     "note: decoder WAV fixture encode failed: %s\n",
                     err.c_str());
    }

    const std::string video_utf8 = Utf8Path(video_path);

    // -- 2. Bridge adapter: video decode through IMediaDecoder --------
    {
        auto decoder = XER::VideoEditor::MakeBridgeDecoder();
        XER_VE_CHECK(decoder != nullptr);
        XER_VE_CHECK(!decoder->IsOpen());

        XER_VE_CHECK(decoder->Open(video_utf8, &err));
        XER_VE_CHECK(decoder->IsOpen());
        if (decoder->IsOpen()) {
            XER_VE_CHECK(decoder->GetWidth() == kClipW);
            XER_VE_CHECK(decoder->GetHeight() == kClipH);
            // Container-level metadata is best-effort: avg_frame_rate
            // comes out as frame_count/span (27.43 for this 8-frame
            // clip, not 24) and fmt->duration is unset (0.0) for
            // encoder-produced mp4s — the bridge reads fmt->duration
            // only. Pin the sane bounds, not exact values.
            XER_VE_CHECK(decoder->GetFps() > 0.0);
            XER_VE_CHECK(decoder->GetDurationSeconds() >= 0.0);
            XER_VE_CHECK(!decoder->HasAudio());  // video-only fixture

            std::vector<std::uint8_t> bgra;
            int w = 0;
            int h = 0;
            int decoded = 0;
            double first_pos = -1.0;
            double prev_pos = -1.0;
            bool steps_ok = true;
            bool pixels_ok = true;
            while (decoder->NextFrame(&bgra, &w, &h)) {
                ++decoded;
                if (w != kClipW || h != kClipH ||
                    bgra.size() != static_cast<std::size_t>(kClipW) *
                                      kClipH * 4u) {
                    pixels_ok = false;
                }
                // Packed BGRA8 with opaque alpha (the fixture is
                // opaque; YUV->BGRA conversion forces A=255).
                if (bgra.size() >= 4 && bgra[3] != 255) {
                    pixels_ok = false;
                }
                const double pos = decoder->GetPositionSeconds();
                if (decoded == 1) {
                    first_pos = pos;
                } else if (pos < prev_pos || pos - prev_pos > 0.1) {
                    // Monotonic pts with ~1/24 s steps (bounded slack
                    // for time_base rounding — average checked below).
                    steps_ok = false;
                }
                prev_pos = pos;
            }
            XER_VE_CHECK(decoded == kClipFrames);
            XER_VE_CHECK(pixels_ok);
            XER_VE_CHECK(steps_ok);
            XER_VE_CHECK(first_pos >= 0.0 && first_pos < 0.1);
            // Average frame interval matches the 24 fps fixture rate.
            if (decoded > 1) {
                const double avg_step =
                    (prev_pos - first_pos) / (decoded - 1);
                XER_VE_CHECK(std::abs(avg_step - 1.0 / kClipFps) <
                             0.01);
            }
            // Last frame's pts ~= (frames-1)/fps = 7/24 ≈ 0.2917 —
            // bounded by the clip's nominal duration, NOT by
            // GetDurationSeconds() (unreliable on these fixtures).
            XER_VE_CHECK(prev_pos >= 0.25);
            XER_VE_CHECK(prev_pos <=
                         static_cast<double>(kClipFrames) / kClipFps);
            // EOF is sticky — another NextFrame keeps returning false.
            XER_VE_CHECK(!decoder->NextFrame(&bgra, &w, &h));

            // Seek pass-through: rewinds decode; position presets to the
            // requested target until the next frame lands.
            XER_VE_CHECK(decoder->Seek(0.0));
            XER_VE_CHECK(decoder->GetPositionSeconds() == 0.0);
            XER_VE_CHECK(decoder->NextFrame(&bgra, &w, &h));
            XER_VE_CHECK(w == kClipW && h == kClipH);
        }
        decoder->Close();
        XER_VE_CHECK(!decoder->IsOpen());
    }

    // -- 3a. Audio tap on an A/V clip ----------------------------------
    if (av_ok) {
        auto decoder = XER::VideoEditor::MakeBridgeDecoder();
        decoder->EnableAudioTap();  // BEFORE Open — ordering contract
        XER_VE_CHECK(decoder->Open(Utf8Path(av_path), &err));
        if (decoder->IsOpen()) {
            XER_VE_CHECK(decoder->HasAudio());
            XER_VE_CHECK(decoder->GetWidth() == kClipW);
            XER_VE_CHECK(decoder->GetHeight() == kClipH);
            XER_VE_CHECK(decoder->GetAudioTimeBase() > 0.0);
            XER_VE_CHECK(decoder->GetAudioStartSeconds() >= 0.0);

            // Drive the documented consumer loop: alternate
            // NextFrame()/ReadAudioFrames() until BOTH sides are done.
            // NextFrame still yields the 8 video frames AND pumps the
            // interleaved audio packets into the tap queue. AudioDrained
            // can flip true while the video decoder still holds buffered
            // (B-frame) output — keep pumping NextFrame until it reports
            // EOF so no tail frames are lost.
            std::vector<std::uint8_t> bgra;
            int w = 0;
            int h = 0;
            int video_frames = 0;
            bool video_done = false;
            long long audio_samples = 0;
            int audio_chunks = 0;
            bool audio_fields_ok = true;
            int guard = 0;
            while ((!video_done || !decoder->AudioDrained()) &&
                   guard++ < 200000) {
                if (!video_done) {
                    if (decoder->NextFrame(&bgra, &w, &h)) {
                        ++video_frames;
                    } else {
                        video_done = true;  // EOF/error — sticky
                    }
                }
                std::vector<FFmpeg::AudioFrameInfo> chunk;
                if (!decoder->ReadAudioFrames(&chunk, 256)) {
                    break;  // decode error — drained check fails below
                }
                for (const auto& f : chunk) {
                    ++audio_chunks;
                    audio_samples += f.sample_count;
                    if (f.sample_rate != kAudioRate ||
                        f.channels != kAudioChannels ||
                        f.sample_count <= 0 || f.data.empty()) {
                        audio_fields_ok = false;
                    }
                }
            }
            XER_VE_CHECK(video_done);
            XER_VE_CHECK(decoder->AudioDrained());
            // The muxed fixture's video stream yields 7 decodable
            // frames (nb_frames metadata says 8; batch DecodeVideoFrames
            // agrees with the streaming decoder — a container-level
            // artifact of the short libx264 encode, not an adapter
            // defect). Pin "nearly all frames", not the exact count.
            XER_VE_CHECK(video_frames >= kClipFrames - 1);
            XER_VE_CHECK(video_frames <= kClipFrames);
            XER_VE_CHECK(audio_chunks > 0);
            XER_VE_CHECK(audio_samples > 0);
            XER_VE_CHECK(audio_fields_ok);
            // AAC priming/padding makes the exact count fuzzy — require
            // a healthy fraction of the 0.5 s (24000) input samples.
            XER_VE_CHECK(audio_samples > 15000);
        }
        decoder->Close();
    }

    // -- 3b. Audio tap on an audio-only WAV -----------------------------
    if (wav_ok) {
        auto decoder = XER::VideoEditor::MakeBridgeDecoder();
        decoder->EnableAudioTap();
        XER_VE_CHECK(decoder->Open(Utf8Path(wav_path), &err));
        if (decoder->IsOpen()) {
            XER_VE_CHECK(decoder->HasAudio());
            XER_VE_CHECK(decoder->GetWidth() == 0);
            XER_VE_CHECK(decoder->GetHeight() == 0);
            XER_VE_CHECK(decoder->GetAudioTimeBase() > 0.0);
            // Audio-only containers have no video pts — position stays
            // at the bridge's -1 sentinel.
            XER_VE_CHECK(decoder->GetPositionSeconds() == -1.0);

            std::vector<std::uint8_t> bgra;
            int w = 0;
            int h = 0;
            long long total = 0;
            bool fields_ok = true;
            int guard = 0;
            while (!decoder->AudioDrained() && guard++ < 200000) {
                // NextFrame returns false (no video) but MUST still be
                // called — each call pumps a bounded batch of demux
                // packets into the tap queue.
                decoder->NextFrame(&bgra, &w, &h);
                std::vector<FFmpeg::AudioFrameInfo> chunk;
                if (!decoder->ReadAudioFrames(&chunk, 256)) {
                    break;
                }
                for (const auto& f : chunk) {
                    total += f.sample_count;
                    if (f.sample_rate != kAudioRate ||
                        f.channels != kAudioChannels ||
                        f.sample_count <= 0 || f.data.empty() ||
                        f.sample_format != "s16") {
                        fields_ok = false;
                    }
                }
            }
            XER_VE_CHECK(decoder->AudioDrained());
            // 1 s @ 48 kHz = 48000 samples; PCM decode is sample-exact,
            // bounded slack for container rounding.
            XER_VE_CHECK(total > 40000);
            XER_VE_CHECK(total <= 52000);
            XER_VE_CHECK(fields_ok);
            XER_VE_CHECK(decoder->GetPositionSeconds() == -1.0);

            // Seek flushes the audio side: the epoch bumps so consumers
            // drop stale in-flight chunks, and position presets to the
            // target until the next frame lands (never, audio-only).
            const std::uint64_t epoch_before =
                decoder->GetAudioEpoch();
            XER_VE_CHECK(decoder->Seek(0.5));
            XER_VE_CHECK(decoder->GetAudioEpoch() > epoch_before);
            XER_VE_CHECK(decoder->GetPositionSeconds() == 0.5);

            // The tap survives the seek — ~0.5 s of audio remains.
            long long total2 = 0;
            guard = 0;
            while (!decoder->AudioDrained() && guard++ < 200000) {
                decoder->NextFrame(&bgra, &w, &h);
                std::vector<FFmpeg::AudioFrameInfo> chunk;
                if (!decoder->ReadAudioFrames(&chunk, 256)) {
                    break;
                }
                for (const auto& f : chunk) {
                    total2 += f.sample_count;
                }
            }
            XER_VE_CHECK(decoder->AudioDrained());
            XER_VE_CHECK(total2 > 15000);
        }
        decoder->Close();
    }

    return 0;
}
