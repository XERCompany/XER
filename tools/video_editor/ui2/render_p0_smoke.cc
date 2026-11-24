// render_p0 smoke — see render_p0_smoke.h.
#include "render_p0_smoke.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "compositor.h"          // ClipCostClass / ClipCostClassFor
#include "media_decoder.h"       // IMediaDecoder / MediaDecoderFactory
#include "monitor_panel.h"
#include "playback/preview_engine.h"
#include "preview_binding.h"
#include "rendering/renderer.h"
#include "timeline_surface.h"
#include "timeline_model.h"
#include "ve_theme.h"
#include "widgets/widget_tree.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace Layout = XER::UI::Layout;
namespace W = XER::UI::Widgets;
using XER::UI::Rendering::Renderer;
using XER::UI::Rendering::TextStyle;
using XER::UI::Theme::Color;
using XER::UI::Theme::Tokens;

// ---- a decoder that costs real time -------------------------------
// Every NextFrame sleeps `ms_per_frame` — decode-bound frames are the
// honest case for both the dropped-frame counter (the wall-clock
// playhead runs ahead) and the adaptive ladder (measured render ms
// exceeds the frame budget).
class SlowDecoder final : public IMediaDecoder {
public:
    SlowDecoder(int w, int h, double fps, double ms_per_frame)
        : w_(w), h_(h), fps_(fps), ms_per_frame_(ms_per_frame) {}
    bool Open(const std::string& utf8_path,
              std::string* /*error_out*/) override {
        opened_path_ = utf8_path;
        pos_ = 0.0;
        return true;
    }
    void Close() override {}
    bool IsOpen() const override { return !opened_path_.empty(); }
    bool NextFrame(std::vector<std::uint8_t>* bgra, int* width,
                   int* height) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(
            static_cast<long long>(ms_per_frame_)));
        bgra->assign(static_cast<std::size_t>(w_) * h_ * 4u, 0x66);
        *width = w_;
        *height = h_;
        pos_ += 1.0 / fps_;
        return true;
    }
    bool Seek(double seconds) override {
        pos_ = std::max(0.0, seconds);
        return true;
    }
    double GetPositionSeconds() const override { return pos_; }
    double GetFps() const override { return fps_; }
    double GetDurationSeconds() const override { return 60.0; }
    int GetWidth() const override { return w_; }
    int GetHeight() const override { return h_; }
    void EnableAudioTap(bool) override {}
    bool HasAudio() const override { return false; }
    double GetAudioStartSeconds() const override { return 0.0; }
    double GetAudioTimeBase() const override { return 0.0; }
    bool ReadAudioFrames(
        std::vector<XER::Bridge::FFmpeg::AudioFrameInfo>* /*out*/,
        int /*max_frames*/) override {
        return false;
    }
    bool AudioDrained() const override { return true; }
    std::uint64_t GetAudioEpoch() const override { return 0; }

private:
    int w_;
    int h_;
    double fps_;
    double ms_per_frame_;
    std::string opened_path_;
    double pos_ = 0.0;
};

struct DimSink final : PreviewEngine::Sink {
    void OnVideoFrame(const video::Frame& f) override {
        ++frames;
        last_w = f.Width();
        last_h = f.Height();
    }
    void OnAudioPeakLevels(float, float) override {}
    void OnPlayhead(double) override {}
    std::atomic<int> frames{0};
    std::atomic<int> last_w{0};
    std::atomic<int> last_h{0};
};

// Poll helper — waits until `pred` or the deadline.
bool WaitFor(const std::function<bool()>& pred, int timeout_ms) {
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);
    while (!pred() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

// ---- T2: static cost classification -------------------------------

std::string CheckCostClasses() {
    const auto cls = [](std::initializer_list<const char*> types,
                        bool adjustment = false) {
        Clip c;
        c.is_adjustment = adjustment;
        for (const char* t : types) {
            c.effects.push_back({t, {1.0f}});
        }
        return ClipCostClassFor(c);
    };
    using C = ClipCostClass;
    if (cls({}) != C::kRealtime) {
        return "fx-free clip is not kRealtime";
    }
    if (cls({"brightness_contrast"}) != C::kRealtime) {
        return "cheap named kernel classified heavy";
    }
    if (cls({"builtin:xer.fx.lift_gamma_gain"}) != C::kRealtime) {
        return "allowlisted builtin classified heavy";
    }
    if (cls({"builtin:xer.fx.exposure",
             "builtin:xer.fx.temperature"}) != C::kRealtime) {
        return "all-cheap builtin chain classified heavy";
    }
    if (cls({"gaussian_blur"}) != C::kNeedsRender) {
        return "gaussian_blur not kNeedsRender";
    }
    if (cls({"builtin:xer.fx.fast_gaussian"}) != C::kNeedsRender) {
        return "fast_gaussian builtin not kNeedsRender";
    }
    if (cls({"builtin:xer.fx.track_mosaic"}) != C::kNeedsRender) {
        return "track-driven builtin not kNeedsRender";
    }
    if (cls({"builtin:xer.fx.twirl"}) != C::kNeedsRender) {
        return "warp builtin not kNeedsRender";
    }
    if (cls({"script:invert"}) != C::kNeedsRender) {
        return "script: chain not kNeedsRender";
    }
    if (cls({"builtin:xer.fx.exposure", "gaussian_blur"}) !=
        C::kNeedsRender) {
        return "one heavy effect did not reclassify the chain";
    }
    if (cls({}, /*adjustment=*/true) != C::kNeedsRender) {
        return "adjustment clip not kNeedsRender";
    }
    return {};
}

// ---- T2: the strip on the timeline surface -------------------------

// FillRect-recording renderer — the strip asserts by color.
class StripRenderer final : public Renderer {
public:
    std::vector<Color> fills;
    void BeginFrame(Layout::Rect) override {}
    void EndFrame() override {}
    void FillRect(Layout::Rect, Color color, float) override {
        fills.push_back(color);
    }
    bool DrewFill(Color c) const {
        return std::find_if(fills.begin(), fills.end(), [&](Color f) {
                   return f.r == c.r && f.g == c.g && f.b == c.b;
               }) != fills.end();
    }
    void StrokeRect(Layout::Rect, Color, float, float) override {}
    void DrawLine(Layout::Point, Layout::Point, Color,
                  float) override {}
    void DrawText(std::string_view, Layout::Rect, TextStyle) override {}
    Layout::Size MeasureText(std::string_view text,
                             TextStyle) const override {
        return {static_cast<float>(text.size()) * 7.0f, 14.0f};
    }
    bool DrawFrameBgra(const void*, int, int, Layout::Rect,
                       float) override {
        return true;
    }
};

std::string CheckStatusStrip() {
    Project p;
    p.width = 640;
    p.height = 360;
    p.fps_num = 30;
    p.fps_den = 1;
    Track v;
    v.kind = TrackKind::Video;
    p.tracks.push_back(v);
    Clip plain;
    plain.id = "plain";
    plain.track = 0;
    plain.duration = 4.0;
    plain.source_path = "a.mp4";
    Clip cached;
    cached.id = "cached";
    cached.track = 0;
    cached.timeline_start = 4.0;
    cached.duration = 4.0;
    cached.source_path = "b.mp4";
    cached.effects.push_back({"gaussian_blur", {2.0f}});
    Clip heavy;
    heavy.id = "heavy";
    heavy.track = 0;
    heavy.timeline_start = 8.0;
    heavy.duration = 4.0;
    heavy.source_path = "c.mp4";
    heavy.effects.push_back({"gaussian_blur", {2.0f}});
    p.clips = {plain, cached, heavy};

    TimelineHooks hooks;
    // The shell-side mapping: cache coverage upgrades to kCached,
    // else the static classifier — faked here by id.
    hooks.get_clip_cost_class = [](const Clip& c) {
        if (c.id == "cached") {
            return static_cast<int>(ClipCostClass::kCached);
        }
        return static_cast<int>(ClipCostClassFor(c));
    };
    auto surface = std::make_shared<TimelineSurface>(hooks);
    W::WidgetTree tree;
    tree.SetRoot(surface);
    const auto tokens = Theme::DarkTokens();
    tree.Layout({0.0f, 0.0f, 1200.0f, 300.0f}, tokens);
    surface->SetProject(&p);

    if (surface->CostClassOf("plain") != 0 ||
        surface->CostClassOf("cached") != 1 ||
        surface->CostClassOf("heavy") != 2) {
        return "cached cost classes wrong";
    }
    StripRenderer r;
    tree.Render(r, tokens);
    if (!r.DrewFill(tokens.status_ok) || !r.DrewFill(tokens.status_warn) ||
        !r.DrewFill(tokens.status_bad)) {
        return "status strip did not paint all three tiers";
    }
    // Unwired hook -> no strip at all.
    auto bare = std::make_shared<TimelineSurface>(TimelineHooks{});
    tree.SetRoot(bare);
    tree.Layout({0.0f, 0.0f, 1200.0f, 300.0f}, tokens);
    bare->SetProject(&p);
    StripRenderer r2;
    tree.Render(r2, tokens);
    if (r2.DrewFill(tokens.status_ok)) {
        return "strip painted without a bound hook";
    }
    tree.SetRoot(nullptr);
    return {};
}

// ---- T1 + T3 + T4: the engine ---------------------------------

std::string CheckEngine() {
    constexpr int kW = 320;
    constexpr int kH = 180;
    constexpr int kFps = 30;
    // ~1.4x the 33.3 ms frame budget — real enough for both the
    // drop counter and the adaptive EMA, cheap enough for the 15 s
    // watchdog (~20 presented frames per wall second).
    constexpr double kSlowMs = 45.0;

    Project p;
    p.width = kW;
    p.height = kH;
    p.fps_num = kFps;
    p.fps_den = 1;
    Track v;
    v.kind = TrackKind::Video;
    p.tracks.push_back(v);
    Clip clip;
    clip.id = "slow.mp4";
    clip.track = 0;
    clip.duration = 30.0;
    clip.source.kind = SourceKind::Media;
    clip.source_path = "slow.mp4";
    p.clips.push_back(clip);

    auto factory = MediaDecoderFactory{[] {
        return MediaDecoderPtr(new SlowDecoder(kW, kH, kFps, kSlowMs));
    }};

    DimSink sink;
    PreviewEngine engine;
    PreviewOptions po;
    po.decoder_factory = factory;
    po.audio_enabled = false;
    po.autocache = false;   // isolate the playback path — an
                          // autocacher would land coverage and
                          // erase the drops under test
    std::string err;
    if (!engine.Init(p, po, &sink, &err)) {
        return "engine init failed: " + err;
    }

    // T3 playing half: pick the 1/2 tier and play — delivered
    // frames must not be full-canvas (1/2, or deeper if the ladder
    // already stepped on a starved box).
    engine.SetQuality(0.5f);
    engine.SetPlaying(true);
    if (!WaitFor([&] { return sink.frames.load() >= 4; }, 10000)) {
        return "playback stalled — no frames presented";
    }
    if (sink.last_w.load() > kW / 2 || sink.last_h.load() > kH / 2) {
        return "playing frame not at the 1/2 tier: got " +
               std::to_string(sink.last_w.load()) + "x" +
               std::to_string(sink.last_h.load());
    }

    // T1: the 45 ms/frame render can't hold a 33 ms frame budget —
    // the wall-clock playhead must skip indices. ~10 presented
    // frames is ~450 ms of play: expect at least a few drops.
    if (!WaitFor([&] { return engine.GetFrameStats().dropped >= 1; },
                 8000)) {
        return "dropped-frame counter stayed at 0 under overload";
    }
    const PreviewEngine::FrameStats s1 = engine.GetFrameStats();
    if (s1.last_render_ms < kSlowMs * 0.8) {
        return "last_render_ms too low: " +
               std::to_string(s1.last_render_ms);
    }
    if (s1.effective_scale > 0.51f) {
        return "effective scale not reduced while playing";
    }

    // T4: sustained overload must step the adaptive ladder. First
    // window fires at ~500 ms of play; allow three windows.
    if (!WaitFor(
            [&] { return engine.GetFrameStats().adaptive_level >= 1; },
            8000)) {
        return "adaptive ladder did not step down under overload";
    }
    const PreviewEngine::FrameStats s2 = engine.GetFrameStats();
    if (s2.effective_scale >= 0.5f) {
        return "adaptive step did not reduce the effective tier";
    }

    // T3 pause half: paused presents render full-res — seek like
    // Stop() does (a bare SetPlaying(false) never wakes a blocked
    // worker; the pause seek also lands the inspection frame).
    // Wait for a FULL-RES delivery, not just frames+1: a render that
    // was in flight when pause was commanded still delivers at the
    // old adaptive tier — the paused inspection frame lands after it.
    engine.SetPlaying(false);
    engine.Seek(2.0);
    if (!WaitFor([&] {
            return sink.last_w.load() == kW && sink.last_h.load() == kH;
        }, 8000)) {
        return "paused frame not full-res: got " +
               std::to_string(sink.last_w.load()) + "x" +
               std::to_string(sink.last_h.load());
    }
    if (engine.GetFrameStats().adaptive_level != 0) {
        return "adaptive ladder did not restore on pause";
    }
    engine.Shutdown();
    return {};
}

// ---- T1 badge: the monitor stoplight -----------------------------

std::string CheckDropBadge() {
    const auto tokens = Theme::DarkTokens();
    auto panel = MonitorPanel::Create(MonitorPanel::Kind::Program);
    W::WidgetTree tree;
    tree.SetRoot(panel);
    tree.Layout({0.0f, 0.0f, 720.0f, 480.0f}, tokens);

    if (panel->Chrome(MonitorPanel::ChromeId::DropLight) == nullptr) {
        return "drop light chrome missing";
    }
    panel->SetDropStats(/*dropped=*/0, /*rendered=*/30,
                        /*last_render_ms=*/12.0, /*adaptive_level=*/0,
                        /*effective_scale=*/1.0f);
    if (panel->DropLevel() != 0) {
        return "no-drop state is not green";
    }
    // ~5% recent drop rate -> amber.
    panel->SetDropStats(2, 68, 12.0, 0, 1.0f);
    if (panel->DropLevel() != 1) {
        return "light drop rate is not amber";
    }
    // ~50% recent drop rate -> red.
    panel->SetDropStats(22, 88, 30.0, 0, 1.0f);
    if (panel->DropLevel() != 2) {
        return "heavy drop rate is not red";
    }
    const std::string& tip =
        panel->Chrome(MonitorPanel::ChromeId::DropLight)->GetTooltip();
    if (tip.find("22") == std::string::npos) {
        return "drop tooltip lacks the count: " + tip;
    }
    // Adaptive badge shows while the ladder is active.
    panel->SetDropStats(22, 100, 30.0, /*adaptive_level=*/1, 0.25f);
    if (!panel->AdaptiveBadgeVisible()) {
        return "adaptive badge hidden while the ladder is active";
    }
    panel->SetDropStats(22, 100, 30.0, 0, 1.0f);
    if (panel->AdaptiveBadgeVisible()) {
        return "adaptive badge stayed after the ladder restored";
    }
    tree.SetRoot(nullptr);
    return {};
}

}  // namespace

std::string RenderP0Smoke() {
    if (const std::string e = CheckCostClasses(); !e.empty()) {
        return e;
    }
    if (const std::string e = CheckStatusStrip(); !e.empty()) {
        return e;
    }
    if (const std::string e = CheckDropBadge(); !e.empty()) {
        return e;
    }
    return CheckEngine();
}

}  // namespace XER::VideoEditor::Ui2
