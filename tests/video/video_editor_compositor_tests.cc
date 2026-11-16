// Compositor suite — Phase-1C frame-accurate timeline -> canvas BGRA
// renderer. Pure-helper cases (TransitionWeightAt / WarpClipFrame /
// ApplyTransitionPixels) need no decoder; compositor cases run against
// a hand-rolled FakeDecoder bank (scripted BGRA frames at scripted
// pts, per-path specs, open/seek/decode counters) so the whole
// composition logic is covered WITHOUT FFmpeg. One FFmpeg-gated
// integration case self-skips via XER::Bridge::FFmpeg::IsAvailable().
// Expected pixel values are computed from the documented math in
// compositor.h — NOT observed from the implementation.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "video_editor_test_util.h"
#include "compositor.h"
#include "helper/path_utils.h"
#include "media_decoder.h"
#include "timeline_model.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {
namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers
namespace FFmpeg = XER::Bridge::FFmpeg;
using XER::VideoEditor::Clip;
using XER::VideoEditor::ClipEffect;
using XER::VideoEditor::Compositor;
using XER::VideoEditor::CompositorOptions;
using XER::VideoEditor::IMediaDecoder;
using XER::VideoEditor::MediaDecoderFactory;
using XER::VideoEditor::MediaDecoderPtr;
using XER::VideoEditor::Project;
using XER::VideoEditor::Track;
using XER::VideoEditor::TrackKind;
using XER::VideoEditor::Transform;
using XER::VideoEditor::Transition;

// ------------------------------------------------------------------
// Fake decoder bank — per-path scripted specs shared by every decoder
// the factory produces. Open()/Seek()/NextFrame() calls are counted
// per path so laziness/sequential-decode behavior is observable.
// ------------------------------------------------------------------

struct FakeFrame {
    std::vector<std::uint8_t> pixels;  // packed BGRA8, w*h*4
    double pts = 0.0;                  // <0 -> decoder reports -1
};

struct FakeSpec {
    int width = 16;
    int height = 16;
    double fps = 30.0;
    double duration = 0.0;    // GetDurationSeconds (0 = unknown)
    bool audio_only = false;  // GetWidth/GetHeight report 0
    bool seek_ok = true;
    std::vector<FakeFrame> frames;
};

struct FakeBank {
    std::map<std::string, FakeSpec> specs;
    std::map<std::string, int> open_counts;
    std::map<std::string, int> seek_calls;
    std::map<std::string, int> decode_calls;

    int Opens(const std::string& path) const {
        const auto it = open_counts.find(path);
        return it == open_counts.end() ? 0 : it->second;
    }
    int Seeks(const std::string& path) const {
        const auto it = seek_calls.find(path);
        return it == seek_calls.end() ? 0 : it->second;
    }
    int Decodes(const std::string& path) const {
        const auto it = decode_calls.find(path);
        return it == decode_calls.end() ? 0 : it->second;
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
        index_ = 0;
        position_ = -1.0;   // bridge convention: -1 before first frame
        ++epoch_;
        return true;
    }

    void Close() override {
        open_ = false;
        index_ = 0;
        position_ = -1.0;
    }

    bool IsOpen() const override { return open_; }

    bool NextFrame(std::vector<std::uint8_t>* bgra, int* width,
                   int* height) override {
        if (!open_ || bgra == nullptr || width == nullptr ||
            height == nullptr || spec_ == nullptr ||
            spec_->audio_only || index_ >= spec_->frames.size()) {
            return false;
        }
        const FakeFrame& f = spec_->frames[index_++];
        *bgra = f.pixels;
        *width = spec_->width;
        *height = spec_->height;
        position_ = f.pts;   // may be -1 -> compositor synthesizes
        ++bank_->decode_calls[open_path_];
        return true;
    }

    bool Seek(double seconds) override {
        if (!open_ || spec_ == nullptr || !spec_->seek_ok) {
            return false;
        }
        // Mirrors the bridge contract: position presets to the target
        // until the next frame lands; decode restarts from the top.
        position_ = seconds;
        index_ = 0;
        ++epoch_;
        ++bank_->seek_calls[open_path_];
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
        return (spec_ != nullptr && !spec_->audio_only) ? spec_->width
                                                      : 0;
    }
    int GetHeight() const override {
        return (spec_ != nullptr && !spec_->audio_only) ? spec_->height
                                                      : 0;
    }

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
    std::shared_ptr<FakeBank> bank_;
    const FakeSpec* spec_ = nullptr;
    bool open_ = false;
    std::string open_path_;
    std::size_t index_ = 0;
    double position_ = -1.0;
    std::uint64_t epoch_ = 0;
};

MediaDecoderFactory MakeFakeFactory(std::shared_ptr<FakeBank> bank) {
    return [bank]() -> MediaDecoderPtr {
        return std::make_unique<FakeDecoder>(bank);
    };
}

// ------------------------------------------------------------------
// Pixel / spec helpers
// ------------------------------------------------------------------

using Color = std::array<std::uint8_t, 4>;  // {B,G,R,A}

FakeFrame MakeFramePixels(int w, int h, const Color& c, double pts) {
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

// Vertical two-tone frame: columns [0, split) = left, [split, w) =
// right — used by the slide-transition offset asserts.
FakeFrame MakeSplitFrame(int w, int h, int split, const Color& left,
                         const Color& right, double pts) {
    FakeFrame f;
    f.pts = pts;
    f.pixels.assign(static_cast<std::size_t>(w) * h * 4u, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color& c = x < split ? left : right;
            const std::size_t i =
                (static_cast<std::size_t>(y) * w + x) * 4u;
            f.pixels[i + 0] = c[0];
            f.pixels[i + 1] = c[1];
            f.pixels[i + 2] = c[2];
            f.pixels[i + 3] = c[3];
        }
    }
    return f;
}

// Solid-color spec: one frame per pts entry, all the same color.
FakeSpec MakeSolidSpec(int w, int h, double fps,
                       const std::vector<double>& pts, const Color& c,
                       double duration = 0.0) {
    FakeSpec s;
    s.width = w;
    s.height = h;
    s.fps = fps;
    s.duration = duration;
    for (double p : pts) {
        s.frames.push_back(MakeFramePixels(w, h, c, p));
    }
    return s;
}

FakeSpec MakeSolidSpecAuto(int w, int h, double fps, int count,
                           const Color& c, double duration = 0.0) {
    std::vector<double> pts;
    pts.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        pts.push_back(static_cast<double>(i) / fps);
    }
    return MakeSolidSpec(w, h, fps, pts, c, duration);
}

// Two-tone spec for slide tests (same pattern every frame).
FakeSpec MakeSplitSpec(int w, int h, double fps,
                       const std::vector<double>& pts, int split,
                       const Color& left, const Color& right) {
    FakeSpec s;
    s.width = w;
    s.height = h;
    s.fps = fps;
    for (double p : pts) {
        s.frames.push_back(MakeSplitFrame(w, h, split, left, right, p));
    }
    return s;
}

// Per-frame distinct R channel (R = base_r + i*step) so nearest-PTS
// selection is observable in the composited pixels.
FakeSpec MakeRampSpec(int w, int h, double fps, int count,
                      std::uint8_t base_b, std::uint8_t base_g,
                      std::uint8_t base_r, int r_step) {
    FakeSpec s;
    s.width = w;
    s.height = h;
    s.fps = fps;
    for (int i = 0; i < count; ++i) {
        const Color c{base_b, base_g,
                      static_cast<std::uint8_t>(base_r + i * r_step),
                      255};
        s.frames.push_back(
            MakeFramePixels(w, h, c, static_cast<double>(i) / fps));
    }
    return s;
}

// ------------------------------------------------------------------
// Project / clip helpers — hand-built Projects (the compositor takes
// the model as-is; tests deliberately bypass AddClip validation for
// degenerate states like overlapping clips and audio-only-on-video).
// ------------------------------------------------------------------

Project MakeCanvasProject(int w = 16, int h = 16) {
    Project p;
    p.width = w;
    p.height = h;
    p.fps_num = 30;
    p.fps_den = 1;
    p.bg_color = {0, 0, 0, 255};   // {r,g,b,a} -> BGRA (0,0,0,255)
    p.tracks.push_back(Track{TrackKind::Video, "V1"});
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

Transition MakeTransition(const std::string& from,
                          const std::string& to,
                          Transition::Type type, double duration) {
    Transition t;
    t.from_clip = from;
    t.to_clip = to;
    t.type = type;
    t.duration = duration;
    return t;
}

// ------------------------------------------------------------------
// Pixel assert helpers — one set for video::Frame, one for the
// RenderFrameAt byte vector.
// ------------------------------------------------------------------

const std::uint8_t* FramePx(const video::Frame& f, int x, int y) {
    return f.Data() +
           (static_cast<std::size_t>(y) * f.Width() + x) * 4u;
}

bool FramePxIs(const video::Frame& f, int x, int y, std::uint8_t b,
               std::uint8_t g, std::uint8_t r, std::uint8_t a) {
    const std::uint8_t* p = FramePx(f, x, y);
    return p[0] == b && p[1] == g && p[2] == r && p[3] == a;
}

bool FramePxNear(const video::Frame& f, int x, int y, std::uint8_t b,
                 std::uint8_t g, std::uint8_t r, std::uint8_t a,
                 int tol) {
    const std::uint8_t* p = FramePx(f, x, y);
    return std::abs(static_cast<int>(p[0]) - b) <= tol &&
           std::abs(static_cast<int>(p[1]) - g) <= tol &&
           std::abs(static_cast<int>(p[2]) - r) <= tol &&
           std::abs(static_cast<int>(p[3]) - a) <= tol;
}

const std::uint8_t* VecPx(const std::vector<std::uint8_t>& bgra, int w,
                          int x, int y) {
    return &bgra[(static_cast<std::size_t>(y) * w + x) * 4u];
}

bool VecPxIs(const std::vector<std::uint8_t>& bgra, int w, int x, int y,
             std::uint8_t b, std::uint8_t g, std::uint8_t r,
             std::uint8_t a) {
    const std::uint8_t* p = VecPx(bgra, w, x, y);
    return p[0] == b && p[1] == g && p[2] == r && p[3] == a;
}

bool VecPxNear(const std::vector<std::uint8_t>& bgra, int w, int x, int y,
               std::uint8_t b, std::uint8_t g, std::uint8_t r,
               std::uint8_t a, int tol) {
    const std::uint8_t* p = VecPx(bgra, w, x, y);
    return std::abs(static_cast<int>(p[0]) - b) <= tol &&
           std::abs(static_cast<int>(p[1]) - g) <= tol &&
           std::abs(static_cast<int>(p[2]) - r) <= tol &&
           std::abs(static_cast<int>(p[3]) - a) <= tol;
}

bool HasWarning(const Compositor& compositor,
                const std::string& needle) {
    for (const auto& w : compositor.Warnings()) {
        if (w.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// video::Frame builders for the pure-helper suites.
video::Frame MakeVideoFrame(int w, int h, const Color& c) {
    video::Frame f;
    XER_VE_CHECK(f.Allocate(w, h, video::PixelFormat::BGRA8));
    for (int i = 0; i < w * h; ++i) {
        f.Data()[i * 4 + 0] = c[0];
        f.Data()[i * 4 + 1] = c[1];
        f.Data()[i * 4 + 2] = c[2];
        f.Data()[i * 4 + 3] = c[3];
    }
    return f;
}

video::Frame MakeVideoSplitFrame(int w, int h, int split,
                                 const Color& left, const Color& right) {
    video::Frame f;
    XER_VE_CHECK(f.Allocate(w, h, video::PixelFormat::BGRA8));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const Color& c = x < split ? left : right;
            const std::size_t i =
                (static_cast<std::size_t>(y) * w + x) * 4u;
            f.Data()[i + 0] = c[0];
            f.Data()[i + 1] = c[1];
            f.Data()[i + 2] = c[2];
            f.Data()[i + 3] = c[3];
        }
    }
    return f;
}

// Shared palette (BGRA bytes) — distinct channels so blends are
// unambiguous.
constexpr Color kRed = {10, 20, 200, 255};
constexpr Color kGreen = {30, 200, 40, 255};
constexpr Color kBlue = {200, 40, 10, 255};
constexpr Color kWhite = {240, 240, 240, 255};
constexpr Color kBlack = {5, 5, 5, 255};
constexpr Color kBgPixel = {0, 0, 0, 255};   // project bg {0,0,0,255}
// Slide-test quadrant colors.
constexpr Color kUnderLeft = {10, 20, 200, 255};
constexpr Color kUnderRight = {200, 20, 10, 255};
constexpr Color kOverLeft = {30, 200, 40, 255};
constexpr Color kOverRight = {40, 30, 200, 255};

// ------------------------------------------------------------------
// Rig — bank + compositor wired together.
// ------------------------------------------------------------------
struct Rig {
    std::shared_ptr<FakeBank> bank = std::make_shared<FakeBank>();
    // unique_ptr: Compositor is copy-only-deleted and non-movable, so
    // the rig itself must stay movable for MakeRig's return value.
    std::unique_ptr<Compositor> comp = std::make_unique<Compositor>();
    bool init_ok = false;
    std::string err;
};

Rig MakeRig(const Project& project) {
    Rig rig;
    CompositorOptions options;
    options.decoder_factory = MakeFakeFactory(rig.bank);
    rig.init_ok =
        rig.comp->Init(project, options, &rig.err, nullptr);
    return rig;
}

void RegisterSpec(Rig& rig, const std::string& path, FakeSpec spec) {
    rig.bank->specs[path] = std::move(spec);
}

// ------------------------------------------------------------------
// Suite 1: TransitionWeightAt (pure)
// ------------------------------------------------------------------
void TestTransitionWeightAt() {
    using XER::VideoEditor::TransitionWeightAt;
    Transition tr;
    tr.duration = 1.0;
    const double cut = 2.0;   // window [1.5, 2.5)
    XER_VE_CHECK(TransitionWeightAt(1.5, tr, cut) == 0.0);
    XER_VE_CHECK(TransitionWeightAt(2.0, tr, cut) == 0.5);
    XER_VE_CHECK(TransitionWeightAt(2.5, tr, cut) == 1.0);
    XER_VE_CHECK(TransitionWeightAt(1.75, tr, cut) == 0.25);
    // Clamped outside the window.
    XER_VE_CHECK(TransitionWeightAt(0.0, tr, cut) == 0.0);
    XER_VE_CHECK(TransitionWeightAt(1.4, tr, cut) == 0.0);
    XER_VE_CHECK(TransitionWeightAt(9.0, tr, cut) == 1.0);
    // Degenerate duration <= 0: w = 1 for t >= cut, else 0.
    Transition bad;
    bad.duration = 0.0;
    XER_VE_CHECK(TransitionWeightAt(1.9, bad, cut) == 0.0);
    XER_VE_CHECK(TransitionWeightAt(2.0, bad, cut) == 1.0);
    bad.duration = -1.0;
    XER_VE_CHECK(TransitionWeightAt(2.5, bad, cut) == 1.0);
}

// ------------------------------------------------------------------
// Suite 2: WarpClipFrame (pure)
// ------------------------------------------------------------------
void TestWarpClipFrame() {
    using XER::VideoEditor::WarpClipFrame;
    // 4x4 solid src -> 8x8 canvas.
    const Color c = {10, 20, 200, 255};
    video::Frame src = MakeVideoFrame(4, 4, c);
    video::Frame out;

    // Identity: scaled rect = 4x4 at top-left -> that quadrant is
    // filled; the rest stays transparent.
    Transform tr;   // offset 0, scale 1, rot 0, opacity 1
    XER_VE_CHECK(WarpClipFrame(src, tr, 8, 8, &out));
    XER_VE_CHECK(out.Width() == 8 && out.Height() == 8);
    XER_VE_CHECK(FramePxIs(out, 0, 0, 10, 20, 200, 255));
    XER_VE_CHECK(FramePxIs(out, 3, 3, 10, 20, 200, 255));
    XER_VE_CHECK(FramePxIs(out, 4, 0, 0, 0, 0, 0));
    XER_VE_CHECK(FramePxIs(out, 0, 4, 0, 0, 0, 0));
    XER_VE_CHECK(FramePxIs(out, 7, 7, 0, 0, 0, 0));

    // scale = 2 -> 8x8 scaled rect covers the whole canvas.
    tr.scale = 2.0f;
    XER_VE_CHECK(WarpClipFrame(src, tr, 8, 8, &out));
    XER_VE_CHECK(FramePxIs(out, 7, 7, 10, 20, 200, 255));
    XER_VE_CHECK(FramePxIs(out, 0, 7, 10, 20, 200, 255));

    // offset 0.5/0.5 scale 1 -> rect occupies [4,8) x [4,8).
    tr = Transform{};
    tr.offset_x = 0.5f;
    tr.offset_y = 0.5f;
    XER_VE_CHECK(WarpClipFrame(src, tr, 8, 8, &out));
    XER_VE_CHECK(FramePxIs(out, 4, 4, 10, 20, 200, 255));
    XER_VE_CHECK(FramePxIs(out, 7, 7, 10, 20, 200, 255));
    XER_VE_CHECK(FramePxIs(out, 3, 3, 0, 0, 0, 0));
    XER_VE_CHECK(FramePxIs(out, 4, 3, 0, 0, 0, 0));
    XER_VE_CHECK(FramePxIs(out, 3, 4, 0, 0, 0, 0));

    // opacity 0.5: RGB passes through, alpha ~127-128.
    tr = Transform{};
    tr.opacity = 0.5f;
    XER_VE_CHECK(WarpClipFrame(src, tr, 8, 8, &out));
    XER_VE_CHECK(FramePxNear(out, 0, 0, 10, 20, 200, 128, 2));

    // rotation 90 about the 4x4 rect's center (2,2): corners map
    // dest(3,0)<-src(0,0), dest(3,3)<-src(3,0), dest(0,0)<-src(0,3),
    // dest(0,3)<-src(3,3) per the inverse-map contract.
    video::Frame corners(4, 4, video::PixelFormat::BGRA8);
    XER_VE_CHECK(corners.IsValid());
    auto set_px = [&](int x, int y, const Color& col) {
        std::uint8_t* p =
            corners.Data() + (static_cast<std::size_t>(y) * 4 + x) * 4u;
        p[0] = col[0];
        p[1] = col[1];
        p[2] = col[2];
        p[3] = col[3];
    };
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            set_px(x, y, kBlack);
        }
    }
    set_px(0, 0, kRed);
    set_px(3, 0, kGreen);
    set_px(0, 3, kBlue);
    set_px(3, 3, kWhite);
    tr = Transform{};
    tr.rotation_deg = 90.0f;
    XER_VE_CHECK(WarpClipFrame(corners, tr, 8, 8, &out));
    XER_VE_CHECK(FramePxNear(out, 3, 0, 10, 20, 200, 255, 2));   // src(0,0)
    XER_VE_CHECK(FramePxNear(out, 3, 3, 30, 200, 40, 255, 2));   // src(3,0)
    XER_VE_CHECK(FramePxNear(out, 0, 0, 200, 40, 10, 255, 2));   // src(0,3)
    XER_VE_CHECK(FramePxNear(out, 0, 3, 240, 240, 240, 255, 2)); // src(3,3)
    XER_VE_CHECK(FramePxNear(out, 1, 1, 5, 5, 5, 255, 2));       // src(1,2)
    XER_VE_CHECK(FramePxIs(out, 4, 4, 0, 0, 0, 0));
    XER_VE_CHECK(FramePxIs(out, 7, 7, 0, 0, 0, 0));

    // Degenerate transforms: null out, zero scale -> transparent.
    XER_VE_CHECK(!WarpClipFrame(src, tr, 8, 8, nullptr));
    tr = Transform{};
    tr.scale = 0.0f;
    XER_VE_CHECK(WarpClipFrame(src, tr, 8, 8, &out));
    XER_VE_CHECK(FramePxIs(out, 0, 0, 0, 0, 0, 0));
}

// ------------------------------------------------------------------
// Suite 3: ApplyTransitionPixels (pure)
// ------------------------------------------------------------------
void TestApplyTransitionPixels() {
    using XER::VideoEditor::ApplyTransitionPixels;
    using XER::VideoEditor::Transition;
    video::Frame under = MakeVideoFrame(8, 4, kRed);
    video::Frame over = MakeVideoFrame(8, 4, kGreen);
    video::Frame out;

    // Crossfade w=0.5, both opaque: straight lerp == premul lerp.
    // (10,20,200)x0.5 + (30,200,40)x0.5 = (20,110,120).
    XER_VE_CHECK(ApplyTransitionPixels(Transition::Type::Crossfade, 0.5,
                                      under, over, &out));
    XER_VE_CHECK(FramePxIs(out, 0, 0, 20, 110, 120, 255));
    XER_VE_CHECK(FramePxIs(out, 7, 3, 20, 110, 120, 255));
    // weight clamps: w=2.0 -> pure over.
    XER_VE_CHECK(ApplyTransitionPixels(Transition::Type::Crossfade, 2.0,
                                      under, over, &out));
    XER_VE_CHECK(FramePxIs(out, 0, 0, 30, 200, 40, 255));

    // Crossfade with a semi-transparent under: premultiplied lerp —
    // NOT straight lerp. under=(100,0,0,128), over=(0,0,200,255), w=0.5:
    //   ua=128/255, oa=1 -> pa=0.75098
    //   B = 100*ua*0.5/pa = 33.4 ; R = 200*1*0.5/pa = 133.2 ; A = 192
    video::Frame semi_u(8, 4, video::PixelFormat::BGRA8);
    video::Frame semi_o(8, 4, video::PixelFormat::BGRA8);
    XER_VE_CHECK(semi_u.IsValid() && semi_o.IsValid());
    for (int i = 0; i < 8 * 4; ++i) {
        semi_u.Data()[i * 4 + 0] = 100;
        semi_u.Data()[i * 4 + 1] = 0;
        semi_u.Data()[i * 4 + 2] = 0;
        semi_u.Data()[i * 4 + 3] = 128;
        semi_o.Data()[i * 4 + 0] = 0;
        semi_o.Data()[i * 4 + 1] = 0;
        semi_o.Data()[i * 4 + 2] = 200;
        semi_o.Data()[i * 4 + 3] = 255;
    }
    XER_VE_CHECK(ApplyTransitionPixels(Transition::Type::Crossfade, 0.5,
                                      semi_u, semi_o, &out));
    XER_VE_CHECK(FramePxNear(out, 4, 2, 33, 0, 133, 192, 3));

    // WipeLeft w=0.5 on 8px: revealed where (x+0.5)/8 < 0.5 -> x <= 3.
    XER_VE_CHECK(ApplyTransitionPixels(Transition::Type::WipeLeft, 0.5,
                                      under, over, &out));
    XER_VE_CHECK(FramePxIs(out, 0, 1, 30, 200, 40, 255));
    XER_VE_CHECK(FramePxIs(out, 3, 1, 30, 200, 40, 255));
    XER_VE_CHECK(FramePxIs(out, 4, 1, 10, 20, 200, 255));
    XER_VE_CHECK(FramePxIs(out, 7, 1, 10, 20, 200, 255));
    // WipeRight w=0.25: revealed where (x+0.5)/8 > 0.75 -> x >= 6.
    XER_VE_CHECK(ApplyTransitionPixels(Transition::Type::WipeRight, 0.25,
                                      under, over, &out));
    XER_VE_CHECK(FramePxIs(out, 5, 1, 10, 20, 200, 255));
    XER_VE_CHECK(FramePxIs(out, 6, 1, 30, 200, 40, 255));
    XER_VE_CHECK(FramePxIs(out, 7, 1, 30, 200, 40, 255));
    // WipeUp w=0.5 on 4px rows: revealed where (y+0.5)/4 < 0.5 -> y <= 1.
    XER_VE_CHECK(ApplyTransitionPixels(Transition::Type::WipeUp, 0.5,
                                      under, over, &out));
    XER_VE_CHECK(FramePxIs(out, 0, 0, 30, 200, 40, 255));
    XER_VE_CHECK(FramePxIs(out, 0, 1, 30, 200, 40, 255));
    XER_VE_CHECK(FramePxIs(out, 0, 2, 10, 20, 200, 255));
    // WipeDown w=0.75: revealed where (y+0.5)/4 > 0.25 -> y >= 1.
    XER_VE_CHECK(ApplyTransitionPixels(Transition::Type::WipeDown, 0.75,
                                      under, over, &out));
    XER_VE_CHECK(FramePxIs(out, 0, 0, 10, 20, 200, 255));
    XER_VE_CHECK(FramePxIs(out, 0, 1, 30, 200, 40, 255));
    XER_VE_CHECK(FramePxIs(out, 0, 3, 30, 200, 40, 255));

    // SlideLeft w=0.5 on 8x4: under shifts -4px, over +4px.
    // Split sources: under cols[0,4)=UL cols[4,8)=UR;
    //                over  cols[0,4)=OL cols[4,8)=OR.
    video::Frame u_split =
        MakeVideoSplitFrame(8, 4, 4, kUnderLeft, kUnderRight);
    video::Frame o_split =
        MakeVideoSplitFrame(8, 4, 4, kOverLeft, kOverRight);
    XER_VE_CHECK(ApplyTransitionPixels(Transition::Type::SlideLeft, 0.5,
                                      u_split, o_split, &out));
    // canvas x<4: under samples cols 4..7 -> UR; over samples <0 ->
    // transparent. canvas x>=4: under >8 -> transparent; over samples
    // cols 0..3 -> OL.
    XER_VE_CHECK(FramePxIs(out, 1, 1, 200, 20, 10, 255));   // UR
    XER_VE_CHECK(FramePxIs(out, 6, 1, 30, 200, 40, 255));   // OL
    // Endpoints: w=0 -> under verbatim; w=1 -> over verbatim.
    XER_VE_CHECK(ApplyTransitionPixels(Transition::Type::SlideLeft, 0.0,
                                      u_split, o_split, &out));
    XER_VE_CHECK(FramePxIs(out, 1, 1, 10, 20, 200, 255));   // UL
    XER_VE_CHECK(FramePxIs(out, 6, 1, 200, 20, 10, 255));   // UR
    XER_VE_CHECK(ApplyTransitionPixels(Transition::Type::SlideLeft, 1.0,
                                      u_split, o_split, &out));
    XER_VE_CHECK(FramePxIs(out, 1, 1, 30, 200, 40, 255));   // OL
    XER_VE_CHECK(FramePxIs(out, 6, 1, 40, 30, 200, 255));   // OR

    // Contract failures.
    video::Frame small = MakeVideoFrame(4, 4, kRed);
    XER_VE_CHECK(!ApplyTransitionPixels(Transition::Type::Crossfade, 0.5,
                                       under, small, &out));
    XER_VE_CHECK(!ApplyTransitionPixels(Transition::Type::Crossfade, 0.5,
                                       under, over, nullptr));
}

// ------------------------------------------------------------------
// Suite 4: Compositor basics (single clip, tracks, transforms,
// effects, lifecycle)
// ------------------------------------------------------------------
void TestCompositorBasics() {
    // Init validation: bad canvas / bad fps fail; clips problems do
    // NOT fail Init.
    {
        Project bad = MakeCanvasProject();
        bad.width = 0;
        Rig rig = MakeRig(bad);
        XER_VE_CHECK(!rig.init_ok);
        XER_VE_CHECK(!rig.err.empty());
    }
    {
        Project bad = MakeCanvasProject();
        bad.fps_num = 0;
        Rig rig = MakeRig(bad);
        XER_VE_CHECK(!rig.init_ok);
    }
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(
            MakeClip("c1", 0, 0.0, 1.0, "fake://red"));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        XER_VE_CHECK(rig.comp->CanvasWidth() == 16);
        XER_VE_CHECK(rig.comp->CanvasHeight() == 16);
        XER_VE_CHECK(rig.comp->DurationSeconds() == 1.0);
        XER_VE_CHECK(rig.comp->FrameCount(30, 1) == 30);
        XER_VE_CHECK(rig.comp->FrameCount(0, 1) == 0);
        XER_VE_CHECK(!rig.comp->Cancelled());
    }

    // Single full-canvas clip: every pixel is the clip color.
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(
            MakeClip("c1", 0, 0.0, 1.0, "fake://red"));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://red",
                     MakeSolidSpecAuto(16, 16, 4.0, 4, kRed));
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.0, &out, &err));
        XER_VE_CHECK(out.size() == 16u * 16u * 4u);
        XER_VE_CHECK(VecPxIs(out, 16, 0, 0, 10, 20, 200, 255));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 20, 200, 255));
        XER_VE_CHECK(VecPxIs(out, 16, 15, 15, 10, 20, 200, 255));
        XER_VE_CHECK(rig.bank->Opens("fake://red") == 1);
    }

    // Empty regions render bg_color; clip outside [0,duration) too.
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(
            MakeClip("c1", 0, 1.0, 1.0, "fake://red"));   // [1,2)
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://red",
                     MakeSolidSpecAuto(16, 16, 4.0, 4, kRed));
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 0, 0, 0, 255));
        XER_VE_CHECK(rig.bank->Opens("fake://red") == 0);  // lazy!
        XER_VE_CHECK(rig.comp->RenderFrameAt(2.0, &out, &err));  // clamped
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 0, 0, 0, 255));
        XER_VE_CHECK(rig.bank->Opens("fake://red") == 0);
        XER_VE_CHECK(rig.comp->RenderFrameAt(1.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 20, 200, 255));
        XER_VE_CHECK(rig.bank->Opens("fake://red") == 1);
    }

    // Adjacent boundary: t=1.0 belongs to the SECOND clip ([) model).
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(MakeClip("a", 0, 0.0, 1.0, "fake://a"));
        p.clips.push_back(MakeClip("b", 0, 1.0, 1.0, "fake://b"));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://a",
                     MakeSolidSpec(16, 16, 2.0, {0.0, 0.5, 1.0}, kRed));
        RegisterSpec(rig, "fake://b",
                     MakeSolidSpec(16, 16, 2.0, {0.0, 0.5, 1.0}, kGreen));
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.999, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 20, 200, 255));
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 20, 200, 255));
        XER_VE_CHECK(rig.comp->RenderFrameAt(1.0, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 30, 200, 40, 255));
    }

    // Track stacking: V2 (top) left-half red @ opacity 0.5 over V1
    // full blue; then hidden -> pure blue, top decoder never opened.
    {
        Project p = MakeCanvasProject();
        p.tracks.push_back(Track{TrackKind::Video, "V2"});
        p.clips.push_back(MakeClip("bottom", 0, 0.0, 2.0, "fake://bot"));
        Clip top = MakeClip("top", 1, 0.0, 2.0, "fake://top");
        top.transform.opacity = 0.5f;   // 8x16 src -> left half
        p.clips.push_back(top);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://bot",
                     MakeSolidSpecAuto(16, 16, 2.0, 4, kBlue));
        FakeSpec top_spec =
            MakeSolidSpecAuto(8, 16, 2.0, 4, kRed);
        RegisterSpec(rig, "fake://top", std::move(top_spec));
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        // Left half: red@~0.5 alpha over opaque blue ->
        //   B=10*0.5+200*0.5=105, G=20*0.5+40*0.5=30, R=200*0.5+10*0.5=105
        XER_VE_CHECK(VecPxNear(out, 16, 4, 8, 105, 30, 105, 255, 4));
        // Right half untouched: pure blue.
        XER_VE_CHECK(VecPxIs(out, 16, 12, 8, 200, 40, 10, 255));

        // Hidden track contributes nothing — and its decoder is
        // never even opened (fresh rig, hidden from the start).
        Project hidden = p;
        hidden.tracks[1].hidden = true;
        Rig rig2 = MakeRig(hidden);
        XER_VE_CHECK(rig2.init_ok);
        RegisterSpec(rig2, "fake://bot",
                     MakeSolidSpecAuto(16, 16, 2.0, 4, kBlue));
        RegisterSpec(rig2, "fake://top",
                     MakeSolidSpecAuto(8, 16, 2.0, 4, kRed));
        XER_VE_CHECK(rig2.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 4, 8, 200, 40, 10, 255));
        XER_VE_CHECK(VecPxIs(out, 16, 12, 8, 200, 40, 10, 255));
        XER_VE_CHECK(rig2.bank->Opens("fake://top") == 0);
        XER_VE_CHECK(rig2.bank->Opens("fake://bot") == 1);
    }

    // muted clips still composite (mute is an audio concern).
    {
        Project p = MakeCanvasProject();
        Clip c = MakeClip("c1", 0, 0.0, 1.0, "fake://red");
        c.muted = true;
        p.clips.push_back(c);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://red",
                     MakeSolidSpecAuto(16, 16, 4.0, 4, kRed));
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 20, 200, 255));
    }

    // Transform smoke: 16x16 red scaled 0.5 @ offset (0.5,0.5)
    // opacity 0.5 -> rect x[8,16) y[8,16); inside blends ~half over bg.
    {
        Project p = MakeCanvasProject();
        Clip c = MakeClip("c1", 0, 0.0, 1.0, "fake://red");
        c.transform.offset_x = 0.5f;
        c.transform.offset_y = 0.5f;
        c.transform.scale = 0.5f;
        c.transform.opacity = 0.5f;
        p.clips.push_back(c);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://red",
                     MakeSolidSpecAuto(16, 16, 4.0, 4, kRed));
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 4, 4, 0, 0, 0, 255));   // outside
        XER_VE_CHECK(VecPxIs(out, 16, 7, 15, 0, 0, 0, 255));  // corner out
        // Inside: red@0.5 over opaque black -> ~(5,10,100,255)
        XER_VE_CHECK(VecPxNear(out, 16, 12, 12, 5, 10, 100, 255, 3));
        XER_VE_CHECK(VecPxNear(out, 16, 8, 8, 5, 10, 100, 255, 3));
    }

    // Effects: brightness_contrast(+10) then bogus (skipped+warn)
    // then grayscale -> ~(82,82,82).
    {
        Project p = MakeCanvasProject();
        Clip c = MakeClip("c1", 0, 0.0, 1.0, "fake://red");
        ClipEffect bc;
        bc.type = "brightness_contrast";
        bc.params = {10.0f, 1.0f};
        ClipEffect bogus;
        bogus.type = "bogus";
        ClipEffect gray;
        gray.type = "grayscale";
        c.effects = {bc, bogus, gray};
        p.clips.push_back(c);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        XER_VE_CHECK(HasWarning(*rig.comp, "bogus"));
        RegisterSpec(rig, "fake://red",
                     MakeSolidSpecAuto(16, 16, 4.0, 4, kRed));
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        // (10,20,200)+10 -> (20,30,210); luma=0.299*210+0.587*30+
        // 0.114*20 = 82.68 -> 82 on all channels.
        XER_VE_CHECK(VecPxNear(out, 16, 8, 8, 82, 82, 82, 255, 3));
    }

    // Lazy open: clip at [2,3) does not open before its window.
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(
            MakeClip("lazy", 0, 2.0, 1.0, "fake://lazy"));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://lazy",
                     MakeSolidSpecAuto(16, 16, 4.0, 4, kRed));
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(rig.bank->Opens("fake://lazy") == 0);
        XER_VE_CHECK(rig.comp->RenderFrameAt(2.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 20, 200, 255));
        XER_VE_CHECK(rig.bank->Opens("fake://lazy") == 1);
        XER_VE_CHECK(rig.bank->Seeks("fake://lazy") == 1);
        XER_VE_CHECK(rig.comp->RenderFrameAt(2.6, &out, &err));
        XER_VE_CHECK(rig.bank->Opens("fake://lazy") == 1);  // stays open
        XER_VE_CHECK(rig.bank->Seeks("fake://lazy") == 1);  // seek once
    }

    // Cancel: cooperative, checked per RenderFrameAt.
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(
            MakeClip("c1", 0, 0.0, 1.0, "fake://red"));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://red",
                     MakeSolidSpecAuto(16, 16, 4.0, 4, kRed));
        rig.comp->Cancel();
        XER_VE_CHECK(rig.comp->Cancelled());
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(!rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(err.find("cancel") != std::string::npos);
    }

    // Fatal paths: missing file, undecodable (empty frame list).
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(
            MakeClip("missing", 0, 0.0, 1.0, "fake://missing"));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(!rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(err.find("cannot open") != std::string::npos);
    }
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(
            MakeClip("empty", 0, 0.0, 1.0, "fake://empty"));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        FakeSpec empty;   // no frames at all
        RegisterSpec(rig, "fake://empty", std::move(empty));
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(!rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(err.find("no decodable frames") != std::string::npos);
    }

    // Audio-only clip on a video track: skipped with a warning,
    // canvas stays bg — NOT fatal.
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(
            MakeClip("audio", 0, 0.0, 1.0, "fake://audio"));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        FakeSpec spec;
        spec.audio_only = true;
        spec.frames.push_back(MakeFramePixels(16, 16, kRed, 0.0));
        RegisterSpec(rig, "fake://audio", std::move(spec));
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 0, 0, 0, 255));
        XER_VE_CHECK(HasWarning(*rig.comp, "no video"));
        XER_VE_CHECK(rig.bank->Decodes("fake://audio") == 0);
    }

    // Two clips sharing one source path get INDEPENDENT decoder
    // sessions (per-clip lazy open, in_point offsets honored).
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(MakeClip("a", 0, 0.0, 1.0, "fake://shared"));
        p.clips.push_back(MakeClip("b", 0, 3.0, 1.0, "fake://shared",
                                   2.0));   // in_point = 2.0
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        // 72 frames @24fps -> 3 s; frame i has R=i, G=200, B=10.
        RegisterSpec(rig, "fake://shared",
                     MakeRampSpec(16, 16, 24.0, 72, 10, 200, 0, 1));
        std::vector<std::uint8_t> out;
        std::string err;
        // A @ t=0.5 -> src_t=0.5 -> frame 12 (pts 0.5) -> R=12.
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 200, 12, 255));
        // B @ t=3.5 -> src_t=2.0+(3.5-3.0)=2.5 -> frame 60 -> R=60.
        XER_VE_CHECK(rig.comp->RenderFrameAt(3.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 200, 60, 255));
        XER_VE_CHECK(rig.bank->Opens("fake://shared") == 2);
    }
}

// ------------------------------------------------------------------
// Suite 5: decode behavior — nearest-PTS, EOF freeze, sparse pts
// ------------------------------------------------------------------
void TestDecodeBehavior() {
    // Nearest-PTS incl. the PREVIOUS candidate + sequential decode:
    // 24 frames @24fps, frame i -> R=i*10. Renders must be monotonic.
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(MakeClip("seq", 0, 0.0, 1.0, "fake://seq"));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://seq",
                     MakeRampSpec(16, 16, 24.0, 24, 30, 60, 0, 10));
        std::vector<std::uint8_t> out;
        std::string err;
        // t=0.47: decode stops at first pts>=0.47 -> idx12@0.5;
        // prev=idx11@0.4583 (|0.0117|) beats cur=idx12@0.5 (|0.03|)
        // -> frame 11 -> R=110. 13 NextFrame calls total.
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.47, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 30, 60, 110, 255));
        XER_VE_CHECK(rig.bank->Decodes("fake://seq") == 13);
        XER_VE_CHECK(rig.bank->Seeks("fake://seq") == 1);
        // t=0.48: cur idx12@0.5 already >= target -> no new decodes;
        // |0.5-0.48|=0.02 < |0.4583-0.48|=0.0217 -> cur -> R=120.
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.48, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 30, 60, 120, 255));
        XER_VE_CHECK(rig.bank->Decodes("fake://seq") == 13);
        // t=0.5: exact pts hit -> frame 12 -> R=120.
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 30, 60, 120, 255));
        XER_VE_CHECK(rig.bank->Decodes("fake://seq") == 13);
        XER_VE_CHECK(rig.bank->Opens("fake://seq") == 1);
    }

    // EOF freeze + tie-to-previous: 2 frames pts {0, 0.5}.
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(MakeClip("short", 0, 0.0, 4.0, "fake://short"));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        FakeSpec spec;
        spec.width = 16;
        spec.height = 16;
        spec.fps = 2.0;
        spec.frames.push_back(MakeFramePixels(16, 16, kRed, 0.0));
        spec.frames.push_back(MakeFramePixels(16, 16, kBlue, 0.5));
        RegisterSpec(rig, "fake://short", std::move(spec));
        std::vector<std::uint8_t> out;
        std::string err;
        // t=0.2: |0-0.2|=0.2 < |0.5-0.2|=0.3 -> frame0 (red).
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.2, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 20, 200, 255));
        // t=0.25: exact tie -> previous -> red.
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.25, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 20, 200, 255));
        // t=0.4: |0.5-0.4|=0.1 < |0-0.4|=0.4 -> frame1 (blue).
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.4, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 200, 40, 10, 255));
        // t=3.0: past EOF -> freeze last frame (blue). The EOF probe
        // returns false before the fake's decode counter, so only the
        // two real frames are counted.
        XER_VE_CHECK(rig.comp->RenderFrameAt(3.0, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 200, 40, 10, 255));
        XER_VE_CHECK(rig.bank->Decodes("fake://short") == 2);
    }

    // Missing timestamps (pts=-1): synthesized 1/fps spacing seeded
    // from the post-Seek position. fps=8 -> step 0.125 is exact in
    // binary, so synthesized pts land on exact fractions (no fragile
    // floating-point tie-breaks).
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(MakeClip("sparse", 0, 0.0, 1.0, "fake://sp"));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        FakeSpec spec;
        spec.width = 16;
        spec.height = 16;
        spec.fps = 8.0;   // synth step 0.125 — exact binary fraction
        // 4 frames, all pts=-1, indexed colors R=i*40.
        for (int i = 0; i < 4; ++i) {
            spec.frames.push_back(MakeFramePixels(
                16, 16, Color{30, 60,
                              static_cast<std::uint8_t>(i * 40), 255},
                -1.0));
        }
        RegisterSpec(rig, "fake://sp", std::move(spec));
        std::vector<std::uint8_t> out;
        std::string err;
        // t=0.5 -> Seek(0.25) -> synth_pts=0.25; frames synthesize
        // 0.375, 0.5, 0.625 -> stop at 0.5>=0.5 -> exact hit on
        // frame index 1 -> R=40.
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 30, 60, 40, 255));
        XER_VE_CHECK(rig.bank->Decodes("fake://sp") == 2);
        // Second render is deterministic (session persists).
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 30, 60, 40, 255));
        XER_VE_CHECK(rig.bank->Decodes("fake://sp") == 2);
    }
}

// ------------------------------------------------------------------
// Suite 6: compositor-level transitions
// ------------------------------------------------------------------
void TestCompositorTransitions() {
    const std::vector<double> kPts = {0.0, 0.5, 1.0, 1.5, 2.0, 2.5};

    // Crossfade A->B D=1.0, cut at t=2.0, window [1.5,2.5).
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(MakeClip("a", 0, 0.0, 2.0, "fake://a"));
        p.clips.push_back(MakeClip("b", 0, 2.0, 2.0, "fake://b"));
        p.transitions.push_back(
            MakeTransition("a", "b", Transition::Type::Crossfade, 1.0));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://a",
                     MakeSolidSpec(16, 16, 2.0, kPts, kRed));
        RegisterSpec(rig, "fake://b",
                     MakeSolidSpec(16, 16, 2.0, kPts, kGreen));
        std::vector<std::uint8_t> out;
        std::string err;
        // Before the window: A alone -> pure red.
        XER_VE_CHECK(rig.comp->RenderFrameAt(1.4, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 20, 200, 255));
        // w=0.25 -> 0.75A+0.25B: B=12.5->13, G=65, R=160.
        XER_VE_CHECK(rig.comp->RenderFrameAt(1.75, &out, &err));
        XER_VE_CHECK(VecPxNear(out, 16, 8, 8, 13, 65, 160, 255, 3));
        // w=0.5 -> (20,110,120).
        XER_VE_CHECK(rig.comp->RenderFrameAt(2.0, &out, &err));
        XER_VE_CHECK(VecPxNear(out, 16, 8, 8, 20, 110, 120, 255, 3));
        // w=0.75 -> 0.25A+0.75B: B=25, G=155, R=80.
        XER_VE_CHECK(rig.comp->RenderFrameAt(2.25, &out, &err));
        XER_VE_CHECK(VecPxNear(out, 16, 8, 8, 25, 155, 80, 255, 3));
        // Window end is exclusive: t=2.5 -> B alone -> green.
        XER_VE_CHECK(rig.comp->RenderFrameAt(2.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 30, 200, 40, 255));
    }

    // WipeLeft same geometry: hard edge sweeps left->right.
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(MakeClip("a", 0, 0.0, 2.0, "fake://a"));
        p.clips.push_back(MakeClip("b", 0, 2.0, 2.0, "fake://b"));
        p.transitions.push_back(
            MakeTransition("a", "b", Transition::Type::WipeLeft, 1.0));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://a",
                     MakeSolidSpec(16, 16, 2.0, kPts, kRed));
        RegisterSpec(rig, "fake://b",
                     MakeSolidSpec(16, 16, 2.0, kPts, kGreen));
        std::vector<std::uint8_t> out;
        std::string err;
        // w=0.5 at t=2.0: left half (x+0.5<8 -> x<=7) is B.
        XER_VE_CHECK(rig.comp->RenderFrameAt(2.0, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 4, 8, 30, 200, 40, 255));
        XER_VE_CHECK(VecPxIs(out, 16, 7, 8, 30, 200, 40, 255));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 20, 200, 255));
        XER_VE_CHECK(VecPxIs(out, 16, 12, 8, 10, 20, 200, 255));
        // w=0.25 at t=1.75: revealed x+0.5<4 -> x<=3.
        XER_VE_CHECK(rig.comp->RenderFrameAt(1.75, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 2, 8, 30, 200, 40, 255));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 10, 20, 200, 255));
    }

    // SlideLeft with split-tone sources: composite motion is toward
    // the left — under exits left, over enters from the right.
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(MakeClip("a", 0, 0.0, 2.0, "fake://a"));
        p.clips.push_back(MakeClip("b", 0, 2.0, 2.0, "fake://b"));
        p.transitions.push_back(
            MakeTransition("a", "b", Transition::Type::SlideLeft, 1.0));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://a",
                     MakeSplitSpec(16, 16, 2.0, kPts, 8,
                                   kUnderLeft, kUnderRight));
        RegisterSpec(rig, "fake://b",
                     MakeSplitSpec(16, 16, 2.0, kPts, 8,
                                   kOverLeft, kOverRight));
        std::vector<std::uint8_t> out;
        std::string err;
        // w=0.5: left half shows under's RIGHT half (UR), right half
        // shows over's LEFT half (OL) — both shifted left by 8px.
        XER_VE_CHECK(rig.comp->RenderFrameAt(2.0, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 4, 8, 200, 20, 10, 255));   // UR
        XER_VE_CHECK(VecPxIs(out, 16, 12, 8, 30, 200, 40, 255));  // OL
        XER_VE_CHECK(VecPxIs(out, 16, 4, 8, 200, 20, 10, 255));
        XER_VE_CHECK(!VecPxIs(out, 16, 4, 8, 10, 20, 200, 255));  // not UL
        XER_VE_CHECK(!VecPxIs(out, 16, 12, 8, 40, 30, 200, 255)); // not OR
    }

    // Degenerate 3-candidate overlap: A[0,2) B[2,2.4) C[2.4,5) with
    // A->B and B->C transitions; first stored match wins.
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(MakeClip("a", 0, 0.0, 2.0, "fake://a"));
        p.clips.push_back(MakeClip("b", 0, 2.0, 0.4, "fake://b"));
        p.clips.push_back(MakeClip("c", 0, 2.4, 2.6, "fake://c"));
        p.transitions.push_back(
            MakeTransition("a", "b", Transition::Type::Crossfade, 1.0));
        p.transitions.push_back(
            MakeTransition("b", "c", Transition::Type::Crossfade, 1.0));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://a",
                     MakeSolidSpec(16, 16, 2.0, kPts, kRed));
        RegisterSpec(rig, "fake://b",
                     MakeSolidSpec(16, 16, 4.0, {0.0, 0.25, 0.5, 0.75},
                                   kGreen));
        RegisterSpec(rig, "fake://c",
                     MakeSolidSpec(16, 16, 4.0, {0.0, 0.25, 0.5, 0.75},
                                   kBlue));
        std::vector<std::uint8_t> out;
        std::string err;
        // t=2.1: A->B window [1.5,2.5) matches first -> w=0.6 ->
        // 0.4A+0.6B: B=22, G=128, R=104.
        XER_VE_CHECK(rig.comp->RenderFrameAt(2.1, &out, &err));
        XER_VE_CHECK(VecPxNear(out, 16, 8, 8, 22, 128, 104, 255, 3));
        // t=2.7: only B->C [1.9,2.9) matches (A ext ended 2.5) ->
        // w=0.8 -> 0.2B+0.8C: B=166, G=72, R=16.
        XER_VE_CHECK(rig.comp->RenderFrameAt(2.7, &out, &err));
        XER_VE_CHECK(VecPxNear(out, 16, 8, 8, 166, 72, 16, 255, 3));
    }

    // Overlapping clips with NO transition (invalid project):
    // deterministic pick = latest-starting nominal clip + one warning.
    {
        Project p = MakeCanvasProject();
        p.clips.push_back(MakeClip("x", 0, 0.0, 4.0, "fake://x"));
        p.clips.push_back(MakeClip("y", 0, 2.0, 4.0, "fake://y"));
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        RegisterSpec(rig, "fake://x",
                     MakeSolidSpec(16, 16, 1.0, {0, 1, 2, 3, 4}, kRed));
        RegisterSpec(rig, "fake://y",
                     MakeSolidSpec(16, 16, 1.0, {0, 1, 2, 3, 4}, kGreen));
        std::vector<std::uint8_t> out;
        std::string err;
        // t=3: both nominal-active; y starts later -> green.
        XER_VE_CHECK(rig.comp->RenderFrameAt(3.0, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 30, 200, 40, 255));
        XER_VE_CHECK(HasWarning(*rig.comp, "ambiguous"));
    }
}

// ------------------------------------------------------------------
// Suite 7: FFmpeg integration (self-skips without the bridge)
// ------------------------------------------------------------------
constexpr int kClipW = 64;
constexpr int kClipH = 64;
constexpr int kClipFrames = 8;
constexpr int kClipFps = 24;

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
                [](std::size_t index, FFmpeg::VideoFrameInfo* out,
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

void TestFFmpegIntegration() {
    if (!FFmpeg::IsAvailable()) {
        return;   // self-skip like the decoder/poster suites
    }
    const auto dir =
        XER::VideoEditor::Test::TempDir() / "compositor";
    std::filesystem::create_directories(dir);
    const auto clip_path = dir / "clip.mp4";
    std::string err;
    if (!MakeVideoOnlyClip(Path::ToUtf8(clip_path), &err)) {
        std::fprintf(stderr,
                     "note: compositor fixture encode failed: %s\n",
                     err.c_str());
        return;
    }

    Project p;
    p.width = kClipW;
    p.height = kClipH;
    p.fps_num = 30;
    p.fps_den = 1;
    p.bg_color = {0, 0, 0, 255};
    p.tracks.push_back(Track{TrackKind::Video, "V1"});
    p.clips.push_back(
        MakeClip("real", 0, 0.0, 0.4, Path::ToUtf8(clip_path)));

    Compositor comp;
    CompositorOptions options;   // empty factory -> DefaultDecoderFactory
    XER_VE_CHECK(comp.Init(p, options, &err, nullptr));
    if (!comp.CanvasWidth()) {
        return;
    }
    std::vector<std::uint8_t> out;
    XER_VE_CHECK(comp.RenderFrameAt(0.0, &out, &err));
    XER_VE_CHECK(out.size() ==
                 static_cast<std::size_t>(kClipW) * kClipH * 4u);
    // The moving bar paints (30,60,220) pixels; black bg is (0,0,0).
    bool saw_bar = false;
    for (std::size_t i = 0; i + 3 < out.size(); i += 4) {
        if (out[i + 2] > 150 && out[i + 0] < 100) {
            saw_bar = true;
            break;
        }
    }
    XER_VE_CHECK(saw_bar);
    // A second frame forward still renders (sequential decode path).
    XER_VE_CHECK(comp.RenderFrameAt(0.2, &out, &err));
}

}  // namespace

int XER_VideoEditor_RunCompositorTests() {
    TestTransitionWeightAt();
    TestWarpClipFrame();
    TestApplyTransitionPixels();
    TestCompositorBasics();
    TestDecodeBehavior();
    TestCompositorTransitions();
    TestFFmpegIntegration();
    return 0;
}
