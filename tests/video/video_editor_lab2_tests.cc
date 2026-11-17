// Lab-2 suite — mask refinement (tools/video_editor/lab/mask_refine),
// eyedropper helpers (lab/color_pick) and VST3 param metadata/preset
// strings (vst/vst3_params). Synthetic masks + tiny BGRA8 frames
// only; no decoders, no files, no real VST3 plugins (a minimal
// IPluginInstance fake covers SetParamByName).
//
// NOTE: intentionally NOT registered in tests/CMakeLists.txt /
// video_editor_test_main.cc yet — same parallel-workstream convention
// as video_editor_lab_tests.cc. Wire in as
// XER_VideoEditor_RunLab2Tests() when the parent integrates.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "video_editor_test_util.h"
#include "lab/color_pick.h"
#include "lab/mask_refine.h"
#include "timeline_model.h"
#include "vst/plugin_instance.h"
#include "vst/vst3_params.h"
#include "video/frame.h"

namespace {

namespace Lab = XER::VideoEditor::Lab;
namespace Vst = XER::VideoEditor::Vst;
using XER::VideoEditor::ClipAudioFx;

// w*h float mask with a filled `block` rect (x0,y0)-(x1,y1) inclusive.
std::vector<float> BlockMask(int w, int h, int x0, int y0, int x1,
                             int y1, float in = 1.0f, float out = 0.0f) {
    std::vector<float> m(static_cast<std::size_t>(w) * h, out);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (x >= x0 && x <= x1 && y >= y0 && y <= y1) {
                m[static_cast<std::size_t>(y) * w + x] = in;
            }
        }
    }
    return m;
}

struct Bgra {
    std::uint8_t b, g, r, a;
};

void SetPx(video::Frame& f, int x, int y, Bgra c) {
    std::uint8_t* p =
        f.Data() + (static_cast<std::size_t>(y) * f.Width() + x) * 4;
    p[0] = c.b;
    p[1] = c.g;
    p[2] = c.r;
    p[3] = c.a;
}

Bgra GetPx(const video::Frame& f, int x, int y) {
    const std::uint8_t* p =
        f.Data() + (static_cast<std::size_t>(y) * f.Width() + x) * 4;
    return {p[0], p[1], p[2], p[3]};
}

// Minimal IPluginInstance for param routing — SetParameter accepts
// any id and logs it, like the vst3 suite's FakePlugin.
class FakePlugin final : public Vst::IPluginInstance {
public:
    std::map<std::uint32_t, float> params;
    bool Process(const float* input, float* output,
                 std::uint32_t frames) override {
        std::memcpy(output, input,
                    static_cast<std::size_t>(frames) * 2 * sizeof(float));
        return true;
    }
    bool SetParameter(std::uint32_t id, float value) override {
        params[id] = value;
        return true;
    }
    std::string Name() const override { return "lab2-fake"; }
};

// ------------------------------------------------------------------
// Mask refinement
// ------------------------------------------------------------------

void TestFeather() {
    const int w = 16, h = 16;
    std::vector<float> m = BlockMask(w, h, 4, 4, 11, 11);
    const std::vector<float> orig = m;

    // radius 0 -> exact no-op.
    std::vector<float> noop = m;
    Lab::FeatherMaskInPlace(noop, w, h, 0.0f);
    XER_VE_CHECK(noop == orig);
    // Bad size -> no-op.
    Lab::FeatherMaskInPlace(noop, w, h + 1, 3.0f);
    XER_VE_CHECK(noop == orig);

    Lab::FeatherMaskInPlace(m, w, h, 2.0f);
    const auto at = [&](int x, int y) {
        return m[static_cast<std::size_t>(y) * w + x];
    };
    // Deep interior alpha preserved; far corner untouched.
    XER_VE_CHECK(std::fabs(at(8, 8) - 1.0f) < 1e-3f);
    XER_VE_CHECK(at(0, 0) == 0.0f);
    // Just outside the block edge the mask now slopes in.
    const float edge_out = at(3, 8);    // 1 px outside x0
    const float edge_in = at(4, 8);     // 1 px inside
    const float far_out = at(1, 8);     // beyond the kernel reach
    XER_VE_CHECK(edge_out > 0.0f && edge_out < edge_in);
    XER_VE_CHECK(edge_in < 1.0f);
    XER_VE_CHECK(far_out == 0.0f);
    // Every value still in range.
    for (float v : m) {
        XER_VE_CHECK(v >= 0.0f && v <= 1.0f);
    }
}

void TestGrow() {
    const int w = 16, h = 16;
    const auto at = [](const std::vector<float>& m, int x, int y) {
        return m[static_cast<std::size_t>(y) * w + x];
    };

    // Dilate +2: the edge moves 2 px outward.
    std::vector<float> grow = BlockMask(w, h, 4, 4, 11, 11);
    const std::vector<float> orig = grow;
    Lab::GrowMaskInPlace(grow, w, h, 2.0f);
    XER_VE_CHECK(at(grow, 2, 8) == 1.0f);   // 2 px left of x0: grown
    XER_VE_CHECK(at(grow, 13, 8) == 1.0f);  // 2 px right of x1
    XER_VE_CHECK(at(grow, 1, 8) == 0.0f);   // 3 px out: still empty
    XER_VE_CHECK(at(grow, 8, 2) == 1.0f);   // 2 px above y0
    // Diagonals of a square max-filter reach too (square kernel).
    XER_VE_CHECK(at(grow, 2, 2) == 1.0f);

    // Erode -2: the edge moves 2 px inward.
    std::vector<float> shrink = BlockMask(w, h, 4, 4, 11, 11);
    Lab::GrowMaskInPlace(shrink, w, h, -2.0f);
    XER_VE_CHECK(at(shrink, 4, 8) == 0.0f);   // eroded edge
    XER_VE_CHECK(at(shrink, 5, 8) == 0.0f);   // 1 inside: still eroded
    XER_VE_CHECK(at(shrink, 6, 8) == 1.0f);   // 2 inside: survives
    XER_VE_CHECK(at(shrink, 9, 8) == 1.0f);   // center survives
    XER_VE_CHECK(at(shrink, 0, 0) == 0.0f);

    // radius 0 -> no-op; bad size -> no-op.
    std::vector<float> noop = orig;
    Lab::GrowMaskInPlace(noop, w, h, 0.0f);
    XER_VE_CHECK(noop == orig);
    Lab::GrowMaskInPlace(noop, w + 1, h, 2.0f);
    XER_VE_CHECK(noop == orig);
}

void TestEdgeContrast() {
    const int w = 9, h = 1;
    // Linear ramp 0 .. 1 across the row.
    std::vector<float> ramp(static_cast<std::size_t>(w));
    for (int x = 0; x < w; ++x) {
        ramp[static_cast<std::size_t>(x)] = x / 8.0f;
    }
    const std::vector<float> orig = ramp;

    // contrast 0 -> no-op.
    Lab::EdgeContrastInPlace(ramp, w, h, 0.0f);
    XER_VE_CHECK(ramp == orig);

    // Positive contrast: below 0.5 drops, above rises, 0.5 pinned.
    std::vector<float> hard = orig;
    Lab::EdgeContrastInPlace(hard, w, h, 0.5f);
    XER_VE_CHECK(hard[1] < orig[1]);                 // 0.125 -> lower
    XER_VE_CHECK(hard[7] > orig[7]);                 // 0.875 -> higher
    XER_VE_CHECK(std::fabs(hard[4] - 0.5f) < 1e-6f); // midpoint pinned
    XER_VE_CHECK(hard[0] >= 0.0f && hard[8] <= 1.0f);// clamped

    // Negative contrast: pulled toward 0.5.
    std::vector<float> soft = orig;
    Lab::EdgeContrastInPlace(soft, w, h, -0.5f);
    XER_VE_CHECK(soft[1] > orig[1] && soft[1] < 0.5f);
    XER_VE_CHECK(soft[7] < orig[7] && soft[7] > 0.5f);
}

void TestRefineCompositeAndUint8() {
    const int w = 16, h = 16;
    // uint8 mask — object_ops-shaped. Refine: grow a bit, feather a
    // bit, harden. Must stay in [0,255] and actually change coverage.
    std::vector<std::uint8_t> m(static_cast<std::size_t>(w) * h, 0);
    for (int y = 4; y <= 11; ++y) {
        for (int x = 4; x <= 11; ++x) {
            m[static_cast<std::size_t>(y) * w + x] = 255;
        }
    }
    const std::vector<std::uint8_t> orig = m;
    Lab::RefineMaskEdgeInPlace(m, w, h, /*shift=*/2.0f,
                               /*feather=*/1.5f, /*contrast=*/0.3f);
    XER_VE_CHECK(m != orig);
    // Grown coverage: a pixel 2 px outside the original block is on.
    XER_VE_CHECK(m[8 * w + 2] > 0);
    // Interior still saturated.
    XER_VE_CHECK(m[8 * w + 8] == 255);

    // All-zero sliders -> bit-exact no-op.
    std::vector<std::uint8_t> noop = orig;
    Lab::RefineMaskEdgeInPlace(noop, w, h, 0.0f, 0.0f, 0.0f);
    XER_VE_CHECK(noop == orig);

    // Feather/grow/contrast uint8 entry points individually.
    std::vector<std::uint8_t> f = orig;
    Lab::FeatherMaskInPlace(f, w, h, 2.0f);
    XER_VE_CHECK(f[8 * w + 3] > 0 && f[8 * w + 3] < f[8 * w + 4]);
    std::vector<std::uint8_t> g = orig;
    Lab::GrowMaskInPlace(g, w, h, 2.0f);
    XER_VE_CHECK(g[8 * w + 2] == 255);
    std::vector<std::uint8_t> c(static_cast<std::size_t>(w) * h, 100);
    Lab::EdgeContrastInPlace(c, w, h, 0.5f);
    XER_VE_CHECK(c[0] < 100);   // below midpoint drops
}

void TestAlphaRoundTrip() {
    video::Frame frame(4, 2, video::PixelFormat::BGRA8);
    const std::uint8_t alphas[8] = {0, 32, 64, 96, 128, 200, 250, 255};
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 4; ++x) {
            SetPx(frame, x, y,
                  Bgra{10, 20, 30, alphas[y * 4 + x]});
        }
    }
    const std::vector<float> alpha = Lab::ExtractAlphaMask(frame);
    XER_VE_CHECK(alpha.size() == 8);
    XER_VE_CHECK(std::fabs(alpha[0] - 0.0f) < 1e-6f);
    XER_VE_CHECK(std::fabs(alpha[7] - 1.0f) < 1e-6f);
    XER_VE_CHECK(std::fabs(alpha[4] - 128.0f / 255.0f) < 1e-6f);

    // Apply to a copy: alpha round-trips byte-exact, BGR survives.
    video::Frame dst(4, 2, video::PixelFormat::BGRA8);
    std::memcpy(dst.Data(), frame.Data(), frame.DataSize());
    // Scramble alpha first so the apply visibly writes.
    for (std::size_t i = 3; i < dst.DataSize(); i += 4) {
        dst.Data()[i] = 0;
    }
    XER_VE_CHECK(Lab::ApplyAlphaMask(dst, alpha));
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 4; ++x) {
            const Bgra p = GetPx(dst, x, y);
            XER_VE_CHECK(p.a == alphas[y * 4 + x]);
            XER_VE_CHECK(p.b == 10 && p.g == 20 && p.r == 30);
        }
    }

    // Rejections: wrong-size mask, non-BGRA frame.
    XER_VE_CHECK(!Lab::ApplyAlphaMask(dst, {0.5f, 0.5f}));
    video::Frame nv12(4, 2, video::PixelFormat::NV12);
    XER_VE_CHECK(Lab::ExtractAlphaMask(nv12).empty());
    XER_VE_CHECK(!Lab::ApplyAlphaMask(nv12, alpha));
}

// ------------------------------------------------------------------
// Color pick
// ------------------------------------------------------------------

void TestSampleRegionAvg() {
    video::Frame f(4, 4, video::PixelFormat::BGRA8);
    // Left half {40,20,10,255}, right half {80,60,50,100}.
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            SetPx(f, x, y,
                  x < 2 ? Bgra{40, 20, 10, 255} : Bgra{80, 60, 50, 100});
        }
    }
    // Single pixel.
    const auto p00 = Lab::SampleRegionAvg(f, 0, 0, 0);
    XER_VE_CHECK(p00[0] == 40 && p00[1] == 20 && p00[2] == 10 &&
                 p00[3] == 255);
    // 3x3 window centered on the seam: columns x=0,1,2 -> 2 left +
    // 1 right columns -> B = (40*2 + 80)/3 = 53.33 -> 53,
    // A = (255*2 + 100)/3 = 203.33 -> 203.
    const auto seam = Lab::SampleRegionAvg(f, 1, 1, 1);
    XER_VE_CHECK(seam[0] == 53 && seam[3] == 203);
    XER_VE_CHECK(seam[1] == 33 && seam[2] == 23);
    // Corner window clamps — 2x2 of the left color only.
    const auto corner = Lab::SampleRegionAvg(f, 0, 0, 1);
    XER_VE_CHECK(corner[0] == 40 && corner[3] == 255);
    // Far outside -> nothing.
    const auto off = Lab::SampleRegionAvg(f, -50, 50, 0);
    XER_VE_CHECK(off[0] == 0 && off[3] == 0);
    // Non-BGRA -> zeros.
    video::Frame nv12(4, 4, video::PixelFormat::NV12);
    XER_VE_CHECK(Lab::SampleRegionAvg(nv12, 0, 0, 0)[3] == 0);
}

void TestColorDistanceAndHex() {
    const std::array<std::uint8_t, 4> a{10, 20, 30, 40};
    XER_VE_CHECK(Lab::ColorDistance(a, a) == 0.0f);
    const std::array<std::uint8_t, 4> b{10, 20, 30 + 100, 40};
    XER_VE_CHECK(std::fabs(Lab::ColorDistance(a, b) - 100.0f) < 1e-3f);
    const std::array<std::uint8_t, 4> black{0, 0, 0, 0};
    const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    XER_VE_CHECK(std::fabs(Lab::ColorDistance(black, white) -
                           std::sqrt(4.0f * 255.0f * 255.0f)) < 0.01f);

    // HexToBgra: RRGGBB string -> {b,g,r,a}.
    const auto c1 = Lab::HexToBgra("#1A2B3C");
    XER_VE_CHECK(c1[0] == 0x3C && c1[1] == 0x2B && c1[2] == 0x1A &&
                 c1[3] == 255);
    const auto c2 = Lab::HexToBgra("#1A2B3C80");
    XER_VE_CHECK(c2[3] == 0x80);
    const auto c3 = Lab::HexToBgra("aabbcc");   // no '#', lowercase
    XER_VE_CHECK(c3[0] == 0xCC && c3[2] == 0xAA);
    // Malformed -> opaque black.
    for (const char* bad : {"", "#12", "zzzzzz", "#12345", "#1234567",
                            "   "}) {
        const auto c = Lab::HexToBgra(bad);
        XER_VE_CHECK(c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] == 255);
    }

    // Round-trip through the canonical formatter.
    XER_VE_CHECK(Lab::BgraToHex(c1) == "#1A2B3CFF");
    XER_VE_CHECK(Lab::BgraToHex(c2) == "#1A2B3C80");
    XER_VE_CHECK(Lab::BgraToHex(c1, false) == "#1A2B3C");
    XER_VE_CHECK(Lab::BgraToHex(Lab::HexToBgra("aabbcc")) ==
                 "#AABBCCFF");
    const auto rt = Lab::HexToBgra(Lab::BgraToHex(a));
    XER_VE_CHECK(rt == a);
}

// ------------------------------------------------------------------
// VST3 params
// ------------------------------------------------------------------

void TestVst3Params() {
    // Scan metadata carries no params — the documented gap.
    Vst::PluginInfo info;
    info.name = "SomePlugin";
    XER_VE_CHECK(Vst::DescribeParams(info).empty());

    // Stored-spec description: sorted by id, synthetic names.
    ClipAudioFx fx;
    fx.plugin_path = "x.vst3";
    fx.params = {{12u, 1.0f}, {7u, 0.5f}};
    const std::vector<Vst::Vst3ParamInfo> rows =
        Vst::DescribeStoredParams(fx);
    XER_VE_CHECK(rows.size() == 2);
    XER_VE_CHECK(rows[0].id == 7 && rows[0].name == "Param 7" &&
                 std::fabs(rows[0].norm_value - 0.5f) < 1e-6f);
    XER_VE_CHECK(rows[1].id == 12 && rows[1].name == "Param 12" &&
                 std::fabs(rows[1].norm_value - 1.0f) < 1e-6f);

    // Format -> canonical "Param 7=0.5;Param 12=1" (shortest floats).
    const std::string text = Vst::FormatParamsString(rows);
    XER_VE_CHECK(text == "Param 7=0.5;Param 12=1");

    // Parse round-trip (plus lenient skipping of junk tokens).
    const auto pairs = Vst::ParseParamsString(
        "Param 7=0.5; ;Param 12=1 ;junk;noeq;=1;bad=oops;Gain=0.25");
    XER_VE_CHECK(pairs.size() == 3);
    XER_VE_CHECK(pairs[0].first == "Param 7" &&
                 std::fabs(pairs[0].second - 0.5f) < 1e-6f);
    XER_VE_CHECK(pairs[1].first == "Param 12" &&
                 std::fabs(pairs[1].second - 1.0f) < 1e-6f);
    XER_VE_CHECK(pairs[2].first == "Gain" &&
                 std::fabs(pairs[2].second - 0.25f) < 1e-6f);

    // A fake (non-enumerating) instance still resolves the synthetic
    // "Param <id>" names and bare ids.
    FakePlugin fake;
    XER_VE_CHECK(Vst::DescribeParams(&fake).empty());   // interface gap
    XER_VE_CHECK(Vst::SetParamByName(&fake, "Param 7", 0.75f));
    XER_VE_CHECK(std::fabs(fake.params[7] - 0.75f) < 1e-6f);
    XER_VE_CHECK(Vst::SetParamByName(&fake, "param_12", 0.25f));
    XER_VE_CHECK(std::fabs(fake.params[12] - 0.25f) < 1e-6f);
    XER_VE_CHECK(Vst::SetParamByName(&fake, "12", 0.5f));
    XER_VE_CHECK(std::fabs(fake.params[12] - 0.5f) < 1e-6f);
    // Values clamp to [0,1]; unresolvable names and nulls fail.
    XER_VE_CHECK(Vst::SetParamByName(&fake, "Param 3", 4.0f));
    XER_VE_CHECK(std::fabs(fake.params[3] - 1.0f) < 1e-6f);
    XER_VE_CHECK(!Vst::SetParamByName(&fake, "Drive", 0.5f));
    XER_VE_CHECK(!Vst::SetParamByName(nullptr, "Param 1", 0.5f));

    // Preset string applied to the fake: both params land.
    const std::size_t applied = Vst::ApplyParamsString(
        &fake, "Param 7=0.1;Param 12=0.9;NotAParam=0.5");
    XER_VE_CHECK(applied == 2);
    XER_VE_CHECK(std::fabs(fake.params[7] - 0.1f) < 1e-6f);
    XER_VE_CHECK(std::fabs(fake.params[12] - 0.9f) < 1e-6f);
}

}  // namespace

int XER_VideoEditor_RunLab2Tests() {
    TestFeather();
    TestGrow();
    TestEdgeContrast();
    TestRefineCompositeAndUint8();
    TestAlphaRoundTrip();
    TestSampleRegionAvg();
    TestColorDistanceAndHex();
    TestVst3Params();
    return XER::VideoEditor::Test::Failures();
}
