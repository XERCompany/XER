// Mixer suite — Phase-1C timeline -> PCM audio mixer. Every fake-decoder
// case runs WITHOUT FFmpeg: a hand-rolled IMediaDecoder bank serves
// scripted AudioFrameInfo blocks (s16/u8/s32/flt, interleaved/planar,
// mono/stereo, 44.1/48 kHz, pts gaps, missing pts) through the real
// AudioMixer pipeline — gain/fade/transition-weighted mix, media-time
// windowing, chunking, resampling-by-interpolation, lazy open/sequential
// drain, cancel. One FFmpeg-gated integration case self-skips via
// XER::Bridge::FFmpeg::IsAvailable(). Expected values are computed from
// the documented math, not observed from the implementation.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "video_editor_test_util.h"
#include "audio_mixer.h"
#include "helper/path_utils.h"
#include "media_decoder.h"
#include "timeline_model.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {
namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers
namespace FFmpeg = XER::Bridge::FFmpeg;
using XER::VideoEditor::AudioMixer;
using XER::VideoEditor::AudioMixerOptions;
using XER::VideoEditor::Clip;
using XER::VideoEditor::IMediaDecoder;
using XER::VideoEditor::MediaDecoderFactory;
using XER::VideoEditor::MediaDecoderPtr;
using XER::VideoEditor::Project;
using XER::VideoEditor::Track;
using XER::VideoEditor::TrackKind;
using XER::VideoEditor::Transition;

constexpr std::int64_t kNoPts =
    (std::numeric_limits<std::int64_t>::min)();  // AV_NOPTS_VALUE
constexpr double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------
// Fake decoder bank — per-path scripted audio specs; each decoder the
// factory produces registers a state block so tests can count pump/read
// calls per Open (two clips sharing a path -> two independent states).
// ------------------------------------------------------------------

struct FakeDecoderState {
    int next_frame_calls = 0;   // NextFrame pumps (audio-only: returns false)
    int read_calls = 0;         // ReadAudioFrames calls
};

struct FakeSpecBlock {
    double media_start = 0.0;            // media seconds (pts + seek basis)
    FFmpeg::AudioFrameInfo frame;
};

struct FakeSpec {
    int sample_rate = 48000;
    int channels = 2;
    std::string sample_format = "s16";
    bool planar = false;
    double time_base = 1.0 / 48000.0;   // seconds per pts tick
    double audio_start = 0.0;           // GetAudioStartSeconds()
    double duration = 0.0;              // GetDurationSeconds (0 = unknown)
    bool has_audio = true;
    bool has_pts = true;                // false -> pts/bet = NOPTS
    int pump_batch = 3;                 // blocks released per NextFrame
    std::vector<FakeSpecBlock> blocks;  // sorted by media_start
};

struct FakeBank {
    std::map<std::string, FakeSpec> specs;
    std::map<std::string, int> open_counts;
    std::vector<std::shared_ptr<FakeDecoderState>> states;
};

class FakeDecoder final : public IMediaDecoder {
public:
    explicit FakeDecoder(std::shared_ptr<FakeBank> bank)
        : bank_(std::move(bank)) {
        state_ = std::make_shared<FakeDecoderState>();
        bank_->states.push_back(state_);
    }

    bool Open(const std::string& utf8_path,
              std::string* error_out) override {
        auto it = bank_->specs.find(utf8_path);
        if (it == bank_->specs.end()) {
            if (error_out != nullptr) {
                *error_out = "fake: no spec for " + utf8_path;
            }
            return false;
        }
        spec_ = &it->second;   // std::map nodes are stable
        ++bank_->open_counts[utf8_path];
        open_ = true;
        pending_ = 0;
        queue_.clear();
        position_ = -1.0;
        ++epoch_;
        return true;
    }

    void Close() override {
        open_ = false;
        pending_ = 0;
        queue_.clear();
        position_ = -1.0;
    }

    bool IsOpen() const override { return open_; }

    // Audio-only container: never yields video frames, but MUST still be
    // called — each call pumps a bounded batch of scripted blocks into
    // the readable queue (mirrors the bridge's 256-packet pump).
    bool NextFrame(std::vector<std::uint8_t>* bgra, int* width,
                   int* height) override {
        if (!open_ || bgra == nullptr || width == nullptr ||
            height == nullptr || spec_ == nullptr) {
            return false;
        }
        ++state_->next_frame_calls;
        const int release =
            std::min(spec_->pump_batch,
                     static_cast<int>(spec_->blocks.size()) -
                         static_cast<int>(pending_));
        for (int i = 0; i < release; ++i) {
            queue_.push_back(spec_->blocks[pending_++].frame);
        }
        return false;
    }

    bool Seek(double seconds) override {
        if (!open_ || spec_ == nullptr) {
            return false;
        }
        // Keyframe/packet-backward only: drop pending blocks that end at
        // or before the target, clear the released queue, bump the epoch
        // so consumers drop stale in-flight chunks.
        while (pending_ < spec_->blocks.size() &&
               spec_->blocks[pending_].media_start +
                       static_cast<double>(
                           spec_->blocks[pending_].frame.sample_count) /
                           spec_->sample_rate <=
                   seconds) {
            ++pending_;
        }
        queue_.clear();
        ++epoch_;
        return true;
    }

    double GetPositionSeconds() const override { return position_; }
    double GetFps() const override { return 30.0; }
    double GetDurationSeconds() const override {
        return spec_ != nullptr ? spec_->duration : 0.0;
    }
    int GetWidth() const override { return 0; }   // audio-only
    int GetHeight() const override { return 0; }

    void EnableAudioTap(bool /*enable*/) override {}
    bool HasAudio() const override {
        return spec_ != nullptr && spec_->has_audio;
    }
    double GetAudioStartSeconds() const override {
        return spec_ != nullptr ? spec_->audio_start : 0.0;
    }
    double GetAudioTimeBase() const override {
        return spec_ != nullptr ? spec_->time_base : 0.0;
    }
    bool ReadAudioFrames(
        std::vector<FFmpeg::AudioFrameInfo>* out,
        int max_frames) override {
        if (!open_ || out == nullptr || spec_ == nullptr ||
            !spec_->has_audio) {
            return false;
        }
        ++state_->read_calls;
        const int limit = max_frames > 0 ? max_frames : 0x7fffffff;
        const int n = std::min(limit, static_cast<int>(queue_.size()));
        out->insert(out->end(), queue_.begin(), queue_.begin() + n);
        queue_.erase(queue_.begin(), queue_.begin() + n);
        return true;
    }
    bool AudioDrained() const override {
        return spec_ != nullptr && pending_ >= spec_->blocks.size() &&
               queue_.empty();
    }
    std::uint64_t GetAudioEpoch() const override { return epoch_; }

    std::shared_ptr<FakeDecoderState> state() const { return state_; }

private:
    std::shared_ptr<FakeBank> bank_;
    std::shared_ptr<FakeDecoderState> state_;
    const FakeSpec* spec_ = nullptr;
    bool open_ = false;
    std::size_t pending_ = 0;
    std::deque<FFmpeg::AudioFrameInfo> queue_;
    double position_ = -1.0;
    std::uint64_t epoch_ = 0;
};

MediaDecoderFactory MakeFakeFactory(std::shared_ptr<FakeBank> bank) {
    return [bank]() -> MediaDecoderPtr {
        return std::make_unique<FakeDecoder>(bank);
    };
}

// ------------------------------------------------------------------
// Scripted-block builders — media_start is the media-time anchor; pts
// ticks derive from spec.time_base (or NOPTS when !has_pts).
// ------------------------------------------------------------------

void FinalizeBlock(const FakeSpec& spec, double media_start,
                   FFmpeg::AudioFrameInfo* frame) {
    if (spec.has_pts) {
        frame->pts = static_cast<std::int64_t>(
            std::llround(media_start / spec.time_base));
        frame->best_effort_timestamp = kNoPts;
    } else {
        frame->pts = kNoPts;
        frame->best_effort_timestamp = kNoPts;
    }
}

void AddBlock(FakeSpec* spec, double media_start,
              FFmpeg::AudioFrameInfo frame) {
    FinalizeBlock(*spec, media_start, &frame);
    spec->blocks.push_back({media_start, std::move(frame)});
}

// Interleaved s16, `channels` channels, sine at `freq` Hz / `amp` peak.
// `first_sample` is the absolute sample index (phase continuity).
void FillS16Sine(FFmpeg::AudioFrameInfo* frame, int rate, int channels,
                 int frames, double freq_hz, double amp,
                 std::int64_t first_sample) {
    frame->sample_rate = rate;
    frame->channels = channels;
    frame->planar = false;
    frame->sample_format = "s16";
    frame->sample_count = frames;
    frame->data.assign(
        static_cast<std::size_t>(frames) * channels * 2u, 0);
    auto* pcm = reinterpret_cast<std::int16_t*>(frame->data.data());
    for (int i = 0; i < frames; ++i) {
        const double t =
            static_cast<double>(first_sample + i) / rate;
        const std::int16_t v = static_cast<std::int16_t>(
            std::clamp(amp * std::sin(2.0 * kPi * freq_hz * t), -1.0, 1.0) *
            32767.0);
        for (int c = 0; c < channels; ++c) {
            pcm[static_cast<std::size_t>(i) * channels + c] = v;
        }
    }
}

// Interleaved s16 DC level (`dc` in [-1,1]).
void FillS16DC(FFmpeg::AudioFrameInfo* frame, int rate, int channels,
               int frames, double dc) {
    frame->sample_rate = rate;
    frame->channels = channels;
    frame->planar = false;
    frame->sample_format = "s16";
    frame->sample_count = frames;
    frame->data.assign(
        static_cast<std::size_t>(frames) * channels * 2u, 0);
    const std::int16_t v = static_cast<std::int16_t>(
        std::clamp(dc, -1.0, 1.0) * 32767.0);
    auto* pcm = reinterpret_cast<std::int16_t*>(frame->data.data());
    for (int i = 0; i < frames * channels; ++i) {
        pcm[i] = v;
    }
}

// Planar s16 ("s16p"): one plane per channel, plane_sizes in bytes.
void FillS16PlanarDC(FFmpeg::AudioFrameInfo* frame, int rate,
                     int channels, int frames, double dc) {
    frame->sample_rate = rate;
    frame->channels = channels;
    frame->planar = true;
    frame->sample_format = "s16p";
    frame->sample_count = frames;
    const std::int16_t v = static_cast<std::int16_t>(
        std::clamp(dc, -1.0, 1.0) * 32767.0);
    const std::size_t plane_bytes =
        static_cast<std::size_t>(frames) * 2u;
    frame->plane_sizes.assign(channels, static_cast<int>(plane_bytes));
    frame->data.assign(plane_bytes * channels, 0);
    for (int c = 0; c < channels; ++c) {
        auto* pcm = reinterpret_cast<std::int16_t*>(
            frame->data.data() + c * plane_bytes);
        for (int i = 0; i < frames; ++i) {
            pcm[i] = v;
        }
    }
}

// Fills `spec` with back-to-back blocks of `block_frames` samples
// covering [0, seconds) at the spec's rate, each produced by `fill`.
template <typename FillFn>
void FillSpecRange(FakeSpec* spec, double seconds, int block_frames,
                   FillFn&& fill) {
    const double block_sec =
        static_cast<double>(block_frames) / spec->sample_rate;
    std::int64_t sample_index = 0;
    for (double start = 0.0; start < seconds - 1e-9;
         start += block_sec) {
        const int frames = static_cast<int>(std::min(
            block_sec, seconds - start) * spec->sample_rate + 0.5);
        FFmpeg::AudioFrameInfo frame;
        fill(&frame, frames, sample_index);
        sample_index += frames;
        AddBlock(spec, start, std::move(frame));
    }
}

// Convenience: full-range sine spec.
FakeSpec MakeSineSpec(double seconds, double freq = 440.0,
                      double amp = 0.8, int rate = 48000,
                      int channels = 2) {
    FakeSpec spec;
    spec.sample_rate = rate;
    spec.channels = channels;
    spec.time_base = 1.0 / rate;
    spec.duration = seconds;
    FillSpecRange(&spec, seconds, 2048,
                  [&](FFmpeg::AudioFrameInfo* f, int frames,
                      std::int64_t first) {
                      FillS16Sine(f, rate, channels, frames, freq, amp,
                                  first);
                  });
    return spec;
}

// Convenience: full-range DC spec (s16 interleaved).
FakeSpec MakeDCSpec(double seconds, double dc, int rate = 48000,
                    int channels = 2) {
    FakeSpec spec;
    spec.sample_rate = rate;
    spec.channels = channels;
    spec.time_base = 1.0 / rate;
    spec.duration = seconds;
    FillSpecRange(&spec, seconds, 2048,
                  [&](FFmpeg::AudioFrameInfo* f, int frames,
                      std::int64_t /*first*/) {
                      FillS16DC(f, spec.sample_rate, spec.channels, frames,
                                dc);
                  });
    return spec;
}

// ------------------------------------------------------------------
// Project / clip helpers — hand-built Projects (the mixer takes the
// model as-is; tests deliberately bypass AddClip validation).
// ------------------------------------------------------------------

Project MakeProject() {
    Project p;
    p.width = 16;
    p.height = 16;
    p.fps_num = 30;
    p.fps_den = 1;
    p.bg_color = {0, 0, 0, 255};
    p.tracks.push_back(Track{TrackKind::Video, "V1"});
    p.tracks.push_back(Track{TrackKind::Audio, "A1"});
    return p;
}

Clip MakeClip(const std::string& id, int track, double start,
              double duration, const std::string& source,
              double in_point = 0.0) {
    Clip c;
    c.id = id;
    c.source_path = source;
    c.track = track;
    c.timeline_start = start;
    c.in_point = in_point;
    c.duration = duration;
    return c;
}

// ------------------------------------------------------------------
// PCM asserts — output chunks are interleaved s16 L,R.
// ------------------------------------------------------------------

double ChunkPeak(const FFmpeg::AudioFrameInfo& chunk) {
    double peak = 0.0;
    const auto* pcm =
        reinterpret_cast<const std::int16_t*>(chunk.data.data());
    const std::size_t count = chunk.data.size() / 2;
    for (std::size_t i = 0; i < count; ++i) {
        peak = std::max(peak, std::abs(static_cast<double>(pcm[i])));
    }
    return peak;
}

// Mean |sample| over FRAME indices [begin, end) — each frame has two
// interleaved int16 channels, both counted.
double ChunkMeanAbs(const FFmpeg::AudioFrameInfo& chunk,
                    std::size_t begin, std::size_t end) {
    const auto* pcm =
        reinterpret_cast<const std::int16_t*>(chunk.data.data());
    const std::size_t frames = chunk.data.size() / 4;
    end = std::min(end, frames);
    begin = std::min(begin, end);
    if (end <= begin) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = begin * 2; i < end * 2; ++i) {
        sum += std::abs(static_cast<double>(pcm[i]));
    }
    return sum / static_cast<double>((end - begin) * 2);
}

// Signed mean over BOTH channels of a chunk (for DC-ish checks).
double ChunkMean(const FFmpeg::AudioFrameInfo& chunk) {
    const auto* pcm =
        reinterpret_cast<const std::int16_t*>(chunk.data.data());
    const std::size_t frames = chunk.data.size() / 4;
    if (frames == 0) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        sum += static_cast<double>(pcm[i * 2]) +
               static_cast<double>(pcm[i * 2 + 1]);
    }
    return sum / (2.0 * static_cast<double>(frames));
}

// Signed mean over ONE channel (ch 0 = L, 1 = R) — pan checks.
double ChunkMeanChannel(const FFmpeg::AudioFrameInfo& chunk, int ch) {
    const auto* pcm =
        reinterpret_cast<const std::int16_t*>(chunk.data.data());
    const std::size_t frames = chunk.data.size() / 4;
    if (frames == 0) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        sum += static_cast<double>(pcm[i * 2 + ch]);
    }
    return sum / static_cast<double>(frames);
}

// Peak |sample| over ONE channel — meter/pan assertions.
double ChunkPeakChannel(const FFmpeg::AudioFrameInfo& chunk, int ch) {
    const auto* pcm =
        reinterpret_cast<const std::int16_t*>(chunk.data.data());
    const std::size_t frames = chunk.data.size() / 4;
    double peak = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        peak = std::max(
            peak, std::abs(static_cast<double>(pcm[i * 2 + ch])));
    }
    return peak;
}

bool ChunkAllZero(const FFmpeg::AudioFrameInfo& chunk) {
    for (std::uint8_t b : chunk.data) {
        if (b != 0) {
            return false;
        }
    }
    return true;
}

AudioMixerOptions FakeOptions(
    const std::shared_ptr<FakeBank>& bank) {
    AudioMixerOptions options;
    options.decoder_factory = MakeFakeFactory(bank);
    return options;
}

bool InitMixer(AudioMixer* mixer, const Project& project,
               const std::shared_ptr<FakeBank>& bank) {
    std::string err;
    return mixer->Init(project, FakeOptions(bank), &err);
}

// ------------------------------------------------------------------
// Case 1 — gain / mute / fades (fake decoder, no FFmpeg)
// ------------------------------------------------------------------

void TestGainMuteFades() {
    auto bank = std::make_shared<FakeBank>();
    bank->specs["sine.wav"] = MakeSineSpec(1.0);

    // Baseline: single video-track clip 0-1 s, full volume.
    {
        Project p = MakeProject();
        p.clips.push_back(
            MakeClip("c1", 0, 0.0, 1.0, "sine.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        XER_VE_CHECK(mixer.DurationSeconds() == 1.0);
        XER_VE_CHECK(mixer.ChunkCount() == 47);  // ceil(48000/1024)

        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        // Mid-clip chunk (t ~ 0.5 s): sine at ~0.8 peak -> ~26k.
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(chunk.sample_count == 1024);
        XER_VE_CHECK(chunk.sample_rate == 48000);
        XER_VE_CHECK(chunk.channels == 2);
        XER_VE_CHECK(chunk.sample_format == "s16");
        XER_VE_CHECK(!chunk.planar);
        const double peak = ChunkPeak(chunk);
        XER_VE_CHECK(peak > 20000.0);
        XER_VE_CHECK(peak < 30000.0);
        // Sine has real energy across the chunk (not a click).
        XER_VE_CHECK(ChunkMeanAbs(chunk, 0, chunk.sample_count) >
                     peak * 0.25);
    }

    // gain_db = -6 -> linear ~0.501 -> peak roughly halves (+-1 dB).
    {
        Project p = MakeProject();
        Clip c = MakeClip("c1", 0, 0.0, 1.0, "sine.wav");
        c.gain_db = -6.0f;
        p.clips.push_back(c);
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        const double peak = ChunkPeak(chunk);
        XER_VE_CHECK(peak > 9000.0);
        XER_VE_CHECK(peak < 15000.0);   // ~0.5 * 26214 = 13107
    }

    // clip.muted -> silence.
    {
        Project p = MakeProject();
        Clip c = MakeClip("c1", 0, 0.0, 1.0, "sine.wav");
        c.muted = true;
        p.clips.push_back(c);
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(ChunkAllZero(chunk));
    }

    // Audio-kind track muted -> silence.
    {
        Project p = MakeProject();
        p.tracks[1].muted = true;   // A1 (audio kind)
        p.clips.push_back(MakeClip("c1", 1, 0.0, 1.0, "sine.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(ChunkAllZero(chunk));
    }

    // Video-kind track muted -> IGNORED (muted is audio-kind only):
    // audio still plays.
    {
        Project p = MakeProject();
        p.tracks[0].muted = true;   // V1 (video kind) — meaningless
        p.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "sine.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(ChunkPeak(chunk) > 20000.0);
    }

    // Video-kind track hidden -> video AND its embedded audio gone.
    {
        Project p = MakeProject();
        p.tracks[0].hidden = true;
        p.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "sine.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(ChunkAllZero(chunk));
    }

    // Audio-kind track hidden -> ignored (hidden is video-only).
    {
        Project p = MakeProject();
        p.tracks[1].hidden = true;
        p.clips.push_back(MakeClip("c1", 1, 0.0, 1.0, "sine.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(ChunkPeak(chunk) > 20000.0);
    }

    // fade_in = 0.25 s: chunk 0 ([0, 21.3 ms)) ramps 0 -> ~0.085;
    // last quarter must be clearly louder than the first.
    {
        Project p = MakeProject();
        Clip c = MakeClip("c1", 0, 0.0, 1.0, "sine.wav");
        c.fade_in = 0.25;
        p.clips.push_back(c);
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(0, &chunk, &err));
        const double q = chunk.sample_count / 4.0;
        const double first =
            ChunkMeanAbs(chunk, 0, static_cast<std::size_t>(q));
        const double last = ChunkMeanAbs(
            chunk, chunk.sample_count - static_cast<std::size_t>(q),
            chunk.sample_count);
        XER_VE_CHECK(last > first * 2.0);
        XER_VE_CHECK(first > 0.0);   // not hard-zeroed (gain > 0 inside)
    }

    // fade_out = 0.25 s: symmetric at the clip end — last chunk's first
    // quarter louder than its last.
    {
        Project p = MakeProject();
        Clip c = MakeClip("c1", 0, 0.0, 1.0, "sine.wav");
        c.fade_out = 0.25;
        p.clips.push_back(c);
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(mixer.ChunkCount() - 1, &chunk, &err));
        const std::size_t n = chunk.sample_count;
        const std::size_t q = n / 4;
        const double first = ChunkMeanAbs(chunk, 0, q);
        const double last = ChunkMeanAbs(chunk, n - q, n);
        XER_VE_CHECK(first > last * 2.0);
    }
}

// ------------------------------------------------------------------
// Case 2 — overlapping clips sum; hard clamp at full scale
// ------------------------------------------------------------------

void TestOverlapSumClamp() {
    auto bank = std::make_shared<FakeBank>();
    bank->specs["dc_v.wav"] = MakeDCSpec(1.0, 0.4);
    bank->specs["dc_a.wav"] = MakeDCSpec(1.0, 0.4);
    bank->specs["dc_hot.wav"] = MakeDCSpec(1.0, 0.6);

    // Video clip + audio-track clip overlapping -> both contribute:
    // 0.4 + 0.4 = 0.8 -> ~26214.
    {
        Project p = MakeProject();
        p.clips.push_back(MakeClip("v", 0, 0.0, 1.0, "dc_v.wav"));
        p.clips.push_back(MakeClip("a", 1, 0.0, 1.0, "dc_a.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        const double mean = ChunkMean(chunk);
        XER_VE_CHECK(std::abs(mean - 0.8 * 32767.0) < 600.0);
    }

    // 0.6 + 0.6 = 1.2 -> clamps at +-full scale (32767), no wrap.
    {
        Project p = MakeProject();
        p.clips.push_back(MakeClip("v", 0, 0.0, 1.0, "dc_hot.wav"));
        p.clips.push_back(MakeClip("a", 1, 0.0, 1.0, "dc_hot.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        const double peak = ChunkPeak(chunk);
        XER_VE_CHECK(peak >= 32760.0);
        XER_VE_CHECK(peak <= 32767.0);
        // And it is positive DC, not wrapped garbage.
        XER_VE_CHECK(ChunkMean(chunk) > 30000.0);
    }
}

// ------------------------------------------------------------------
// Case 3 — transition crossfade weighting (audio ignores geometry)
// ------------------------------------------------------------------

void CheckTransitionChunk(AudioMixer* mixer, std::size_t index,
                          double lo, double hi) {
    FFmpeg::AudioFrameInfo chunk;
    std::string err;
    XER_VE_CHECK(mixer->GetChunk(index, &chunk, &err));
    const double mean = ChunkMean(chunk);
    XER_VE_CHECK(mean > lo);
    XER_VE_CHECK(mean < hi);
}

void TestTransitions() {
    auto bank = std::make_shared<FakeBank>();
    // Both sources carry DC +-0.5 over [0,3] s — enough to cover the
    // handle regions (A needs up to out_point+0.5 = 2.5, B needs
    // in_point-0.5 = 0.5).
    bank->specs["a.wav"] = MakeDCSpec(3.0, 0.5);
    bank->specs["b.wav"] = MakeDCSpec(3.0, -0.5);

    auto make_project = [](Transition::Type type) {
        Project p = MakeProject();
        p.clips.push_back(MakeClip("a", 0, 0.0, 2.0, "a.wav", 0.0));
        p.clips.push_back(
            MakeClip("b", 0, 2.0, 2.0, "b.wav", 1.0));
        p.transitions.push_back(
            Transition{"a", "b", type, 1.0});
        return p;
    };

    for (Transition::Type type :
         {Transition::Type::Crossfade, Transition::Type::WipeLeft,
          Transition::Type::SlideRight}) {
        Project p = make_project(type);
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        XER_VE_CHECK(mixer.DurationSeconds() == 4.0);
        // Well inside A's nominal range (chunk 20, t ~ 0.43): full A.
        // NOTE: chunks must be requested in non-decreasing order —
        // request the early chunk first, then walk forward.
        CheckTransitionChunk(&mixer, 20, 14000.0, 17000.0);
        // Chunk duration 1024/48000 = 21.33 ms; window [1.5, 2.5).
        // t ~ 1.76 (chunk 82): w ~ 0.26 -> A*.74 + B*(-.5)*.26 ~ +0.24.
        CheckTransitionChunk(&mixer, 82, 4000.0, 12000.0);
        // t ~ 1.99 (chunk 93, straddles the cut): w ~ 0.5 -> ~0.
        {
            FFmpeg::AudioFrameInfo chunk;
            std::string err;
            XER_VE_CHECK(mixer.GetChunk(93, &chunk, &err));
            XER_VE_CHECK(std::abs(ChunkMean(chunk)) < 1500.0);
        }
        // t ~ 2.25 (chunk 105): w ~ 0.75 -> ~ -0.25.
        CheckTransitionChunk(&mixer, 105, -12000.0, -4000.0);
        // Well inside B's nominal range (chunk 140, t ~ 2.99): full B.
        CheckTransitionChunk(&mixer, 140, -17000.0, -14000.0);
        mixer.Shutdown();
    }
}

// ------------------------------------------------------------------
// Case 4 — chunking contract / progress
// ------------------------------------------------------------------

void TestChunking() {
    auto bank = std::make_shared<FakeBank>();
    bank->specs["sine.wav"] = MakeSineSpec(1.0);
    Project p = MakeProject();
    p.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "sine.wav"));
    AudioMixer mixer;
    XER_VE_CHECK(InitMixer(&mixer, p, bank));

    XER_VE_CHECK(mixer.ChunkCount() == 47);
    bool formats_ok = true;
    bool counts_ok = true;
    for (std::size_t i = 0; i < mixer.ChunkCount(); ++i) {
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(i, &chunk, &err));
        if (chunk.sample_rate != 48000 || chunk.channels != 2 ||
            chunk.sample_format != "s16" || chunk.planar ||
            chunk.sample_count <= 0) {
            formats_ok = false;
        }
        const std::size_t expected =
            (i + 1 == mixer.ChunkCount()) ? 896 : 1024;
        if (static_cast<std::size_t>(chunk.sample_count) != expected) {
            counts_ok = false;
        }
        // pts is the debug aid: index * chunk_samples.
        if (chunk.pts != static_cast<std::int64_t>(i) * 1024) {
            formats_ok = false;
        }
    }
    XER_VE_CHECK(formats_ok);
    XER_VE_CHECK(counts_ok);

    // index >= ChunkCount -> false + pinned error string.
    FFmpeg::AudioFrameInfo chunk;
    std::string err;
    XER_VE_CHECK(!mixer.GetChunk(mixer.ChunkCount(), &chunk, &err));
    XER_VE_CHECK(err.find("chunk index out of range") !=
                 std::string::npos);
    XER_VE_CHECK(!mixer.GetChunk(1000, &chunk, &err));
}

// ------------------------------------------------------------------
// Case 5 — resample / format conversion (all fake, no FFmpeg)
// ------------------------------------------------------------------

void TestResampleAndFormats() {
    // 44.1 kHz s16 stereo DC 0.5 -> 48 kHz s16 stereo; DC is
    // rate-invariant so the output must sit at ~0.5 * 32767 and the
    // last chunk must still carry signal (duration preserved).
    {
        auto bank = std::make_shared<FakeBank>();
        bank->specs["tone441.wav"] = MakeDCSpec(1.0, 0.5, 44100);
        Project p = MakeProject();
        p.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "tone441.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        XER_VE_CHECK(mixer.ChunkCount() == 47);
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(std::abs(ChunkMean(chunk) - 0.5 * 32767.0) < 400.0);
        // Tail chunk: still DC, not silence — the 44.1k stream covered
        // the full clip span through media-time interpolation.
        XER_VE_CHECK(mixer.GetChunk(46, &chunk, &err));
        XER_VE_CHECK(chunk.sample_count == 896);
        XER_VE_CHECK(std::abs(ChunkMean(chunk) - 0.5 * 32767.0) < 600.0);
    }

    // Mono s16 -> dual-mono: L == R everywhere.
    {
        auto bank = std::make_shared<FakeBank>();
        bank->specs["mono.wav"] = MakeDCSpec(1.0, 0.5, 48000, 1);
        Project p = MakeProject();
        p.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "mono.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        const auto* pcm =
            reinterpret_cast<const std::int16_t*>(chunk.data.data());
        bool dual = true;
        for (std::size_t i = 0; i < chunk.data.size() / 4; ++i) {
            if (pcm[i * 2] != pcm[i * 2 + 1]) {
                dual = false;
                break;
            }
        }
        XER_VE_CHECK(dual);
        XER_VE_CHECK(std::abs(ChunkMean(chunk) - 0.5 * 32767.0) < 400.0);
    }

    // u8 DC 192 -> (192-128)/128 = 0.5.
    {
        auto bank = std::make_shared<FakeBank>();
        FakeSpec spec;
        spec.sample_format = "u8";
        spec.channels = 2;
        spec.time_base = 1.0 / 48000.0;
        FillSpecRange(&spec, 1.0, 2048,
                      [](FFmpeg::AudioFrameInfo* f, int frames,
                         std::int64_t) {
                          f->sample_rate = 48000;
                          f->channels = 2;
                          f->planar = false;
                          f->sample_format = "u8";
                          f->sample_count = frames;
                          f->data.assign(
                              static_cast<std::size_t>(frames) * 2u, 192);
                      });
        bank->specs["u8.wav"] = std::move(spec);
        Project p = MakeProject();
        p.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "u8.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(std::abs(ChunkMean(chunk) - 0.5 * 32767.0) < 500.0);
    }

    // s32 DC 0.5 * 2^31.
    {
        auto bank = std::make_shared<FakeBank>();
        FakeSpec spec;
        spec.sample_format = "s32";
        spec.channels = 2;
        spec.time_base = 1.0 / 48000.0;
        FillSpecRange(&spec, 1.0, 2048,
                      [](FFmpeg::AudioFrameInfo* f, int frames,
                         std::int64_t) {
                          f->sample_rate = 48000;
                          f->channels = 2;
                          f->planar = false;
                          f->sample_format = "s32";
                          f->sample_count = frames;
                          f->data.assign(
                              static_cast<std::size_t>(frames) * 2u * 4u,
                              0);
                          auto* pcm = reinterpret_cast<std::int32_t*>(
                              f->data.data());
                          for (int i = 0; i < frames * 2; ++i) {
                              pcm[i] = 1073741824;  // 0.5 * 2^31
                          }
                      });
        bank->specs["s32.wav"] = std::move(spec);
        Project p = MakeProject();
        p.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "s32.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(std::abs(ChunkMean(chunk) - 0.5 * 32767.0) < 500.0);
    }

    // flt DC 0.5f (packed float).
    {
        auto bank = std::make_shared<FakeBank>();
        FakeSpec spec;
        spec.sample_format = "flt";
        spec.channels = 2;
        spec.time_base = 1.0 / 48000.0;
        FillSpecRange(&spec, 1.0, 2048,
                      [](FFmpeg::AudioFrameInfo* f, int frames,
                         std::int64_t) {
                          f->sample_rate = 48000;
                          f->channels = 2;
                          f->planar = false;
                          f->sample_format = "flt";
                          f->sample_count = frames;
                          f->data.assign(
                              static_cast<std::size_t>(frames) * 2u * 4u,
                              0);
                          auto* pcm = reinterpret_cast<float*>(
                              f->data.data());
                          for (int i = 0; i < frames * 2; ++i) {
                              pcm[i] = 0.5f;
                          }
                      });
        bank->specs["flt.wav"] = std::move(spec);
        Project p = MakeProject();
        p.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "flt.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(std::abs(ChunkMean(chunk) - 0.5 * 32767.0) < 300.0);
    }

    // Planar s16p vs interleaved s16 of the same DC -> identical output.
    {
        auto bank = std::make_shared<FakeBank>();
        FakeSpec interleaved = MakeDCSpec(1.0, 0.5);
        bank->specs["dc_i.wav"] = interleaved;
        FakeSpec planar;
        planar.sample_rate = 48000;
        planar.channels = 2;
        planar.time_base = 1.0 / 48000.0;
        planar.duration = 1.0;
        FillSpecRange(&planar, 1.0, 2048,
                      [](FFmpeg::AudioFrameInfo* f, int frames,
                         std::int64_t) {
                          FillS16PlanarDC(f, 48000, 2, frames, 0.5);
                      });
        bank->specs["dc_p.wav"] = std::move(planar);

        FFmpeg::AudioFrameInfo a, b;
        {
            Project p = MakeProject();
            p.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "dc_i.wav"));
            AudioMixer mixer;
            XER_VE_CHECK(InitMixer(&mixer, p, bank));
            std::string err;
            XER_VE_CHECK(mixer.GetChunk(23, &a, &err));
        }
        {
            Project p = MakeProject();
            p.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "dc_p.wav"));
            AudioMixer mixer;
            XER_VE_CHECK(InitMixer(&mixer, p, bank));
            std::string err;
            XER_VE_CHECK(mixer.GetChunk(23, &b, &err));
        }
        XER_VE_CHECK(a.data == b.data);
        XER_VE_CHECK(a.sample_count == b.sample_count);
    }
}

// ------------------------------------------------------------------
// Case 6 — media-time anchoring / sparse pts / EOF
// ------------------------------------------------------------------

void TestAnchoringAndSparsePts() {
    // pts GAP: blocks cover [0,0.5) and [0.7,1.0) — the hole must come
    // out as zeros, not shifted audio.
    {
        auto bank = std::make_shared<FakeBank>();
        FakeSpec spec;
        spec.sample_rate = 48000;
        spec.channels = 2;
        spec.time_base = 1.0 / 48000.0;
        spec.duration = 1.0;
        for (double start = 0.0; start < 0.5; start += 0.1) {
            FFmpeg::AudioFrameInfo f;
            FillS16DC(&f, 48000, 2, 4800, 0.6);
            AddBlock(&spec, start, std::move(f));
        }
        for (double start = 0.7; start < 1.0; start += 0.1) {
            FFmpeg::AudioFrameInfo f;
            FillS16DC(&f, 48000, 2, 4800, 0.6);
            AddBlock(&spec, start, std::move(f));
        }
        bank->specs["gap.wav"] = std::move(spec);
        Project p = MakeProject();
        p.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "gap.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(20, &chunk, &err));  // t ~ 0.43
        XER_VE_CHECK(ChunkPeak(chunk) > 15000.0);
        // Chunk 28 covers [0.5867, 0.6083) — inside the hole.
        XER_VE_CHECK(mixer.GetChunk(28, &chunk, &err));
        XER_VE_CHECK(ChunkAllZero(chunk));
        // Chunk 40 covers [0.853, 0.875) — audio resumes.
        XER_VE_CHECK(mixer.GetChunk(40, &chunk, &err));
        XER_VE_CHECK(ChunkPeak(chunk) > 15000.0);
    }

    // First frame pts > 0 (AAC-priming-like): clip in_point = 0.5 must
    // play the media at ~0.5, not frame 0. Source: DC +0.8 on [0.5,0.7),
    // DC -0.8 on [0.7,1.0); clip [0,0.5) with in_point 0.5.
    {
        auto bank = std::make_shared<FakeBank>();
        FakeSpec spec;
        spec.sample_rate = 48000;
        spec.channels = 2;
        spec.time_base = 1.0 / 48000.0;
        spec.duration = 1.0;
        for (double start = 0.5; start < 0.7; start += 0.05) {
            FFmpeg::AudioFrameInfo f;
            FillS16DC(&f, 48000, 2, 2400, 0.8);
            AddBlock(&spec, start, std::move(f));
        }
        for (double start = 0.7; start < 1.0; start += 0.05) {
            FFmpeg::AudioFrameInfo f;
            FillS16DC(&f, 48000, 2, 2400, -0.8);
            AddBlock(&spec, start, std::move(f));
        }
        bank->specs["primed.wav"] = std::move(spec);
        Project p = MakeProject();
        p.clips.push_back(
            MakeClip("c1", 0, 0.0, 0.5, "primed.wav", 0.5));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        XER_VE_CHECK(mixer.ChunkCount() == 24);  // ceil(0.5*48000/1024)
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        // t ~ 0.1 -> src ~0.6 -> +0.8 region.
        XER_VE_CHECK(mixer.GetChunk(4, &chunk, &err));
        XER_VE_CHECK(ChunkMean(chunk) > 15000.0);
        // t ~ 0.4 -> src ~0.9 -> -0.8 region.
        XER_VE_CHECK(mixer.GetChunk(18, &chunk, &err));
        XER_VE_CHECK(ChunkMean(chunk) < -15000.0);
    }

    // Missing pts entirely -> continuity fallback anchored at
    // GetAudioStartSeconds (0.3 here): clip in_point 0.3 lines up.
    {
        auto bank = std::make_shared<FakeBank>();
        FakeSpec spec;
        spec.sample_rate = 48000;
        spec.channels = 2;
        spec.time_base = 1.0 / 48000.0;
        spec.has_pts = false;
        spec.audio_start = 0.3;
        spec.duration = 1.0;
        // Blocks tile [0.3, 1.0) contiguously (media_start used only
        // for Seek bookkeeping — pts are NOPTS so the mixer must rely
        // on pcm_media_end continuity).
        for (double start = 0.3; start < 1.0; start += 0.1) {
            FFmpeg::AudioFrameInfo f;
            FillS16DC(&f, 48000, 2, 4800, 0.5);
            AddBlock(&spec, start, std::move(f));
        }
        bank->specs["nopts.wav"] = std::move(spec);
        Project p = MakeProject();
        p.clips.push_back(
            MakeClip("c1", 0, 0.0, 0.5, "nopts.wav", 0.3));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(10, &chunk, &err));
        XER_VE_CHECK(std::abs(ChunkMean(chunk) - 0.5 * 32767.0) < 800.0);
    }

    // EOF mid-clip: source has 0.6 s of audio, clip runs 1.0 s — tail
    // is silence, drain completes, no pump spin.
    {
        auto bank = std::make_shared<FakeBank>();
        bank->specs["short.wav"] = MakeDCSpec(0.6, 0.6);
        Project p = MakeProject();
        p.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "short.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(20, &chunk, &err));  // t ~ 0.43
        XER_VE_CHECK(ChunkPeak(chunk) > 15000.0);
        XER_VE_CHECK(mixer.GetChunk(30, &chunk, &err));  // t ~ 0.64 — past EOF
        XER_VE_CHECK(ChunkAllZero(chunk));
        XER_VE_CHECK(mixer.GetChunk(46, &chunk, &err));  // tail
        XER_VE_CHECK(ChunkAllZero(chunk));
        // Pump guard: draining 0.6 s must take far fewer than 20000
        // iterations — proves AudioDrained terminates the loop.
        XER_VE_CHECK(bank->states.size() == 1);
        XER_VE_CHECK(bank->states[0]->next_frame_calls < 1000);
    }
}

// ------------------------------------------------------------------
// Case 7 — lifecycle: per-instance opens, session close, cancel
// ------------------------------------------------------------------

void TestLifecycle() {
    auto bank = std::make_shared<FakeBank>();
    bank->specs["shared.wav"] = MakeDCSpec(3.0, 0.4);

    // Two clips sharing one source far apart -> two decoder Opens.
    {
        Project p = MakeProject();
        p.clips.push_back(MakeClip("c1", 1, 0.0, 0.5, "shared.wav"));
        p.clips.push_back(MakeClip("c2", 1, 2.0, 0.5, "shared.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        XER_VE_CHECK(mixer.DurationSeconds() == 2.5);

        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        // March through the whole project sequentially.
        for (std::size_t i = 0; i < mixer.ChunkCount(); ++i) {
            XER_VE_CHECK(mixer.GetChunk(i, &chunk, &err));
        }
        XER_VE_CHECK(bank->open_counts["shared.wav"] == 2);
        XER_VE_CHECK(bank->states.size() == 2);

        // Clip 1's session closed at its ext_end (~0.5 s): its decoder
        // stopped pumping while clip 2's kept going. states[0] opened
        // first (earlier timeline position).
        XER_VE_CHECK(bank->states[0]->next_frame_calls > 0);
        XER_VE_CHECK(bank->states[1]->next_frame_calls > 0);
        // Re-request is out-of-order -> error, not silent corruption.
        XER_VE_CHECK(!mixer.GetChunk(0, &chunk, &err));
    }

    // Cancel mid-mix -> GetChunk false + cancelled error; Shutdown
    // idempotent.
    {
        auto bank2 = std::make_shared<FakeBank>();
        bank2->specs["sine.wav"] = MakeSineSpec(2.0);
        Project p = MakeProject();
        p.clips.push_back(MakeClip("c1", 0, 0.0, 2.0, "sine.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank2));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(0, &chunk, &err));
        mixer.Cancel();
        XER_VE_CHECK(mixer.Cancelled());
        XER_VE_CHECK(!mixer.GetChunk(1, &chunk, &err));
        XER_VE_CHECK(err.find("cancel") != std::string::npos);
        mixer.Shutdown();
        mixer.Shutdown();   // idempotent
        XER_VE_CHECK(!mixer.Cancelled());
    }
}

// ------------------------------------------------------------------
// Case 8 — FFmpeg-gated integration: real WAV through the bridge
// ------------------------------------------------------------------

void FillSineFrame(FFmpeg::AudioFrameInfo* frame, int sample_count,
                   int rate, int channels, std::int64_t first_sample) {
    frame->sample_rate = rate;
    frame->channels = channels;
    frame->sample_count = sample_count;
    frame->planar = false;
    frame->sample_format = "s16";
    frame->data.assign(
        static_cast<std::size_t>(sample_count) * channels * 2u, 0);
    auto* pcm = reinterpret_cast<std::int16_t*>(frame->data.data());
    for (int i = 0; i < sample_count; ++i) {
        const double t =
            static_cast<double>(first_sample + i) / rate;
        const std::int16_t v = static_cast<std::int16_t>(
            12000.0 * std::sin(2.0 * kPi * 440.0 * t));
        for (int c = 0; c < channels; ++c) {
            pcm[static_cast<std::size_t>(i) * channels + c] = v;
        }
    }
}

void TestFfmpegWavIntegration() {
    if (!FFmpeg::IsAvailable()) {
        return;   // self-skip — same as the other suites
    }
    const auto dir = XER::VideoEditor::Test::TempDir() / "mixer";
    std::filesystem::create_directories(dir);
    const auto wav_path = dir / "tone.wav";
    const std::string wav_utf8 = Path::ToUtf8(wav_path);

    // ~1 s stereo 48 kHz sine WAV (pcm_s16le — always available).
    constexpr int kRate = 48000;
    constexpr int kChannels = 2;
    constexpr int kTotal = kRate;
    std::vector<FFmpeg::AudioFrameInfo> frames;
    for (int done = 0; done < kTotal;) {
        const int n = std::min(1024, kTotal - done);
        FFmpeg::AudioFrameInfo f;
        FillSineFrame(&f, n, kRate, kChannels, done);
        frames.push_back(std::move(f));
        done += n;
    }
    FFmpeg::EncodeAudioParams params;
    params.output_path = wav_utf8;
    params.codec_name = "pcm_s16le";
    params.sample_rate = kRate;
    params.channels = kChannels;
    params.sample_format = "s16";
    std::string err;
    if (!FFmpeg::EncodeAudioFrames(params, frames, &err)) {
        std::fprintf(stderr,
                     "note: mixer WAV fixture encode failed: %s\n",
                     err.c_str());
        return;   // fixture unavailable — skip
    }

    // Real bridge decoder (empty factory -> DefaultDecoderFactory).
    Project p = MakeProject();
    p.tracks[1].name = "A1";
    p.clips.push_back(MakeClip("c1", 1, 0.0, 1.0, wav_utf8));
    AudioMixer mixer;
    XER_VE_CHECK(mixer.Init(p, AudioMixerOptions{}, &err));
    XER_VE_CHECK(mixer.ChunkCount() == 47);

    bool any_signal = false;
    bool tail_signal = false;
    for (std::size_t i = 0; i < mixer.ChunkCount(); ++i) {
        FFmpeg::AudioFrameInfo chunk;
        XER_VE_CHECK(mixer.GetChunk(i, &chunk, &err));
        XER_VE_CHECK(chunk.sample_rate == 48000);
        XER_VE_CHECK(chunk.channels == 2);
        XER_VE_CHECK(chunk.sample_format == "s16");
        if (ChunkPeak(chunk) > 3000.0) {
            any_signal = true;
            if (i >= 40) {
                tail_signal = true;   // ~0.85 s+ still has audio
            }
        }
    }
    XER_VE_CHECK(any_signal);
    XER_VE_CHECK(tail_signal);
}

// ------------------------------------------------------------------
// Case 9 — Phase-2: pan, track gain, gain keyframes, rate, meters
// ------------------------------------------------------------------

void TestPanTrackGainRateMeters() {
    // Equal-power stereo balance: hard-right keeps the dominant
    // channel at unity and silences the left; hard-left mirrors.
    {
        auto bank = std::make_shared<FakeBank>();
        bank->specs["dc.wav"] = MakeDCSpec(1.0, 0.5);
        Project p = MakeProject();
        Clip c = MakeClip("c1", 1, 0.0, 1.0, "dc.wav");
        c.pan = 1.0f;
        p.clips.push_back(c);
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(std::abs(ChunkMeanChannel(chunk, 0)) < 300.0);
        XER_VE_CHECK(std::abs(ChunkMeanChannel(chunk, 1) -
                              0.5 * 32767.0) < 600.0);
    }
    {
        auto bank = std::make_shared<FakeBank>();
        bank->specs["dc.wav"] = MakeDCSpec(1.0, 0.5);
        Project p = MakeProject();
        Clip c = MakeClip("c1", 1, 0.0, 1.0, "dc.wav");
        c.pan = -1.0f;
        p.clips.push_back(c);
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(std::abs(ChunkMeanChannel(chunk, 1)) < 300.0);
        XER_VE_CHECK(std::abs(ChunkMeanChannel(chunk, 0) -
                              0.5 * 32767.0) < 600.0);
    }

    // pan = 0.5: dominant (R) stays at unity -> full DC level; L is
    // attenuated but not silent (equal-power ratio ~0.41).
    {
        auto bank = std::make_shared<FakeBank>();
        bank->specs["dc.wav"] = MakeDCSpec(1.0, 0.5);
        Project p = MakeProject();
        Clip c = MakeClip("c1", 1, 0.0, 1.0, "dc.wav");
        c.pan = 0.5f;
        p.clips.push_back(c);
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        const double l = ChunkMeanChannel(chunk, 0);
        const double r = ChunkMeanChannel(chunk, 1);
        XER_VE_CHECK(std::abs(r - 0.5 * 32767.0) < 600.0);
        XER_VE_CHECK(l > 3000.0);
        XER_VE_CHECK(l < r * 0.75);
    }

    // Track gain_db: -6 dB on the audio track -> ~0.5x amplitude.
    {
        auto bank = std::make_shared<FakeBank>();
        bank->specs["dc.wav"] = MakeDCSpec(1.0, 0.5);
        Project p = MakeProject();
        p.tracks[1].gain_db = -6.0f;
        p.clips.push_back(MakeClip("c1", 1, 0.0, 1.0, "dc.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(std::abs(ChunkMean(chunk) - 0.25 * 32767.0) <
                     800.0);
    }

    // Track gain stacks with clip gain: -6 dB each -> ~0.25x.
    {
        auto bank = std::make_shared<FakeBank>();
        bank->specs["dc.wav"] = MakeDCSpec(1.0, 0.5);
        Project p = MakeProject();
        p.tracks[1].gain_db = -6.0f;
        Clip c = MakeClip("c1", 1, 0.0, 1.0, "dc.wav");
        c.gain_db = -6.0f;
        p.clips.push_back(c);
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        XER_VE_CHECK(std::abs(ChunkMean(chunk) - 0.125 * 32767.0) <
                     600.0);
    }

    // GainDb keyframes: 0 dB at u=0 fading to -40 dB at u=1 —
    // evaluated once per chunk at the chunk's clip-local start.
    // Chunk 0 starts at u=0 -> full level; chunk 40 (u ~ 0.85) ->
    // ~-34 dB -> near silence.
    {
        auto bank = std::make_shared<FakeBank>();
        bank->specs["dc.wav"] = MakeDCSpec(1.0, 0.5);
        Project p = MakeProject();
        Clip c = MakeClip("c1", 1, 0.0, 1.0, "dc.wav");
        c.keyframes[XER::VideoEditor::KeyProp::GainDb] = {
            XER::VideoEditor::Keyframe{
                0.0, 0.0f, XER::VideoEditor::KeyEase::Linear},
            XER::VideoEditor::Keyframe{
                1.0, -40.0f, XER::VideoEditor::KeyEase::Linear}};
        p.clips.push_back(c);
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(0, &chunk, &err));
        XER_VE_CHECK(ChunkPeak(chunk) > 12000.0);
        XER_VE_CHECK(mixer.GetChunk(40, &chunk, &err));
        XER_VE_CHECK(ChunkPeak(chunk) < 1500.0);
    }

    // playback_rate in the source map: +0.8 DC on media [0,0.5),
    // -0.8 DC on [0.5,1.0). Clip [0,0.5) at rate 2.0 hits the negative
    // region from t=0.25 on — rate 1 would stay positive the whole
    // clip.
    {
        auto bank = std::make_shared<FakeBank>();
        FakeSpec spec;
        spec.sample_rate = 48000;
        spec.channels = 2;
        spec.time_base = 1.0 / 48000.0;
        spec.duration = 1.0;
        for (double start = 0.0; start < 0.5; start += 0.05) {
            FFmpeg::AudioFrameInfo f;
            FillS16DC(&f, 48000, 2, 2400, 0.8);
            AddBlock(&spec, start, std::move(f));
        }
        for (double start = 0.5; start < 1.0; start += 0.05) {
            FFmpeg::AudioFrameInfo f;
            FillS16DC(&f, 48000, 2, 2400, -0.8);
            AddBlock(&spec, start, std::move(f));
        }
        bank->specs["signs.wav"] = std::move(spec);
        Project p = MakeProject();
        Clip c = MakeClip("c1", 1, 0.0, 0.5, "signs.wav");
        c.playback_rate = 2.0;
        p.clips.push_back(c);
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        // chunk 4: t ~ 0.085 -> src ~0.17 -> +0.8 region.
        XER_VE_CHECK(mixer.GetChunk(4, &chunk, &err));
        XER_VE_CHECK(ChunkMean(chunk) > 15000.0);
        // chunk 14: t ~ 0.30 -> src ~0.60 -> -0.8 region.
        XER_VE_CHECK(mixer.GetChunk(14, &chunk, &err));
        XER_VE_CHECK(ChunkMean(chunk) < -15000.0);
    }

    // LastChunkPeaks: {0,0} before rendering, ~the source amplitude
    // after — meters for the UI.
    {
        auto bank = std::make_shared<FakeBank>();
        bank->specs["dc.wav"] = MakeDCSpec(1.0, 0.5);
        Project p = MakeProject();
        p.clips.push_back(MakeClip("c1", 1, 0.0, 1.0, "dc.wav"));
        AudioMixer mixer;
        XER_VE_CHECK(InitMixer(&mixer, p, bank));
        XER_VE_CHECK(mixer.LastChunkPeaks().first == 0.0f);
        XER_VE_CHECK(mixer.LastChunkPeaks().second == 0.0f);
        FFmpeg::AudioFrameInfo chunk;
        std::string err;
        XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
        const auto peaks = mixer.LastChunkPeaks();
        XER_VE_CHECK(peaks.first > 0.45f && peaks.first < 0.55f);
        XER_VE_CHECK(peaks.second > 0.45f && peaks.second < 0.55f);
    }
}

}  // namespace

int XER_VideoEditor_RunMixerTests() {
    TestGainMuteFades();
    TestOverlapSumClamp();
    TestTransitions();
    TestChunking();
    TestResampleAndFormats();
    TestAnchoringAndSparsePts();
    TestLifecycle();
    TestFfmpegWavIntegration();
    TestPanTrackGainRateMeters();
    return 0;
}
