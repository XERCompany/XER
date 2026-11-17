// Playback suite — Phase-2 realtime program-monitor engine
// (tools/video_editor/playback). Every case runs WITHOUT FFmpeg: a
// hand-rolled IMediaDecoder bank serves scripted BGRA frames AND
// scripted AudioFrameInfo blocks through the real Compositor +
// AudioMixer paths the engine drives (one decoder instance per
// session, mirroring production's two demux sessions per A/V file).
// Determinism comes from a ManualClock injected through
// PreviewOptions::clock — SleepSeconds advances virtual time, so each
// worker tick steps the playhead by exactly one project frame times
// rate; tests poll sink counters with real-time deadlines instead of
// sleeping for wall-clock playback. NOT registered in
// tests/CMakeLists.txt — the integration patch wires it into
// XER_VideoEditor_Tests (RunPlaybackTests + add the source file).
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "video_editor_test_util.h"
#include "media_decoder.h"
#include "playback/audio_output.h"
#include "playback/frame_clock.h"
#include "playback/preview_engine.h"
#include "timeline_model.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {
namespace FFmpeg = XER::Bridge::FFmpeg;
using XER::VideoEditor::Clip;
using XER::VideoEditor::FrameClock;
using XER::VideoEditor::IAudioSink;
using XER::VideoEditor::IMediaDecoder;
using XER::VideoEditor::MediaDecoderFactory;
using XER::VideoEditor::MediaDecoderPtr;
using XER::VideoEditor::PreviewEngine;
using XER::VideoEditor::PreviewOptions;
using XER::VideoEditor::Project;
using XER::VideoEditor::Track;
using XER::VideoEditor::TrackKind;

// ------------------------------------------------------------------
// ManualClock — virtual "now" advances ONLY inside SleepSeconds (the
// engine's sole wait while playing), so one worker tick == exactly
// the requested wall interval. A 1 ms real sleep per call keeps the
// worker from spinning and gives tests a real-time cadence (~1 tick
// per ms — a 1 s playback finishes in ~30 ms).
// ------------------------------------------------------------------
class ManualClock final : public FrameClock {
public:
    double NowSeconds() const override {
        return now_.load(std::memory_order_relaxed);
    }
    void SleepSeconds(double seconds) override {
        if (seconds > 0.0) {
            now_.store(now_.load(std::memory_order_relaxed) + seconds,
                       std::memory_order_relaxed);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

private:
    std::atomic<double> now_{0.0};
};

// ------------------------------------------------------------------
// Fake decoder bank — one spec per path serves BOTH the compositor's
// video session (NextFrame -> BGRA frames) and the mixer's audio
// session (NextFrame pumps blocks -> ReadAudioFrames), like a real
// A/V container. Seek rewinds video to the first frame at/after the
// target and drops consumed audio blocks — closer to the bridge than
// the unit fakes (open/seek counts stay observable per path).
// ------------------------------------------------------------------
struct FakeFrame {
    std::vector<std::uint8_t> pixels;  // packed BGRA8, w*h*4
    double pts = 0.0;
};

struct FakeSpec {
    int width = 16;
    int height = 16;
    double fps = 30.0;
    double duration = 0.0;
    // video
    std::vector<FakeFrame> frames;
    // audio (has_audio == false -> mixer treats it as silent media)
    bool has_audio = false;
    int audio_rate = 48000;
    double time_base = 1.0 / 48000.0;
    int pump_batch = 8;
    std::vector<double> block_starts;   // media seconds per block
    int block_samples = 1024;
    double sine_hz = 440.0;
    double sine_amp = 0.5;
};

struct FakeBank {
    std::map<std::string, FakeSpec> specs;
    std::map<std::string, int> open_counts;
    std::map<std::string, int> seek_calls;

    int Opens(const std::string& path) const {
        const auto it = open_counts.find(path);
        return it == open_counts.end() ? 0 : it->second;
    }
};

class FakeDecoder final : public IMediaDecoder {
public:
    explicit FakeDecoder(std::shared_ptr<FakeBank> bank)
        : bank_(std::move(bank)) {}

    bool Open(const std::string& utf8_path,
              std::string* error_out) override {
        auto it = bank_->specs.find(utf8_path);
        if (it == bank_->specs.end()) {
            if (error_out != nullptr) {
                *error_out = "fake: no spec for " + utf8_path;
            }
            return false;
        }
        spec_ = &it->second;   // std::map nodes are reference-stable
        open_path_ = utf8_path;
        ++bank_->open_counts[utf8_path];
        open_ = true;
        video_index_ = 0;
        audio_pending_ = 0;
        audio_queue_.clear();
        position_ = -1.0;
        ++epoch_;
        return true;
    }

    void Close() override {
        open_ = false;
        video_index_ = 0;
        audio_pending_ = 0;
        audio_queue_.clear();
        position_ = -1.0;
    }

    bool IsOpen() const override { return open_; }

    // Serves video frames while ALSO pumping audio blocks — the mixer
    // relies on NextFrame being called to fill the tap queue even on
    // audio-only containers (returns false but still pumps).
    bool NextFrame(std::vector<std::uint8_t>* bgra, int* width,
                   int* height) override {
        if (!open_ || spec_ == nullptr) {
            return false;
        }
        PumpAudioBatch();
        if (bgra == nullptr || width == nullptr || height == nullptr ||
            video_index_ >= spec_->frames.size()) {
            return false;
        }
        const FakeFrame& f = spec_->frames[video_index_++];
        *bgra = f.pixels;
        *width = spec_->width;
        *height = spec_->height;
        position_ = f.pts;
        return true;
    }

    bool Seek(double seconds) override {
        if (!open_ || spec_ == nullptr) {
            return false;
        }
        ++bank_->seek_calls[open_path_];
        // Video: land on the first frame at/after the target.
        video_index_ = 0;
        while (video_index_ < spec_->frames.size() &&
               spec_->frames[video_index_].pts < seconds) {
            ++video_index_;
        }
        position_ = seconds;
        // Audio: drop blocks that end at/before the target, clear the
        // released queue, bump the epoch so consumers drop stale PCM.
        while (audio_pending_ < spec_->block_starts.size() &&
               spec_->block_starts[audio_pending_] +
                       static_cast<double>(spec_->block_samples) /
                           spec_->audio_rate <=
                   seconds) {
            ++audio_pending_;
        }
        audio_queue_.clear();
        ++epoch_;
        return true;
    }

    double GetPositionSeconds() const override { return position_; }
    double GetFps() const override {
        return spec_ != nullptr ? spec_->fps : 30.0;
    }
    double GetDurationSeconds() const override {
        return spec_ != nullptr ? spec_->duration : 0.0;
    }
    int GetWidth() const override {
        return spec_ != nullptr ? spec_->width : 0;
    }
    int GetHeight() const override {
        return spec_ != nullptr ? spec_->height : 0;
    }

    void EnableAudioTap(bool /*enable*/) override {}
    bool HasAudio() const override {
        return spec_ != nullptr && spec_->has_audio;
    }
    double GetAudioStartSeconds() const override { return 0.0; }
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
        const int limit = max_frames > 0 ? max_frames : 0x7fffffff;
        const int n = std::min(limit,
                               static_cast<int>(audio_queue_.size()));
        out->insert(out->end(), audio_queue_.begin(),
                    audio_queue_.begin() + n);
        audio_queue_.erase(audio_queue_.begin(),
                           audio_queue_.begin() + n);
        return true;
    }
    bool AudioDrained() const override {
        return spec_ != nullptr &&
               audio_pending_ >= spec_->block_starts.size() &&
               audio_queue_.empty();
    }
    std::uint64_t GetAudioEpoch() const override { return epoch_; }

private:
    void PumpAudioBatch() {
        const int release =
            std::min(spec_->pump_batch,
                     static_cast<int>(spec_->block_starts.size()) -
                         static_cast<int>(audio_pending_));
        for (int i = 0; i < release; ++i) {
            const double start = spec_->block_starts[audio_pending_];
            FFmpeg::AudioFrameInfo block;
            block.stream_index = 1;
            block.sample_rate = spec_->audio_rate;
            block.channels = 2;
            block.planar = false;
            block.sample_format = "s16";
            block.sample_count = spec_->block_samples;
            block.pts = static_cast<std::int64_t>(
                std::llround(start / spec_->time_base));
            block.best_effort_timestamp = block.pts;
            block.data.assign(
                static_cast<std::size_t>(spec_->block_samples) * 2u *
                    2u,
                0);
            auto* pcm = reinterpret_cast<std::int16_t*>(
                block.data.data());
            const std::int64_t first =
                static_cast<std::int64_t>(
                    std::llround(start * spec_->audio_rate));
            for (int s = 0; s < spec_->block_samples; ++s) {
                const double tt = static_cast<double>(first + s) /
                                  spec_->audio_rate;
                const auto v = static_cast<std::int16_t>(std::clamp(
                    spec_->sine_amp *
                            std::sin(2.0 * kPi * spec_->sine_hz * tt),
                        -1.0, 1.0) * 32767.0);
                pcm[s * 2 + 0] = v;
                pcm[s * 2 + 1] = v;
            }
            audio_queue_.push_back(std::move(block));
            ++audio_pending_;
        }
    }

    static constexpr double kPi = 3.14159265358979323846;
    std::shared_ptr<FakeBank> bank_;
    const FakeSpec* spec_ = nullptr;
    bool open_ = false;
    std::string open_path_;
    std::size_t video_index_ = 0;
    std::size_t audio_pending_ = 0;
    std::deque<FFmpeg::AudioFrameInfo> audio_queue_;
    double position_ = -1.0;
    std::uint64_t epoch_ = 0;
};

MediaDecoderFactory MakeFakeFactory(std::shared_ptr<FakeBank> bank) {
    return [bank]() -> MediaDecoderPtr {
        return std::make_unique<FakeDecoder>(bank);
    };
}

// ------------------------------------------------------------------
// Spec / project builders
// ------------------------------------------------------------------
using Color = std::array<std::uint8_t, 4>;  // {B,G,R,A}

FakeFrame MakeSolidFrame(int w, int h, const Color& c, double pts) {
    FakeFrame f;
    f.pts = pts;
    f.pixels.assign(static_cast<std::size_t>(w) * h * 4u, 0);
    for (std::size_t i = 0; i < f.pixels.size(); i += 4) {
        f.pixels[i + 0] = c[0];
        f.pixels[i + 1] = c[1];
        f.pixels[i + 2] = c[2];
        f.pixels[i + 3] = c[3];
    }
    return f;
}

// Ramp spec: frame i is solid (0, 0, base_r + i*r_step, 255) so the
// delivered pixel's R channel identifies the source frame index.
FakeSpec MakeRampSpec(int w, int h, double fps, int count,
                      std::uint8_t base_r, int r_step) {
    FakeSpec s;
    s.width = w;
    s.height = h;
    s.fps = fps;
    s.duration = static_cast<double>(count) / fps;
    for (int i = 0; i < count; ++i) {
        const Color c{0, 0,
                      static_cast<std::uint8_t>(base_r + i * r_step),
                      255};
        s.frames.push_back(
            MakeSolidFrame(w, h, c, static_cast<double>(i) / fps));
    }
    return s;
}

void AddAudio(FakeSpec* spec, double seconds, int samples_per_block,
              double amp) {
    spec->has_audio = true;
    spec->block_samples = samples_per_block;
    spec->sine_amp = amp;
    const int blocks = static_cast<int>(
        std::ceil(seconds * spec->audio_rate / samples_per_block));
    for (int i = 0; i < blocks; ++i) {
        spec->block_starts.push_back(
            static_cast<double>(i) * samples_per_block /
            spec->audio_rate);
    }
}

Project MakeProject(double clip_duration) {
    Project p;
    p.width = 16;
    p.height = 16;
    p.fps_num = 30;
    p.fps_den = 1;
    p.bg_color = {0, 0, 0, 255};
    p.tracks.push_back(Track{TrackKind::Video, "V1"});
    Clip c;
    c.id = "c1";
    c.source_path = "av.mp4";
    c.track = 0;
    c.timeline_start = 0.0;
    c.in_point = 0.0;
    c.duration = clip_duration;
    p.clips.push_back(c);
    return p;
}

// ------------------------------------------------------------------
// Sinks — worker-thread callbacks; all state mutex-guarded.
// ------------------------------------------------------------------
class RecordingSink final : public PreviewEngine::Sink {
public:
    void OnVideoFrame(const video::Frame& frame) override {
        std::lock_guard<std::mutex> lk(mutex_);
        ++frames;
        last_w = frame.Width();
        last_h = frame.Height();
        last_pts = frame.Pts();
        last_pixels.assign(frame.Data(),
                           frame.Data() + frame.DataSize());
    }
    void OnAudioPeakLevels(float l, float r) override {
        std::lock_guard<std::mutex> lk(mutex_);
        ++peak_calls;
        last_peak_l = l;
        last_peak_r = r;
    }
    void OnPlayhead(double t) override {
        std::lock_guard<std::mutex> lk(mutex_);
        playheads.push_back(t);
    }

    int FrameCount() {
        std::lock_guard<std::mutex> lk(mutex_);
        return frames;
    }
    std::vector<double> Playheads() {
        std::lock_guard<std::mutex> lk(mutex_);
        return playheads;
    }
    // {w, h, R channel of pixel (0,0)} of the last delivered frame.
    void LastFrame(int* w, int* h, int* r) {
        std::lock_guard<std::mutex> lk(mutex_);
        *w = last_w;
        *h = last_h;
        *r = last_pixels.size() >= 4 ? last_pixels[2] : -1;
    }
    int PeakCalls() {
        std::lock_guard<std::mutex> lk(mutex_);
        return peak_calls;
    }
    std::pair<float, float> LastPeaks() {
        std::lock_guard<std::mutex> lk(mutex_);
        return {last_peak_l, last_peak_r};
    }

private:
    std::mutex mutex_;
    int frames = 0;
    int last_w = 0;
    int last_h = 0;
    std::int64_t last_pts = 0;
    std::vector<std::uint8_t> last_pixels;
    std::vector<double> playheads;
    int peak_calls = 0;
    float last_peak_l = 0.0f;
    float last_peak_r = 0.0f;
};

class CountingAudioSink final : public IAudioSink {
public:
    bool Write(const std::int16_t* samples, int frame_count,
               int sample_rate) override {
        std::lock_guard<std::mutex> lk(mutex_);
        ++writes;
        total_frames += frame_count;
        last_rate = sample_rate;
        last_count = frame_count;
        if (samples == nullptr) {
            null_samples = true;
        }
        return true;
    }
    int Writes() {
        std::lock_guard<std::mutex> lk(mutex_);
        return writes;
    }
    int TotalFrames() {
        std::lock_guard<std::mutex> lk(mutex_);
        return total_frames;
    }
    int LastRate() {
        std::lock_guard<std::mutex> lk(mutex_);
        return last_rate;
    }
    int LastCount() {
        std::lock_guard<std::mutex> lk(mutex_);
        return last_count;
    }
    bool SawNull() {
        std::lock_guard<std::mutex> lk(mutex_);
        return null_samples;
    }

private:
    std::mutex mutex_;
    int writes = 0;
    int total_frames = 0;
    int last_rate = 0;
    int last_count = 0;
    bool null_samples = false;
};

// ------------------------------------------------------------------
// Rig + polling helpers
// ------------------------------------------------------------------
struct Rig {
    std::shared_ptr<FakeBank> bank = std::make_shared<FakeBank>();
    std::shared_ptr<ManualClock> clock = std::make_shared<ManualClock>();
    RecordingSink sink;
    CountingAudioSink audio;
    PreviewEngine engine;
    bool init_ok = false;
    std::string err;
};

// Rig is non-movable (engine + mutexes) — fill in place.
void InitRig(Rig& rig, const Project& project) {
    PreviewOptions options;
    options.decoder_factory = MakeFakeFactory(rig.bank);
    options.clock = rig.clock;
    options.audio_sink = &rig.audio;
    rig.init_ok = rig.engine.Init(project, options, &rig.sink, &rig.err);
}

// Polls `cond` up to `ms` real milliseconds — the worker ticks ~1/ms
// under ManualClock, so a few hundred ms covers dozens of ticks.
template <typename F>
bool WaitFor(F&& cond, int ms = 5000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!cond()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

constexpr double kFrameDt = 1.0 / 30.0;

// ------------------------------------------------------------------
// Cases
// ------------------------------------------------------------------

// Init presents the sequence head while paused, then idles.
void TestInitPresentsFirstFrame() {
    Rig rig;
    InitRig(rig, MakeProject(1.0));
    rig.bank->specs["av.mp4"] = MakeRampSpec(16, 16, 30.0, 30, 10, 5);
    XER_VE_CHECK(rig.init_ok);
    XER_VE_CHECK(WaitFor([&] { return rig.sink.FrameCount() >= 1; }));
    int w = 0, h = 0, r = -1;
    rig.sink.LastFrame(&w, &h, &r);
    XER_VE_CHECK(w == 16 && h == 16);
    XER_VE_CHECK(r == 10);   // frame 0 of the ramp
    XER_VE_CHECK(!rig.engine.Playing());
    const auto ph = rig.sink.Playheads();
    XER_VE_CHECK(!ph.empty() && ph.front() == 0.0);
    // Paused engine does not keep presenting.
    const int seen = rig.sink.FrameCount();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    XER_VE_CHECK(rig.sink.FrameCount() == seen);
}

// Wall-clock advance: with the manual clock each playing tick steps
// exactly one project frame.
void TestPlayAdvancesByFrame() {
    Rig rig;
    InitRig(rig, MakeProject(2.0));
    rig.bank->specs["av.mp4"] = MakeRampSpec(16, 16, 30.0, 60, 10, 3);
    XER_VE_CHECK(rig.init_ok);
    XER_VE_CHECK(WaitFor([&] { return rig.sink.FrameCount() >= 1; }));
    const int base = rig.sink.FrameCount();
    rig.engine.SetPlaying(true);
    XER_VE_CHECK(
        WaitFor([&] { return rig.sink.FrameCount() >= base + 8; }));
    rig.engine.SetPlaying(false);
    // ~8-9 playing ticks after the first present -> playhead sits a
    // few frame intervals in. Generous bounds: the first playing tick
    // re-renders t=0.
    const double ph = rig.engine.Playhead();
    XER_VE_CHECK(ph >= 4.0 * kFrameDt && ph <= 12.0 * kFrameDt);
    // OnPlayhead is non-decreasing during forward playback.
    const auto seq = rig.sink.Playheads();
    for (std::size_t i = 1; i < seq.size(); ++i) {
        XER_VE_CHECK(seq[i] >= seq[i - 1] - 1e-9);
    }
    // And it really stepped by ~one frame per tick (not stalled).
    bool saw_step = false;
    for (std::size_t i = 1; i < seq.size(); ++i) {
        if (seq[i] - seq[i - 1] > 0.9 * kFrameDt) {
            saw_step = true;
        }
    }
    XER_VE_CHECK(saw_step);
    // Forward playback never re-opens: exactly 2 opens total — the
    // compositor's video session plus the mixer's one-time HasAudio
    // probe (the spec has no audio, so it contributes silence).
    XER_VE_CHECK(rig.bank->Opens("av.mp4") == 2);
}

// Rate math: at rate 2 the playhead advances ~2 frames per tick.
void TestRateAdvance() {
    Rig rig;
    InitRig(rig, MakeProject(4.0));
    rig.bank->specs["av.mp4"] = MakeRampSpec(16, 16, 30.0, 120, 10, 2);
    XER_VE_CHECK(rig.init_ok);
    XER_VE_CHECK(WaitFor([&] { return rig.sink.FrameCount() >= 1; }));
    rig.engine.Seek(1.0);
    XER_VE_CHECK(rig.engine.Playhead() == 1.0);
    const int base = rig.sink.FrameCount();
    rig.engine.SetRate(2.0);
    rig.engine.SetPlaying(true);
    XER_VE_CHECK(
        WaitFor([&] { return rig.sink.FrameCount() >= base + 9; }));
    rig.engine.SetPlaying(false);
    // 8+ playing ticks * 2*frame_dt => >= ~0.4 s past the seek point
    // (1.0 + 0.4 = 1.4); rate 1 would reach only ~1.2.
    const double ph = rig.engine.Playhead();
    XER_VE_CHECK(ph >= 1.0 + 8.0 * kFrameDt);
    XER_VE_CHECK(ph <= 1.0 + 24.0 * kFrameDt);
}

// Backward seek re-opens the compositor session and renders the
// landing frame — no stale picture.
void TestBackwardSeekReopens() {
    Rig rig;
    InitRig(rig, MakeProject(1.0));
    rig.bank->specs["av.mp4"] = MakeRampSpec(16, 16, 30.0, 30, 10, 5);
    XER_VE_CHECK(rig.init_ok);
    XER_VE_CHECK(WaitFor([&] { return rig.sink.FrameCount() >= 1; }));
    rig.engine.SetPlaying(true);
    // Play ~10 ticks so the compositor decodes well past frame 0.
    const int base = rig.sink.FrameCount();
    XER_VE_CHECK(
        WaitFor([&] { return rig.sink.FrameCount() >= base + 10; }));
    rig.engine.SetPlaying(false);
    XER_VE_CHECK(rig.engine.Playhead() > 6.0 * kFrameDt);
    const int opens_before = rig.bank->Opens("av.mp4");
    rig.engine.Seek(kFrameDt);   // t = 1/30 -> ramp frame 1 (R=15)
    // The seek's paused present emits OnPlayhead(seek_t) AFTER the
    // frame delivery — deterministic landing signal.
    XER_VE_CHECK(WaitFor([&] {
        const auto seq = rig.sink.Playheads();
        return !seq.empty() &&
               std::fabs(seq.back() - kFrameDt) < 1e-9;
    }));
    int w = 0, h = 0, r = -1;
    rig.sink.LastFrame(&w, &h, &r);
    XER_VE_CHECK(r >= 10 && r <= 20);   // near frame 1, not the stale one
    XER_VE_CHECK(rig.bank->Opens("av.mp4") > opens_before);
}

// Loop [0.2, 0.5) wraps the playhead back near 0.2 and re-opens.
void TestLoopWrap() {
    Rig rig;
    InitRig(rig, MakeProject(1.0));
    rig.bank->specs["av.mp4"] = MakeRampSpec(16, 16, 30.0, 30, 10, 5);
    XER_VE_CHECK(rig.init_ok);
    XER_VE_CHECK(WaitFor([&] { return rig.sink.FrameCount() >= 1; }));
    rig.engine.SetLoop(0.2, 0.5);
    rig.engine.Seek(0.45);
    rig.engine.SetPlaying(true);
    // Wait until a playhead callback lands back inside the loop start.
    XER_VE_CHECK(WaitFor([&] {
        const auto seq = rig.sink.Playheads();
        for (std::size_t i = 1; i < seq.size(); ++i) {
            if (seq[i - 1] > 0.45 && seq[i] < 0.35) {
                return true;
            }
        }
        return false;
    }));
    rig.engine.SetPlaying(false);
    const auto seq = rig.sink.Playheads();
    bool wrapped = false;
    double after = 0.0;
    for (std::size_t i = 1; i < seq.size(); ++i) {
        if (seq[i - 1] > 0.45 && seq[i] < 0.35) {
            wrapped = true;
            after = seq[i];
            break;
        }
    }
    XER_VE_CHECK(wrapped);
    XER_VE_CHECK(after >= 0.2 && after < 0.35);
    // The wrap is a backward jump -> sessions re-opened.
    XER_VE_CHECK(rig.bank->Opens("av.mp4") >= 2);
}

// Negative rate walks the playhead backwards and pauses at t=0.
void TestReverseShuttle() {
    Rig rig;
    InitRig(rig, MakeProject(1.0));
    rig.bank->specs["av.mp4"] = MakeRampSpec(16, 16, 30.0, 30, 10, 5);
    XER_VE_CHECK(rig.init_ok);
    XER_VE_CHECK(WaitFor([&] { return rig.sink.FrameCount() >= 1; }));
    rig.engine.Seek(0.4);
    rig.engine.SetRate(-1.0);
    rig.engine.SetPlaying(true);
    // 0.4 s at 30 fps ~ 12 ticks (~15 ms) — auto-pause at t=0.
    XER_VE_CHECK(WaitFor([&] { return !rig.engine.Playing(); }));
    XER_VE_CHECK(rig.engine.Playhead() <= kFrameDt);
    const auto seq = rig.sink.Playheads();
    bool decreased = false;
    for (std::size_t i = 1; i < seq.size(); ++i) {
        if (seq[i] < seq[i - 1] - 0.9 * kFrameDt) {
            decreased = true;
        }
    }
    XER_VE_CHECK(decreased);
    // Reverse ticks are backward jumps -> repeated session reopens.
    XER_VE_CHECK(rig.bank->Opens("av.mp4") >= 2);
}

// Forward playback mixes audio and feeds the sink in order.
void TestAudioPump() {
    Rig rig;
    InitRig(rig, MakeProject(1.0));
    FakeSpec spec = MakeRampSpec(16, 16, 30.0, 30, 10, 5);
    AddAudio(&spec, 1.0, 1024, 0.5);
    rig.bank->specs["av.mp4"] = std::move(spec);
    XER_VE_CHECK(rig.init_ok);
    XER_VE_CHECK(WaitFor([&] { return rig.sink.FrameCount() >= 1; }));
    rig.engine.SetPlaying(true);
    // ~0.15 s of playback = ~7 chunks of 1024 at 48 kHz.
    XER_VE_CHECK(WaitFor([&] { return rig.audio.Writes() >= 5; }));
    rig.engine.SetPlaying(false);
    XER_VE_CHECK(rig.audio.Writes() >= 5);
    XER_VE_CHECK(rig.audio.LastRate() == 48000);
    XER_VE_CHECK(rig.audio.LastCount() == 1024);
    XER_VE_CHECK(!rig.audio.SawNull());
    // Sine amp 0.5 -> meter peaks in (0.4, 0.6].
    XER_VE_CHECK(rig.sink.PeakCalls() > 0);
    const auto peaks = rig.sink.LastPeaks();
    XER_VE_CHECK(peaks.first > 0.4f && peaks.first <= 0.6f);
    XER_VE_CHECK(peaks.second > 0.4f && peaks.second <= 0.6f);
}

// Paused scrub produces no audio writes.
void TestPausedScrubIsSilent() {
    Rig rig;
    InitRig(rig, MakeProject(1.0));
    FakeSpec spec = MakeRampSpec(16, 16, 30.0, 30, 10, 5);
    AddAudio(&spec, 1.0, 1024, 0.5);
    rig.bank->specs["av.mp4"] = std::move(spec);
    XER_VE_CHECK(rig.init_ok);
    XER_VE_CHECK(WaitFor([&] { return rig.sink.FrameCount() >= 1; }));
    rig.engine.Seek(0.5);
    XER_VE_CHECK(WaitFor([&] {
        const auto seq = rig.sink.Playheads();
        return !seq.empty() && std::fabs(seq.back() - 0.5) < 1e-9;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    XER_VE_CHECK(rig.audio.Writes() == 0);
}

// Quality 0.5 delivers a half-size frame.
void TestQualityScale() {
    Rig rig;
    InitRig(rig, MakeProject(1.0));
    rig.bank->specs["av.mp4"] = MakeRampSpec(16, 16, 30.0, 30, 10, 5);
    XER_VE_CHECK(rig.init_ok);
    XER_VE_CHECK(WaitFor([&] { return rig.sink.FrameCount() >= 1; }));
    const int base = rig.sink.FrameCount();
    rig.engine.SetQuality(0.5f);
    XER_VE_CHECK(
        WaitFor([&] { return rig.sink.FrameCount() >= base + 1; }));
    int w = 0, h = 0, r = -1;
    rig.sink.LastFrame(&w, &h, &r);
    XER_VE_CHECK(w == 8 && h == 8);
    XER_VE_CHECK(r == 10);   // still frame 0's color
}

// Playing off the end auto-pauses on the duration boundary.
void TestStopAtEnd() {
    Rig rig;
    InitRig(rig, MakeProject(0.25));   // ~8 frames of runway
    rig.bank->specs["av.mp4"] = MakeRampSpec(16, 16, 30.0, 30, 10, 5);
    XER_VE_CHECK(rig.init_ok);
    XER_VE_CHECK(WaitFor([&] { return rig.sink.FrameCount() >= 1; }));
    rig.engine.SetPlaying(true);
    XER_VE_CHECK(WaitFor([&] { return !rig.engine.Playing(); }));
    XER_VE_CHECK(std::fabs(rig.engine.Playhead() - 0.25) < kFrameDt);
}

// Init validation.
void TestInitValidation() {
    PreviewEngine engine;
    RecordingSink sink;
    std::string err;
    XER_VE_CHECK(!engine.Init(MakeProject(1.0), nullptr, &err));
    Project bad = MakeProject(1.0);
    bad.width = 0;
    XER_VE_CHECK(!engine.Init(bad, &sink, &err));
    // Commands before Init are safe no-ops.
    engine.Seek(1.0);
    engine.SetPlaying(true);
    engine.SetRate(-2.0);
    engine.SetLoop(0.1, 0.9);
    engine.SetQuality(0.5f);
    XER_VE_CHECK(!engine.Playing());
}

}  // namespace

int XER_VideoEditor_RunPlaybackTests() {
    TestInitPresentsFirstFrame();
    TestPlayAdvancesByFrame();
    TestRateAdvance();
    TestBackwardSeekReopens();
    TestLoopWrap();
    TestReverseShuttle();
    TestAudioPump();
    TestPausedScrubIsSilent();
    TestQualityScale();
    TestStopAtEnd();
    TestInitValidation();
    return XER::VideoEditor::Test::Failures();
}
