// Compositor2 suite — the Phase-2 effect additions: the CPU-only
// ClipEffect types (chroma_key, color_balance, posterize), Text-clip
// inline styles (ClipTextStyle via the "text | key=value" param
// syntax), Premiere-style adjustment layers (Clip::is_adjustment),
// and the .xerproj v2 back-compat for the new "adjustment" field.
// Every composited case runs on GENERATED clips (ColorMatte/Text), so
// no decoder is needed — the factory is a null sentinel that fails
// loudly if a media clip ever sneaks in. Expected pixel values come
// from the documented effect math in compositor.cc.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "video_editor_test_util.h"
#include "compositor.h"
#include "generated_source.h"
#include "helper/path_utils.h"
#include "media_decoder.h"
#include "project_store.h"
#include "timeline_model.h"

namespace {
namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers
using XER::VideoEditor::Clip;
using XER::VideoEditor::ClipEffect;
using XER::VideoEditor::ClipTextStyle;
using XER::VideoEditor::Compositor;
using XER::VideoEditor::CompositorOptions;
using XER::VideoEditor::GenerateSourceFrame;
using XER::VideoEditor::MediaDecoderPtr;
using XER::VideoEditor::ParseClipTextStyle;
using XER::VideoEditor::Project;
using XER::VideoEditor::ProjectStore;
using XER::VideoEditor::SourceKind;
using XER::VideoEditor::Track;
using XER::VideoEditor::TrackKind;

// ------------------------------------------------------------------
// Project / clip helpers — hand-built Projects like the compositor
// suite (tests deliberately bypass AddClip validation).
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
              double duration) {
    Clip c;
    c.id = id;
    c.track = track;
    c.timeline_start = start;
    c.duration = duration;
    return c;
}

// Procedural clip — no source_path, no decoder session ever opens.
Clip MakeGeneratedClip(const std::string& id, int track, double start,
                       double duration, SourceKind kind,
                       const std::string& param) {
    Clip c = MakeClip(id, track, start, duration);
    c.source.kind = kind;
    c.source.param = param;
    return c;
}

// ------------------------------------------------------------------
// Rig — compositor with a decoder factory that must never fire (all
// clips here are generated; returning nullptr turns a stray media
// clip into a hard render error instead of touching real FFmpeg).
// ------------------------------------------------------------------
struct Rig {
    std::unique_ptr<Compositor> comp = std::make_unique<Compositor>();
    bool init_ok = false;
    std::string err;
};

Rig MakeRig(const Project& project) {
    Rig rig;
    CompositorOptions options;
    options.decoder_factory = []() -> MediaDecoderPtr {
        return nullptr;
    };
    rig.init_ok = rig.comp->Init(project, options, &rig.err, nullptr);
    return rig;
}

// ------------------------------------------------------------------
// Pixel assert helpers (BGRA byte order)
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

bool VecPxNear(const std::vector<std::uint8_t>& bgra, int w, int x,
               int y, std::uint8_t b, std::uint8_t g, std::uint8_t r,
               std::uint8_t a, int tol) {
    const std::uint8_t* p = VecPx(bgra, w, x, y);
    return std::abs(static_cast<int>(p[0]) - b) <= tol &&
           std::abs(static_cast<int>(p[1]) - g) <= tol &&
           std::abs(static_cast<int>(p[2]) - r) <= tol &&
           std::abs(static_cast<int>(p[3]) - a) <= tol;
}

#if defined(XER_VE_HAS_SKIA)
// Skia-path text probes — the rasterizer draws a real system font, so
// glyph coordinates/dims depend on the installed font metrics and can
// only be asserted structurally ("some face ink exists", "a blurred
// shadow is present"), never glyph-exact.

// Any pixel within `tol` of the target BGRA — detects ink of the
// requested color without pinning a coordinate.
bool FrameHasPxNear(const video::Frame& f, std::uint8_t b,
                    std::uint8_t g, std::uint8_t r, std::uint8_t a,
                    int tol) {
    for (int y = 0; y < f.Height(); ++y) {
        for (int x = 0; x < f.Width(); ++x) {
            const std::uint8_t* p = FramePx(f, x, y);
            if (std::abs(static_cast<int>(p[0]) - b) <= tol &&
                std::abs(static_cast<int>(p[1]) - g) <= tol &&
                std::abs(static_cast<int>(p[2]) - r) <= tol &&
                std::abs(static_cast<int>(p[3]) - a) <= tol) {
                return true;
            }
        }
    }
    return false;
}

// A blurred drop shadow reads as near-black pixels with PARTIAL
// alpha — the transparent pad and the opaque face both fail this.
bool FrameHasShadowPx(const video::Frame& f) {
    for (int y = 0; y < f.Height(); ++y) {
        for (int x = 0; x < f.Width(); ++x) {
            const std::uint8_t* p = FramePx(f, x, y);
            if (p[3] > 0 && p[3] < 255 && p[0] < 40 && p[1] < 40 &&
                p[2] < 40) {
                return true;
            }
        }
    }
    return false;
}

// Leftmost white-ink column in the frame's lower half — used to prove
// align=right shifts the short second line without hardcoding glyph
// coordinates (-1 = no ink found).
int MinWhiteInkXLowerHalf(const video::Frame& f) {
    int min_x = -1;
    for (int y = f.Height() / 2; y < f.Height(); ++y) {
        for (int x = 0; x < f.Width(); ++x) {
            const std::uint8_t* p = FramePx(f, x, y);
            if (p[3] > 200 && p[0] > 200 && p[1] > 200 && p[2] > 200) {
                if (min_x < 0 || x < min_x) min_x = x;
                break;
            }
        }
    }
    return min_x;
}
#endif  // XER_VE_HAS_SKIA

// ------------------------------------------------------------------
// Suite 1: chroma_key — {key_r, key_g, key_b, tolerance, softness}
// ------------------------------------------------------------------
void TestChromaKey() {
    // Hard key: a green matte over a red matte — the keyed layer goes
    // fully transparent so the red underneath shows through.
    {
        Project p = MakeCanvasProject();
        p.tracks.push_back(Track{TrackKind::Video, "V2"});
        p.clips.push_back(MakeGeneratedClip(
            "under", 0, 0.0, 1.0, SourceKind::ColorMatte, "#FF0000"));
        Clip key = MakeGeneratedClip("key", 1, 0.0, 1.0,
                                     SourceKind::ColorMatte, "#00FF00");
        key.effects.push_back(
            ClipEffect{"chroma_key", {0.0f, 255.0f, 0.0f, 60.0f, 0.0f}});
        p.clips.push_back(key);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 0, 0, 255, 255));
    }

    // A clip far from the key color survives untouched (blue matte,
    // distance ~360 >> tolerance).
    {
        Project p = MakeCanvasProject();
        Clip c = MakeGeneratedClip("blue", 0, 0.0, 1.0,
                                   SourceKind::ColorMatte, "#0000FF");
        c.effects.push_back(
            ClipEffect{"chroma_key", {0.0f, 255.0f, 0.0f, 60.0f, 0.0f}});
        p.clips.push_back(c);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 255, 0, 0, 255));
    }

    // Distance inside the soft band feathers alpha instead of a hard
    // cut: key {0,255,0} tolerance 0 softness 255 on pure green ->
    // dist 0 sits at t=0.5 of the band -> smoothstep(0.5)=0.5 ->
    // alpha ~128 over the black bg -> g ~128.
    {
        Project p = MakeCanvasProject();
        Clip c = MakeGeneratedClip("soft", 0, 0.0, 1.0,
                                   SourceKind::ColorMatte, "#00FF00");
        c.effects.push_back(
            ClipEffect{"chroma_key", {0.0f, 255.0f, 0.0f, 0.0f, 255.0f}});
        p.clips.push_back(c);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxNear(out, 16, 8, 8, 0, 128, 0, 255, 4));
    }

    // Distance just inside tolerance keys out; a near-color just
    // outside is kept — #00C800 (g=200) is 55 units from pure green:
    // tolerance 50 keeps nothing under 50 but 55 survives.
    {
        Project p = MakeCanvasProject();
        Clip c = MakeGeneratedClip("near", 0, 0.0, 1.0,
                                   SourceKind::ColorMatte, "#00C800");
        c.effects.push_back(
            ClipEffect{"chroma_key", {0.0f, 255.0f, 0.0f, 50.0f, 0.0f}});
        p.clips.push_back(c);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 0, 200, 0, 255));
    }
}

// ------------------------------------------------------------------
// Suite 2: color_balance — {lift_r,g,b [-1..1], gain_r,g,b [0..4]}
//   out_c = in_c * gain + lift * 255
// ------------------------------------------------------------------
void TestColorBalance() {
    // lift -0.5 on all channels: matte #C86432 (200,100,50 in rgb) ->
    // (200-127.5, 100-127.5, 50-127.5) -> (73, 0, 0).
    {
        Project p = MakeCanvasProject();
        Clip c = MakeGeneratedClip("m", 0, 0.0, 1.0,
                                   SourceKind::ColorMatte, "#C86432");
        c.effects.push_back(ClipEffect{
            "color_balance",
            {-0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f}});
        p.clips.push_back(c);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        // rgb (50,100,200) -> (0, 0, 73) -> BGRA (0, 0, 73, 255)
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 0, 0, 73, 255));
    }

    // gain_r 0.5 halves only the red channel: rgb (200,100,50) ->
    // (100,100,50).
    {
        Project p = MakeCanvasProject();
        Clip c = MakeGeneratedClip("m", 0, 0.0, 1.0,
                                   SourceKind::ColorMatte, "#C86432");
        c.effects.push_back(ClipEffect{
            "color_balance",
            {0.0f, 0.0f, 0.0f, 0.5f, 1.0f, 1.0f}});
        p.clips.push_back(c);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        // rgb (50,100,100) -> BGRA (50, 100, 100, 255)
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 50, 100, 100, 255));
    }
}

// ------------------------------------------------------------------
// Suite 3: posterize — {levels}; out = round(in*(L-1)/255)*255/(L-1)
// ------------------------------------------------------------------
void TestPosterize() {
    // levels=2 snaps mid values to the nearer rail: 64 -> 0, 200 ->
    // 255, 128 -> 255 (round(0.502)=1).
    {
        Project p = MakeCanvasProject();
        p.tracks.push_back(Track{TrackKind::Video, "V2"});
        Clip dark = MakeGeneratedClip("d", 0, 0.0, 1.0,
                                      SourceKind::ColorMatte,
                                      "#404040");
        dark.effects.push_back(ClipEffect{"posterize", {2.0f}});
        p.clips.push_back(dark);
        Clip light = MakeGeneratedClip("l", 1, 0.0, 1.0,
                                       SourceKind::ColorMatte,
                                       "#C8C8C8");
        light.effects.push_back(ClipEffect{"posterize", {2.0f}});
        light.transform.offset_x = 0.5f;   // right half
        p.clips.push_back(light);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 4, 8, 0, 0, 0, 255));
        XER_VE_CHECK(VecPxIs(out, 16, 12, 8, 255, 255, 255, 255));
    }

    // levels=4 on 200: round(200*3/255)=round(2.35)=2 -> 170.
    {
        Project p = MakeCanvasProject();
        Clip c = MakeGeneratedClip("m", 0, 0.0, 1.0,
                                   SourceKind::ColorMatte, "#C8C8C8");
        c.effects.push_back(ClipEffect{"posterize", {4.0f}});
        p.clips.push_back(c);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxNear(out, 16, 8, 8, 170, 170, 170, 255, 2));
    }
}

// ------------------------------------------------------------------
// Suite 4: ClipTextStyle parsing — back-compat + inline tokens
// ------------------------------------------------------------------
void TestClipTextStyleParse() {
    // No '|': the whole param is the text, every field stays default
    // (this is the pre-style behavior).
    {
        const ClipTextStyle s = ParseClipTextStyle("Hello World");
        XER_VE_CHECK(s.text == "Hello World");
        XER_VE_CHECK(s.size_px == 0);
        XER_VE_CHECK(s.align == ClipTextStyle::Align::Left);
        XER_VE_CHECK(s.outline_px == 0.0f);
        XER_VE_CHECK(s.color[0] == 255 && s.color[1] == 255 &&
                     s.color[2] == 255 && s.color[3] == 255);
    }

    // Full style: text before '|' is r-trimmed, every token maps.
    {
        const ClipTextStyle s = ParseClipTextStyle(
            "Hi  | size=42 color=#FF0000 align=right outline=2 "
            "outline_color=#00FF00");
        XER_VE_CHECK(s.text == "Hi");
        XER_VE_CHECK(s.size_px == 42);
        XER_VE_CHECK(s.align == ClipTextStyle::Align::Right);
        XER_VE_CHECK(s.outline_px == 2.0f);
        // #FF0000 -> BGRA {0,0,255,255}; #00FF00 -> {0,255,0,255}
        XER_VE_CHECK(s.color[0] == 0 && s.color[1] == 0 &&
                     s.color[2] == 255 && s.color[3] == 255);
        XER_VE_CHECK(s.outline_color[0] == 0 &&
                     s.outline_color[1] == 255 &&
                     s.outline_color[2] == 0 &&
                     s.outline_color[3] == 255);
    }

    // Center align + case-insensitive keys.
    {
        const ClipTextStyle s =
            ParseClipTextStyle("x|SIZE=14 ALIGN=center");
        XER_VE_CHECK(s.text == "x");
        XER_VE_CHECK(s.size_px == 14);
        XER_VE_CHECK(s.align == ClipTextStyle::Align::Center);
    }

    // Unknown keys and malformed values are ignored, not fatal.
    {
        const ClipTextStyle s =
            ParseClipTextStyle("Edge | bogus=1 size=abc align=nope");
        XER_VE_CHECK(s.text == "Edge");
        XER_VE_CHECK(s.size_px == 0);
        XER_VE_CHECK(s.align == ClipTextStyle::Align::Left);
    }
}

// ------------------------------------------------------------------
// Suite 5: styled text rendering via GenerateSourceFrame
// ------------------------------------------------------------------
void TestTextStyleRender() {
#if defined(XER_VE_HAS_SKIA)
    // Skia rasterizes a real system font — geometry is font-metric
    // dependent, so every case asserts structure, not coordinates.

    // Baseline "A": non-trivial frame, white face ink, transparent
    // pad corner, and a partial-alpha blurred drop shadow.
    {
        Clip c = MakeGeneratedClip("t", 0, 0.0, 1.0, SourceKind::Text,
                                   "A");
        video::Frame f;
        XER_VE_CHECK(GenerateSourceFrame(c, 0.0, 16, 16, &f));
        XER_VE_CHECK(f.Width() >= 7 && f.Height() >= 9);
        XER_VE_CHECK(FrameHasPxNear(f, 255, 255, 255, 255, 30));
        XER_VE_CHECK(FramePx(f, 0, 0)[3] == 0);
        XER_VE_CHECK(FrameHasShadowPx(f));
    }

    // size=21: a ~21px face plus padding must exceed both the glyph
    // height and the default-size raster.
    {
        Clip c = MakeGeneratedClip("t", 0, 0.0, 1.0, SourceKind::Text,
                                   "A | size=21");
        video::Frame f;
        XER_VE_CHECK(GenerateSourceFrame(c, 0.0, 16, 16, &f));
        XER_VE_CHECK(f.Height() >= 21 && f.Width() >= 10);
        XER_VE_CHECK(FrameHasPxNear(f, 255, 255, 255, 255, 30));
    }

    // color=#FF0000 tints the face red (BGRA 0,0,255).
    {
        Clip c = MakeGeneratedClip("t", 0, 0.0, 1.0, SourceKind::Text,
                                   "A | color=#FF0000");
        video::Frame f;
        XER_VE_CHECK(GenerateSourceFrame(c, 0.0, 16, 16, &f));
        XER_VE_CHECK(FrameHasPxNear(f, 0, 0, 255, 255, 30));
    }

    // align=right shifts the short second line rightward inside the
    // block: the lower half's leftmost ink column moves right.
    {
        Clip left = MakeGeneratedClip("l", 0, 0.0, 1.0,
                                      SourceKind::Text, "AA\nA");
        Clip right = MakeGeneratedClip("r", 0, 0.0, 1.0,
                                       SourceKind::Text,
                                       "AA\nA | align=right");
        video::Frame fl;
        video::Frame fr;
        XER_VE_CHECK(GenerateSourceFrame(left, 0.0, 16, 16, &fl));
        XER_VE_CHECK(GenerateSourceFrame(right, 0.0, 16, 16, &fr));
        const int lx = MinWhiteInkXLowerHalf(fl);
        const int rx = MinWhiteInkXLowerHalf(fr);
        XER_VE_CHECK(lx >= 0 && rx > lx);
    }

    // outline=1 replaces the implicit shadow with a stroke: outline-
    // color ink and fill-color ink are both present, the dark
    // partial-alpha shadow pixels are gone, and the pad stays empty.
    {
        Clip c = MakeGeneratedClip(
            "t", 0, 0.0, 1.0, SourceKind::Text,
            "A | color=#FF0000 outline=1 outline_color=#00FF00");
        video::Frame f;
        XER_VE_CHECK(GenerateSourceFrame(c, 0.0, 16, 16, &f));
        XER_VE_CHECK(f.Width() >= 7 && f.Height() >= 9);
        XER_VE_CHECK(FrameHasPxNear(f, 0, 255, 0, 255, 60));   // ring
        XER_VE_CHECK(FrameHasPxNear(f, 0, 0, 255, 255, 60));   // face
        XER_VE_CHECK(!FrameHasShadowPx(f));                    // no shadow
        XER_VE_CHECK(FramePx(f, 0, 0)[3] == 0);                // empty pad
    }
#else   // !XER_VE_HAS_SKIA — BitmapFont raster, glyph-exact geometry.
    // Baseline (unchanged back-compat path): "A" at canvas 16 ->
    // glyph scale 1 -> 5x7 glyph + 2px shadow tail -> 7x9 frame.
    // 'A' row 0 (0x0E) sets columns 1..3 -> white face at (1,0).
    {
        Clip c = MakeGeneratedClip("t", 0, 0.0, 1.0, SourceKind::Text,
                                   "A");
        video::Frame f;
        XER_VE_CHECK(GenerateSourceFrame(c, 0.0, 16, 16, &f));
        XER_VE_CHECK(f.Width() == 7 && f.Height() == 9);
        XER_VE_CHECK(FramePxIs(f, 1, 0, 255, 255, 255, 255));
        // Near-shadow tap survives: 'A' row1 col2 is unset in the
        // face but the near tap's row0 col2 lands at (3,1) -> a120.
        XER_VE_CHECK(FramePxIs(f, 3, 1, 0, 0, 0, 120));
    }

    // size=21 -> scale round(21/7)=3 -> glyph 15x21 + 6px shadow.
    {
        Clip c = MakeGeneratedClip("t", 0, 0.0, 1.0, SourceKind::Text,
                                   "A | size=21");
        video::Frame f;
        XER_VE_CHECK(GenerateSourceFrame(c, 0.0, 16, 16, &f));
        XER_VE_CHECK(f.Width() == 21 && f.Height() == 27);
    }

    // color=#FF0000 tints the face red (BGRA 0,0,255).
    {
        Clip c = MakeGeneratedClip("t", 0, 0.0, 1.0, SourceKind::Text,
                                   "A | color=#FF0000");
        video::Frame f;
        XER_VE_CHECK(GenerateSourceFrame(c, 0.0, 16, 16, &f));
        XER_VE_CHECK(FramePxIs(f, 1, 0, 0, 0, 255, 255));
    }

    // align=right shifts a short line inside the block: "AA\nA" at
    // scale 1 -> block 11 wide, "A" is 5 -> xoff 6. Left-aligned the
    // second line's row-1 col-0 pixel lands at (0,10); right-aligned
    // it lands at (6,10) and (0,10) stays empty.
    {
        Clip left = MakeGeneratedClip("l", 0, 0.0, 1.0,
                                      SourceKind::Text, "AA\nA");
        Clip right = MakeGeneratedClip("r", 0, 0.0, 1.0,
                                       SourceKind::Text,
                                       "AA\nA | align=right");
        video::Frame fl;
        video::Frame fr;
        XER_VE_CHECK(GenerateSourceFrame(left, 0.0, 16, 16, &fl));
        XER_VE_CHECK(GenerateSourceFrame(right, 0.0, 16, 16, &fr));
        // 'A' row 1 (0x11) sets columns 0 and 4.
        XER_VE_CHECK(FramePxIs(fl, 0, 10, 255, 255, 255, 255));
        XER_VE_CHECK(FramePx(fr, 0, 10)[3] == 0);
        XER_VE_CHECK(FramePxIs(fr, 6, 10, 255, 255, 255, 255));
    }

    // outline=1 replaces the shadow with a 1px stroke: the ring pixel
    // above the face is the outline color, the face stays its color,
    // and the far corners the old shadow reached stay transparent.
    {
        Clip c = MakeGeneratedClip(
            "t", 0, 0.0, 1.0, SourceKind::Text,
            "A | color=#FF0000 outline=1 outline_color=#00FF00");
        video::Frame f;
        XER_VE_CHECK(GenerateSourceFrame(c, 0.0, 16, 16, &f));
        XER_VE_CHECK(f.Width() == 7 && f.Height() == 9);   // 5+2, 7+2
        XER_VE_CHECK(FramePxIs(f, 2, 0, 0, 255, 0, 255));   // ring
        XER_VE_CHECK(FramePxIs(f, 2, 1, 0, 0, 255, 255));   // face
        XER_VE_CHECK(FramePx(f, 0, 0)[3] == 0);             // no shadow
    }
#endif  // XER_VE_HAS_SKIA
}

// ------------------------------------------------------------------
// Suite 6: adjustment layers (Clip::is_adjustment)
// ------------------------------------------------------------------
void TestAdjustmentLayer() {
    // V1 red matte -> V2 adjustment (color_balance lift -0.5 ->
    // darkens whatever is below) -> V3 green matte covering the right
    // half. Left half must show DARKENED red (200 -> 73), right half
    // shows the untouched green — effects must not reach upward.
    {
        Project p = MakeCanvasProject();
        p.tracks.push_back(Track{TrackKind::Video, "V2"});
        p.tracks.push_back(Track{TrackKind::Video, "V3"});
        p.clips.push_back(MakeGeneratedClip(
            "under", 0, 0.0, 2.0, SourceKind::ColorMatte, "#C80000"));
        Clip adj = MakeGeneratedClip("adj", 1, 0.0, 1.0,
                                     SourceKind::ColorMatte, "#FFFFFF");
        adj.is_adjustment = true;
        adj.effects.push_back(ClipEffect{
            "color_balance",
            {-0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f}});
        p.clips.push_back(adj);
        Clip top = MakeGeneratedClip("top", 2, 0.0, 2.0,
                                     SourceKind::ColorMatte, "#00FF00");
        top.transform.offset_x = 0.5f;   // right half only
        p.clips.push_back(top);

        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        // Left: red darkened by the adjustment -> rgb(0,0,73).
        XER_VE_CHECK(VecPxIs(out, 16, 4, 8, 0, 0, 73, 255));
        // Right: the clip ABOVE the adjustment is untouched.
        XER_VE_CHECK(VecPxIs(out, 16, 12, 8, 0, 255, 0, 255));
    }

    // The adjustment only applies inside its own active span: past
    // its end the under-clip renders at full brightness again.
    {
        Project p = MakeCanvasProject();
        p.tracks.push_back(Track{TrackKind::Video, "V2"});
        p.clips.push_back(MakeGeneratedClip(
            "under", 0, 0.0, 2.0, SourceKind::ColorMatte, "#C80000"));
        Clip adj = MakeGeneratedClip("adj", 1, 0.0, 1.0,
                                     SourceKind::ColorMatte, "#FFFFFF");
        adj.is_adjustment = true;
        adj.effects.push_back(ClipEffect{
            "color_balance",
            {-0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f}});
        p.clips.push_back(adj);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 0, 0, 73, 255));
        XER_VE_CHECK(rig.comp->RenderFrameAt(1.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 8, 8, 0, 0, 200, 255));
    }

    // The adjustment's own transform rect limits coverage: offset
    // 0.5 confines the effect to the right half.
    {
        Project p = MakeCanvasProject();
        p.tracks.push_back(Track{TrackKind::Video, "V2"});
        p.clips.push_back(MakeGeneratedClip(
            "under", 0, 0.0, 1.0, SourceKind::ColorMatte, "#C80000"));
        Clip adj = MakeGeneratedClip("adj", 1, 0.0, 1.0,
                                     SourceKind::ColorMatte, "#FFFFFF");
        adj.is_adjustment = true;
        adj.transform.offset_x = 0.5f;
        adj.effects.push_back(ClipEffect{
            "color_balance",
            {-0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f}});
        p.clips.push_back(adj);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxIs(out, 16, 4, 8, 0, 0, 200, 255));   // untouched
        XER_VE_CHECK(VecPxIs(out, 16, 12, 8, 0, 0, 73, 255));   // darkened
    }

    // opacity < 1 on the adjustment fades the effect, not the clip:
    // opacity 0.5 blends darkened-over-original ~ halfway -> ~136.
    {
        Project p = MakeCanvasProject();
        p.tracks.push_back(Track{TrackKind::Video, "V2"});
        p.clips.push_back(MakeGeneratedClip(
            "under", 0, 0.0, 1.0, SourceKind::ColorMatte, "#C80000"));
        Clip adj = MakeGeneratedClip("adj", 1, 0.0, 1.0,
                                     SourceKind::ColorMatte, "#FFFFFF");
        adj.is_adjustment = true;
        adj.transform.opacity = 0.5f;
        adj.effects.push_back(ClipEffect{
            "color_balance",
            {-0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 1.0f}});
        p.clips.push_back(adj);
        Rig rig = MakeRig(p);
        XER_VE_CHECK(rig.init_ok);
        std::vector<std::uint8_t> out;
        std::string err;
        XER_VE_CHECK(rig.comp->RenderFrameAt(0.5, &out, &err));
        XER_VE_CHECK(VecPxNear(out, 16, 8, 8, 0, 0, 136, 255, 4));
    }
}

// ------------------------------------------------------------------
// Suite 7: "adjustment" serialization — v2 round-trip + back-compat
// ------------------------------------------------------------------
void TestAdjustmentSerialization() {
    const std::string dir =
        Path::ToUtf8(XER::VideoEditor::Test::TempDir());
    std::string err;

    // is_adjustment=true survives a save/load round-trip.
    {
        Project p = MakeCanvasProject();
        Clip c = MakeGeneratedClip("adj", 0, 0.0, 1.0,
                                   SourceKind::ColorMatte, "#FFFFFF");
        c.is_adjustment = true;
        p.clips.push_back(c);
        const std::string path = dir + "/adjustment_on.xerproj";
        XER_VE_CHECK(ProjectStore::Save(p, path, &err));
        Project loaded;
        XER_VE_CHECK(ProjectStore::Load(path, &loaded, &err));
        XER_VE_CHECK(loaded.clips.size() == 1);
        if (loaded.clips.size() == 1) {
            XER_VE_CHECK(loaded.clips[0].is_adjustment);
        }
        // The key is actually written (only when true).
        std::ifstream in(Path::FromUtf8(path), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        XER_VE_CHECK(text.find("\"adjustment\":true") !=
                     std::string::npos);
    }

    // Back-compat: a file with NO "adjustment" key loads as false —
    // exercised both ways (a default clip omits the key on save, and
    // Load defaults it).
    {
        Project p = MakeCanvasProject();
        Clip c = MakeGeneratedClip("plain", 0, 0.0, 1.0,
                                   SourceKind::ColorMatte, "#FFFFFF");
        p.clips.push_back(c);   // is_adjustment stays false
        const std::string path = dir + "/adjustment_off.xerproj";
        XER_VE_CHECK(ProjectStore::Save(p, path, &err));
        std::ifstream in(Path::FromUtf8(path), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        XER_VE_CHECK(text.find("\"adjustment\"") == std::string::npos);
        Project loaded;
        XER_VE_CHECK(ProjectStore::Load(path, &loaded, &err));
        XER_VE_CHECK(loaded.clips.size() == 1);
        if (loaded.clips.size() == 1) {
            XER_VE_CHECK(!loaded.clips[0].is_adjustment);
        }
    }
}

}  // namespace

int XER_VideoEditor_RunCompositor2Tests() {
    TestChromaKey();
    TestColorBalance();
    TestPosterize();
    TestClipTextStyleParse();
    TestTextStyleRender();
    TestAdjustmentLayer();
    TestAdjustmentSerialization();
    return 0;
}
