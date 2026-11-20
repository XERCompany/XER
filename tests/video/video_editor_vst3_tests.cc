// VST3 clip-FX suite — per-clip plugin chains (tools/video_editor/vst)
// and their mixer insertion. Every case runs WITHOUT a real VST3
// plugin: fakes injected through Vst::IPluginInstance /
// PluginInstanceFactory cover ordering, param application, live
// tweaks, bypass-on-error, signature-driven rebuilds, and mixer-level
// pass-through with/without plugins. A compact fake IMediaDecoder
// bank serves constant-DC s16 blocks for the mixer-level checks.
//
// Also covers the editor-session helpers in vst/fx_editor_utils.h —
// the pure placement/param-persistence math behind ui/vst3_editor.cc.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "video_editor_test_util.h"
#include "audio_mixer.h"
#include "media_decoder.h"
#include "timeline_model.h"
#include "vst/fx_editor_utils.h"
#include "vst/plugin_instance.h"
#include "vst/vst3_chain.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {
namespace FFmpeg = XER::Bridge::FFmpeg;
namespace Vst = XER::VideoEditor::Vst;
using XER::VideoEditor::AudioMixer;
using XER::VideoEditor::AudioMixerOptions;
using XER::VideoEditor::Clip;
using XER::VideoEditor::ClipAudioFx;
using XER::VideoEditor::IMediaDecoder;
using XER::VideoEditor::MediaDecoderFactory;
using XER::VideoEditor::MediaDecoderPtr;
using XER::VideoEditor::Project;
using XER::VideoEditor::Track;
using XER::VideoEditor::TrackKind;
using XER::VideoEditor::Vst::Vst3Chain;

constexpr std::int64_t kNoPts =
    (std::numeric_limits<std::int64_t>::min)();  // AV_NOPTS_VALUE
constexpr std::uint32_t kGainParam = 7;    // fake gain param id

// ------------------------------------------------------------------
// Fake plugin — out[i] = in[i]*gain + offset over the interleaved
// stereo buffer. param kGainParamId drives `gain`; every SetParameter
// is recorded. `fail_after` makes the Nth (1-based) Process call fail.
// ------------------------------------------------------------------

class FakePlugin final : public Vst::IPluginInstance {
public:
    std::string name = "fake";
    float gain = 1.0f;
    float offset = 0.0f;
    int fail_after = -1;               // -1 = never
    int process_calls = 0;
    std::map<std::uint32_t, float> params;   // SetParameter log

    bool Process(const float* input, float* output,
                 std::uint32_t frames) override {
        ++process_calls;
        if (fail_after >= 0 && process_calls >= fail_after) {
            return false;
        }
        for (std::uint32_t i = 0; i < frames * 2u; ++i) {
            output[i] = input[i] * gain + offset;
        }
        return true;
    }

    bool SetParameter(std::uint32_t id, float value) override {
        params[id] = value;
        if (id == kGainParam) {
            gain = value;
        }
        return true;
    }

    std::string Name() const override { return name; }
};

// Factory bank: per-path plugin spec + create log; `missing` paths
// yield nullptr (the "plugin not installed" case).
struct FakePluginBank {
    struct Spec {
        std::string name = "fake";
        float gain = 1.0f;
        float offset = 0.0f;
        int fail_after = -1;
    };
    std::map<std::string, Spec> specs;       // key: fx.plugin_path
    std::set<std::string> missing;           // factory returns nullptr
    int create_calls = 0;
    std::vector<std::weak_ptr<FakePlugin>> live;   // created instances
};

std::shared_ptr<FakePlugin> MakeFakePlugin(
    const FakePluginBank::Spec& spec) {
    auto plugin = std::make_shared<FakePlugin>();
    plugin->name = spec.name;
    plugin->gain = spec.gain;
    plugin->offset = spec.offset;
    plugin->fail_after = spec.fail_after;
    return plugin;
}

// Unique-owning wrapper around a shared_ptr<FakePlugin> — the chain
// gets a unique_ptr (per the IPluginInstance contract) while the test
// bank keeps a weak_ptr to the inner fake for inspection.
class SharedFake final : public Vst::IPluginInstance {
public:
    explicit SharedFake(std::shared_ptr<FakePlugin> impl)
        : impl_(std::move(impl)) {}
    bool Process(const float* input, float* output,
                 std::uint32_t frames) override {
        return impl_->Process(input, output, frames);
    }
    bool SetParameter(std::uint32_t id, float value) override {
        return impl_->SetParameter(id, value);
    }
    std::string Name() const override { return impl_->Name(); }

private:
    std::shared_ptr<FakePlugin> impl_;
};

// PluginInstanceFactory over FakePluginBank — created instances stay
// observable through bank->live weak_ptrs.
Vst::PluginInstanceFactory MakePluginFactory(
    const std::shared_ptr<FakePluginBank>& bank) {
    return [bank](const ClipAudioFx& fx, double /*sample_rate*/,
                  std::uint32_t /*max_block*/,
                  std::string* error_out) -> Vst::PluginInstancePtr {
        if (bank->missing.count(fx.plugin_path) != 0u) {
            if (error_out != nullptr) {
                *error_out = "fake: no plugin at " + fx.plugin_path;
            }
            return nullptr;
        }
        auto it = bank->specs.find(fx.plugin_path);
        FakePluginBank::Spec spec =
            it != bank->specs.end() ? it->second
                                    : FakePluginBank::Spec{};
        ++bank->create_calls;
        auto plugin = MakeFakePlugin(spec);
        bank->live.push_back(plugin);
        return std::make_unique<SharedFake>(std::move(plugin));
    };
}

std::shared_ptr<FakePlugin> LastCreated(
    const std::shared_ptr<FakePluginBank>& bank) {
    return bank->live.empty() ? nullptr : bank->live.back().lock();
}

ClipAudioFx MakeFx(const std::string& path, const std::string& name,
                   bool enabled = true,
                   std::map<std::uint32_t, float> params = {}) {
    ClipAudioFx fx;
    fx.plugin_path = path;
    fx.name = name;
    fx.enabled = enabled;
    fx.params = std::move(params);
    return fx;
}

std::vector<float> MakeStereoBlock(std::uint32_t frames, float v) {
    return std::vector<float>(static_cast<std::size_t>(frames) * 2u, v);
}

// ------------------------------------------------------------------
// Chain-level tests
// ------------------------------------------------------------------

// Ordering: [x*2 then +0.25] vs [+0.25 then x*2] must differ.
void TestChainOrdering() {
    auto bank = std::make_shared<FakePluginBank>();
    bank->specs["a.vst3"] = {.name = "mul2", .gain = 2.0f, .offset = 0.0f};
    bank->specs["b.vst3"] = {.name = "add", .gain = 1.0f,
                            .offset = 0.25f};
    auto factory = MakePluginFactory(bank);

    std::vector<ClipAudioFx> fx{
        MakeFx("a.vst3", "mul2"), MakeFx("b.vst3", "add")};
    Vst3Chain chain(48000.0, 1024, factory);
    chain.Sync(fx);
    XER_VE_CHECK(chain.NodeCount() == 2);

    auto buf = MakeStereoBlock(4, 0.5f);
    chain.ProcessInterleaved(buf.data(), 4);
    // (0.5*2)+0.25 = 1.25 on every sample.
    for (float v : buf) {
        XER_VE_CHECK(std::fabs(v - 1.25f) < 1e-6f);
    }

    // Reversed order: (0.5+0.25)*2 = 1.5.
    std::vector<ClipAudioFx> rev{fx[1], fx[0]};
    chain.Sync(rev);
    buf = MakeStereoBlock(4, 0.5f);
    chain.ProcessInterleaved(buf.data(), 4);
    for (float v : buf) {
        XER_VE_CHECK(std::fabs(v - 1.5f) < 1e-6f);
    }
}

// Missing plugin -> warn once + bypass; other nodes still run.
void TestChainBypassOnMissing() {
    auto bank = std::make_shared<FakePluginBank>();
    bank->missing.insert("gone.vst3");
    bank->specs["ok.vst3"] = {.name = "mul2", .gain = 2.0f};
    auto factory = MakePluginFactory(bank);

    std::vector<std::string> warnings;
    Vst3Chain chain(
        48000.0, 1024, factory,
        [&warnings](const std::string& m) { warnings.push_back(m); });
    chain.Sync({MakeFx("gone.vst3", "Gone"), MakeFx("ok.vst3", "Mul2")});

    auto buf = MakeStereoBlock(4, 0.5f);
    chain.ProcessInterleaved(buf.data(), 4);
    chain.ProcessInterleaved(buf.data(), 4);   // 0.5 -> 2.0 over 2 runs
    // ok.vst3 still processed both chunks: 0.5*2*2 = 2.0.
    for (float v : buf) {
        XER_VE_CHECK(std::fabs(v - 2.0f) < 1e-6f);
    }
    XER_VE_CHECK(warnings.size() == 1);        // warned once, not per chunk
    XER_VE_CHECK(chain.NodeBypassed(0));
    XER_VE_CHECK(!chain.NodeBypassed(1));
    XER_VE_CHECK(bank->create_calls == 1);     // missing node never created
}

// Disabled node never instantiates and passes audio through.
void TestChainDisabledNode() {
    auto bank = std::make_shared<FakePluginBank>();
    bank->specs["x.vst3"] = {.name = "mul4", .gain = 4.0f};
    auto factory = MakePluginFactory(bank);

    Vst3Chain chain(48000.0, 1024, factory);
    chain.Sync({MakeFx("x.vst3", "Mul4", /*enabled=*/false)});
    auto buf = MakeStereoBlock(4, 0.5f);
    chain.ProcessInterleaved(buf.data(), 4);
    for (float v : buf) {
        XER_VE_CHECK(std::fabs(v - 0.5f) < 1e-6f);
    }
    XER_VE_CHECK(bank->create_calls == 0);
    XER_VE_CHECK(!chain.NodeInstantiated(0));
}

// Serialized params apply on instantiation; SetParam works live and
// pre-instantiation.
void TestChainParams() {
    auto bank = std::make_shared<FakePluginBank>();
    bank->specs["p.vst3"] = {.name = "gain"};
    auto factory = MakePluginFactory(bank);

    Vst3Chain chain(48000.0, 1024, factory);
    chain.Sync({MakeFx("p.vst3", "Gain", true, {{kGainParam, 0.25f}})});
    auto buf = MakeStereoBlock(2, 1.0f);
    chain.ProcessInterleaved(buf.data(), 2);
    XER_VE_CHECK(std::fabs(buf[0] - 0.25f) < 1e-6f);
    auto plugin = LastCreated(bank);
    XER_VE_CHECK(plugin != nullptr);
    XER_VE_CHECK(plugin != nullptr &&
                 std::fabs(plugin->params[kGainParam] - 0.25f) < 1e-6f);

    // Live tweak on the instantiated node.
    chain.SetParam(0, kGainParam, 0.5f);
    buf = MakeStereoBlock(2, 1.0f);
    chain.ProcessInterleaved(buf.data(), 2);
    XER_VE_CHECK(std::fabs(buf[0] - 0.5f) < 1e-6f);

    // Pre-instantiation tweak on a fresh chain: recorded then applied
    // when the node first processes.
    auto bank2 = std::make_shared<FakePluginBank>();
    bank2->specs["p.vst3"] = {.name = "gain"};
    Vst3Chain chain2(48000.0, 1024, MakePluginFactory(bank2));
    chain2.Sync({MakeFx("p.vst3", "Gain")});
    chain2.SetParam(0, kGainParam, 0.75f);
    buf = MakeStereoBlock(2, 1.0f);
    chain2.ProcessInterleaved(buf.data(), 2);
    XER_VE_CHECK(std::fabs(buf[0] - 0.75f) < 1e-6f);
    auto plugin2 = LastCreated(bank2);
    XER_VE_CHECK(plugin2 != nullptr &&
                 std::fabs(plugin2->params[kGainParam] - 0.75f) < 1e-6f);

    // Out-of-range node index: ignored, no crash.
    chain.SetParam(9, kGainParam, 1.0f);
}

// Same fx list -> no reinstantiation; changed list -> rebuild.
void TestChainSyncRebuild() {
    auto bank = std::make_shared<FakePluginBank>();
    bank->specs["a.vst3"] = {.name = "one", .gain = 2.0f};
    bank->specs["b.vst3"] = {.name = "two", .gain = 3.0f};
    auto factory = MakePluginFactory(bank);

    Vst3Chain chain(48000.0, 1024, factory);
    std::vector<ClipAudioFx> fx{MakeFx("a.vst3", "One")};
    chain.Sync(fx);
    auto buf = MakeStereoBlock(2, 1.0f);
    chain.ProcessInterleaved(buf.data(), 2);
    XER_VE_CHECK(bank->create_calls == 1);

    // Identical list -> no-op.
    chain.Sync(fx);
    chain.ProcessInterleaved(buf.data(), 2);
    XER_VE_CHECK(bank->create_calls == 1);

    // Param change only -> rebuild.
    fx[0].params[kGainParam] = 0.5f;
    chain.Sync(fx);
    buf = MakeStereoBlock(2, 1.0f);
    chain.ProcessInterleaved(buf.data(), 2);
    XER_VE_CHECK(bank->create_calls == 2);
    XER_VE_CHECK(std::fabs(buf[0] - 0.5f) < 1e-6f);

    // Enabled-flag change -> rebuild.
    fx[0].enabled = false;
    chain.Sync(fx);
    buf = MakeStereoBlock(2, 1.0f);
    chain.ProcessInterleaved(buf.data(), 2);
    XER_VE_CHECK(bank->create_calls == 2);
    XER_VE_CHECK(std::fabs(buf[0] - 1.0f) < 1e-6f);

    // Signature sanity: name does NOT affect it.
    auto s1 = Vst3Chain::Signature({MakeFx("a.vst3", "X")});
    auto s2 = Vst3Chain::Signature({MakeFx("a.vst3", "Y")});
    XER_VE_CHECK(s1 == s2);
    auto s3 = Vst3Chain::Signature({MakeFx("b.vst3", "X")});
    XER_VE_CHECK(s1 != s3);
}

// Runtime process failure -> warn once + bypass thereafter.
void TestChainProcessFailure() {
    auto bank = std::make_shared<FakePluginBank>();
    bank->specs["flaky.vst3"] = {.name = "flaky", .gain = 2.0f,
                                .fail_after = 2};   // 2nd call fails
    auto factory = MakePluginFactory(bank);

    std::vector<std::string> warnings;
    Vst3Chain chain(
        48000.0, 1024, factory,
        [&warnings](const std::string& m) { warnings.push_back(m); });
    chain.Sync({MakeFx("flaky.vst3", "Flaky")});

    auto buf = MakeStereoBlock(2, 0.5f);
    chain.ProcessInterleaved(buf.data(), 2);   // ok: *2 -> 1.0
    XER_VE_CHECK(std::fabs(buf[0] - 1.0f) < 1e-6f);
    chain.ProcessInterleaved(buf.data(), 2);   // fails -> bypass
    XER_VE_CHECK(chain.NodeBypassed(0));
    XER_VE_CHECK(warnings.size() == 1);
    buf = MakeStereoBlock(2, 0.5f);
    chain.ProcessInterleaved(buf.data(), 2);   // bypassed: untouched
    XER_VE_CHECK(std::fabs(buf[0] - 0.5f) < 1e-6f);
    XER_VE_CHECK(warnings.size() == 1);        // still once
}

// Empty chain + null-buffer robustness.
void TestChainEdgeCases() {
    Vst3Chain chain(48000.0, 1024, nullptr);
    auto buf = MakeStereoBlock(2, 0.5f);
    chain.ProcessInterleaved(buf.data(), 2);         // no nodes: noop
    XER_VE_CHECK(std::fabs(buf[0] - 0.5f) < 1e-6f);
    chain.ProcessInterleaved(nullptr, 2);            // null: ignored
    chain.ProcessInterleaved(buf.data(), 0);         // zero: ignored

    // Null factory -> nodes bypass with a warning.
    std::vector<std::string> warnings;
    Vst3Chain dead(48000.0, 1024, nullptr,
                   [&warnings](const std::string& m) {
                       warnings.push_back(m);
                   });
    dead.Sync({MakeFx("x.vst3", "X")});
    dead.ProcessInterleaved(buf.data(), 2);
    XER_VE_CHECK(warnings.size() == 1);
    XER_VE_CHECK(dead.NodeBypassed(0));
    XER_VE_CHECK(std::fabs(buf[0] - 0.5f) < 1e-6f);
}

// ------------------------------------------------------------------
// Compact fake decoder bank — constant-DC stereo s16 blocks so the
// mixer has real audio to push through chains.
// ------------------------------------------------------------------

struct DcSpec {
    double duration = 1.0;
    double dc = 0.5;
    int sample_rate = 48000;
    int block_frames = 2048;
};

class FakeDecoder final : public IMediaDecoder {
public:
    FakeDecoder(std::shared_ptr<std::map<std::string, DcSpec>> bank,
                std::string* last_path)
        : bank_(std::move(bank)), last_path_(last_path) {}

    bool Open(const std::string& utf8_path,
              std::string* error_out) override {
        auto it = bank_->find(utf8_path);
        if (it == bank_->end()) {
            if (error_out != nullptr) {
                *error_out = "fake: no spec for " + utf8_path;
            }
            return false;
        }
        spec_ = &it->second;
        *last_path_ = utf8_path;
        open_ = true;
        emitted_ = 0;
        total_ = static_cast<std::int64_t>(
            spec_->duration * spec_->sample_rate);
        return true;
    }
    void Close() override { open_ = false; }
    bool IsOpen() const override { return open_; }
    bool NextFrame(std::vector<std::uint8_t>* /*bgra*/, int* /*w*/,
                   int* /*h*/) override {
        return false;   // audio-only: pump is implicit in ReadAudioFrames
    }
    bool Seek(double seconds) override {
        emitted_ = std::clamp<std::int64_t>(
            static_cast<std::int64_t>(seconds * spec_->sample_rate), 0,
            total_);
        return true;
    }
    double GetPositionSeconds() const override { return -1.0; }
    double GetFps() const override { return 30.0; }
    double GetDurationSeconds() const override {
        return spec_ != nullptr ? spec_->duration : 0.0;
    }
    int GetWidth() const override { return 0; }
    int GetHeight() const override { return 0; }
    void EnableAudioTap(bool) override {}
    bool HasAudio() const override { return true; }
    double GetAudioStartSeconds() const override { return 0.0; }
    double GetAudioTimeBase() const override {
        return spec_ != nullptr ? 1.0 / spec_->sample_rate : 0.0;
    }
    bool ReadAudioFrames(
        std::vector<FFmpeg::AudioFrameInfo>* out,
        int max_frames) override {
        if (!open_ || out == nullptr || spec_ == nullptr ||
            emitted_ >= total_) {
            return false;
        }
        const int limit = max_frames > 0 ? max_frames : 1;
        for (int i = 0; i < limit && emitted_ < total_; ++i) {
            const int frames = static_cast<int>(std::min<std::int64_t>(
                spec_->block_frames, total_ - emitted_));
            FFmpeg::AudioFrameInfo frame;
            frame.sample_rate = spec_->sample_rate;
            frame.channels = 2;
            frame.planar = false;
            frame.sample_format = "s16";
            frame.sample_count = frames;
            frame.data.assign(static_cast<std::size_t>(frames) * 4u, 0);
            const std::int16_t v = static_cast<std::int16_t>(
                std::clamp(spec_->dc, -1.0, 1.0) * 32767.0);
            auto* pcm = reinterpret_cast<std::int16_t*>(
                frame.data.data());
            for (int s = 0; s < frames * 2; ++s) {
                pcm[s] = v;
            }
            frame.pts = emitted_;
            frame.best_effort_timestamp = kNoPts;
            out->push_back(std::move(frame));
            emitted_ += frames;
        }
        return true;
    }
    bool AudioDrained() const override {
        return spec_ != nullptr && emitted_ >= total_;
    }
    std::uint64_t GetAudioEpoch() const override { return 1; }

private:
    std::shared_ptr<std::map<std::string, DcSpec>> bank_;
    std::string* last_path_;
    const DcSpec* spec_ = nullptr;
    bool open_ = false;
    std::int64_t emitted_ = 0;
    std::int64_t total_ = 0;
};

struct MixerFixture {
    std::shared_ptr<std::map<std::string, DcSpec>> bank =
        std::make_shared<std::map<std::string, DcSpec>>();
    std::shared_ptr<FakePluginBank> plugins =
        std::make_shared<FakePluginBank>();
    std::string last_path;

    AudioMixerOptions Options() {
        AudioMixerOptions options;
        auto specs = bank;
        auto* last = &last_path;
        options.decoder_factory = [specs, last]() -> MediaDecoderPtr {
            return std::make_unique<FakeDecoder>(specs, last);
        };
        options.plugin_factory = MakePluginFactory(plugins);
        return options;
    }
};

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
              double duration, const std::string& source) {
    Clip c;
    c.id = id;
    c.source_path = source;
    c.track = track;
    c.timeline_start = start;
    c.duration = duration;
    return c;
}

double ChunkPeak(const FFmpeg::AudioFrameInfo& chunk) {
    double peak = 0.0;
    const auto* pcm =
        reinterpret_cast<const std::int16_t*>(chunk.data.data());
    for (std::size_t i = 0; i < chunk.data.size() / 2; ++i) {
        peak = std::max(peak, std::abs(static_cast<double>(pcm[i])));
    }
    return peak;
}

// Clip with a gain-0.5 fake plugin halves the rendered level vs the
// same clip without fx.
void TestMixerPluginApplied() {
    MixerFixture fx_fixture;
    fx_fixture.bank->emplace("dc.wav", DcSpec{1.0, 0.5});
    fx_fixture.plugins->specs["half.vst3"] = {.name = "half",
                                              .gain = 0.5f};

    Project p = MakeProject();
    Clip c = MakeClip("c1", 0, 0.0, 1.0, "dc.wav");
    c.audio_fx.push_back(MakeFx("half.vst3", "Half"));
    p.clips.push_back(c);

    AudioMixer mixer;
    std::string err;
    XER_VE_CHECK(mixer.Init(p, fx_fixture.Options(), &err));
    FFmpeg::AudioFrameInfo chunk;
    XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
    const double with_fx = ChunkPeak(chunk);
    // 0.5 DC * 0.5 gain ~= 0.25 * 32767 ~ 8192.
    XER_VE_CHECK(with_fx > 7000.0 && with_fx < 9500.0);
    XER_VE_CHECK(fx_fixture.plugins->create_calls == 1);
    mixer.Shutdown();

    // Control: same clip, no fx -> ~16384.
    MixerFixture plain;
    plain.bank->emplace("dc.wav", DcSpec{1.0, 0.5});
    Project p2 = MakeProject();
    p2.clips.push_back(MakeClip("c1", 0, 0.0, 1.0, "dc.wav"));
    AudioMixer mixer2;
    XER_VE_CHECK(mixer2.Init(p2, plain.Options(), &err));
    XER_VE_CHECK(mixer2.GetChunk(23, &chunk, &err));
    const double no_fx = ChunkPeak(chunk);
    XER_VE_CHECK(no_fx > 15000.0 && no_fx < 18000.0);
    XER_VE_CHECK(with_fx < no_fx * 0.6);
}

// Missing plugin -> clip audio passes through unmodified + one
// warning; the mix does not fail.
void TestMixerMissingPlugin() {
    MixerFixture fixture;
    fixture.bank->emplace("dc.wav", DcSpec{1.0, 0.5});
    fixture.plugins->missing.insert("gone.vst3");

    Project p = MakeProject();
    Clip c = MakeClip("c1", 0, 0.0, 1.0, "dc.wav");
    c.audio_fx.push_back(MakeFx("gone.vst3", "Gone"));
    p.clips.push_back(c);

    AudioMixer mixer;
    std::string err;
    XER_VE_CHECK(mixer.Init(p, fixture.Options(), &err));
    FFmpeg::AudioFrameInfo chunk;
    XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
    const double peak = ChunkPeak(chunk);
    XER_VE_CHECK(peak > 15000.0 && peak < 18000.0);   // full level
    // A later chunk still renders and doesn't re-warn.
    XER_VE_CHECK(mixer.GetChunk(24, &chunk, &err));
    XER_VE_CHECK(ChunkPeak(chunk) > 15000.0);
    bool found = false;
    for (const std::string& w : mixer.Warnings()) {
        if (w.find("Gone") != std::string::npos ||
            w.find("gone.vst3") != std::string::npos) {
            found = true;
        }
    }
    XER_VE_CHECK(found);
}

// Disabled fx node -> passthrough, no instantiation.
void TestMixerDisabledFx() {
    MixerFixture fixture;
    fixture.bank->emplace("dc.wav", DcSpec{1.0, 0.5});
    fixture.plugins->specs["x.vst3"] = {.name = "mute", .gain = 0.0f};

    Project p = MakeProject();
    Clip c = MakeClip("c1", 0, 0.0, 1.0, "dc.wav");
    c.audio_fx.push_back(MakeFx("x.vst3", "Mute", /*enabled=*/false));
    p.clips.push_back(c);

    AudioMixer mixer;
    std::string err;
    XER_VE_CHECK(mixer.Init(p, fixture.Options(), &err));
    FFmpeg::AudioFrameInfo chunk;
    XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
    XER_VE_CHECK(ChunkPeak(chunk) > 15000.0);   // unattenuated
    XER_VE_CHECK(fixture.plugins->create_calls == 0);
}

// Two clips; only the fx'd one is attenuated (gain 0) — the other
// still contributes its level.
void TestMixerTwoClipsOneFx() {
    MixerFixture fixture;
    fixture.bank->emplace("a.wav", DcSpec{1.0, 0.4});
    fixture.bank->emplace("b.wav", DcSpec{1.0, 0.4});
    fixture.plugins->specs["kill.vst3"] = {.name = "kill",
                                           .gain = 0.0f};

    Project p = MakeProject();
    Clip a = MakeClip("a", 1, 0.0, 1.0, "a.wav");   // audio track
    a.audio_fx.push_back(MakeFx("kill.vst3", "Kill"));
    p.clips.push_back(a);
    p.clips.push_back(MakeClip("b", 1, 0.0, 1.0, "b.wav"));

    AudioMixer mixer;
    std::string err;
    XER_VE_CHECK(mixer.Init(p, fixture.Options(), &err));
    FFmpeg::AudioFrameInfo chunk;
    XER_VE_CHECK(mixer.GetChunk(23, &chunk, &err));
    const double peak = ChunkPeak(chunk);
    // b alone at 0.4 -> ~13107; a is killed by its plugin.
    XER_VE_CHECK(peak > 11000.0 && peak < 15000.0);
}

// ------------------------------------------------------------------
// Editor-session helpers (vst/fx_editor_utils.h): the pure math the
// popup window and the param commit-back path rely on.
// ------------------------------------------------------------------

// Only params that deviate from their default get persisted — a
// reverted tweak drops out instead of being frozen at the default.
void TestPersistableParamEdits() {
    using Vst::EditorParamSnapshot;
    const auto edits = Vst::PersistableParamEdits({
        {1, 0.5f, 0.5f},          // untouched default -> dropped
        {2, 0.8f, 0.5f},          // edited -> kept
        {3, 0.0f, 1.0f},          // edited to 0 -> kept (differs)
        {4, 0.5000004f, 0.5f},    // within eps -> dropped
    });
    XER_VE_CHECK(edits.size() == 2);
    XER_VE_CHECK(edits.count(2) == 1 &&
                 std::fabs(edits.at(2) - 0.8f) < 1e-7f);
    XER_VE_CHECK(edits.count(3) == 1 &&
                 std::fabs(edits.at(3)) < 1e-7f);
    XER_VE_CHECK(Vst::PersistableParamEdits({}).empty());
}

// Preferred plugin size wins when sane; broken reports fall back;
// both paths clamp into the safe range.
void TestPickEditorClientSize() {
    auto s = Vst::PickEditorClientSize(640, 400, 720, 480);
    XER_VE_CHECK(s.width == 640 && s.height == 400);
    // Plugin reports nothing usable -> fallback.
    s = Vst::PickEditorClientSize(0, 0, 720, 480);
    XER_VE_CHECK(s.width == 720 && s.height == 480);
    s = Vst::PickEditorClientSize(-50, 20, 800, 600);
    XER_VE_CHECK(s.width == 800 && s.height == 600);
    // One axis insane -> whole report distrusted (not per-axis mixed).
    s = Vst::PickEditorClientSize(640, 99999, 700, 500);
    XER_VE_CHECK(s.width == 700 && s.height == 500);
    // Fallback itself is clamped.
    s = Vst::PickEditorClientSize(0, 0, 1, 99999);
    XER_VE_CHECK(s.width == 64 && s.height == 8192);
}

// Popup centers on the parent but stays inside the work area.
void TestCenterPopupOnParent() {
    // Room to spare: centered exactly.
    auto p = Vst::CenterPopupOnParent(100, 100, 1100, 700, 400, 300,
                                      0, 0, 1920, 1080);
    XER_VE_CHECK(p.left == 400 && p.top == 250);
    // Parent near the right edge: nudged back inside.
    p = Vst::CenterPopupOnParent(1700, 850, 1900, 950, 400, 300,
                                 0, 0, 1920, 1080);
    XER_VE_CHECK(p.left == 1520 && p.top == 750);
    // Window larger than the work area pins to the top-left.
    p = Vst::CenterPopupOnParent(0, 0, 500, 400, 4000, 3000,
                                 0, 0, 1920, 1080);
    XER_VE_CHECK(p.left == 0 && p.top == 0);
    // Nonzero work-area origin (taskbar left/top).
    p = Vst::CenterPopupOnParent(60, 60, 260, 160, 400, 300,
                                 100, 40, 2020, 1120);
    XER_VE_CHECK(p.left == 100 && p.top == 40);
}

}  // namespace

int XER_VideoEditor_RunVst3Tests() {
    TestChainOrdering();
    TestChainBypassOnMissing();
    TestChainDisabledNode();
    TestChainParams();
    TestChainSyncRebuild();
    TestChainProcessFailure();
    TestChainEdgeCases();
    TestMixerPluginApplied();
    TestMixerMissingPlugin();
    TestMixerDisabledFx();
    TestMixerTwoClipsOneFx();
    TestPersistableParamEdits();
    TestPickEditorClientSize();
    TestCenterPopupOnParent();
    return XER::VideoEditor::Test::Failures();
}
