// Plugin-host suite — runtime DLL effect registry
// (tools/video_editor/plugin_host.{h,cc}). Covers the PluginDesc
// guard rails (ApplyVideo/ApplyAudio refuse null entry points /
// buffers), Find/OfKind on an empty host, and SharedPluginHost()
// end-to-end: the host scans <exe>/plugins/*.dll, so coverage of the
// loaded effects depends on whether the video_editor_plugins target
// has been built into bin/plugins — every loaded-effect case below
// skips silently when the directory is empty (non-Windows hosts never
// produce a list; ListPluginDlls only scans on _WIN32). Deterministic
// content checks run only for known ids ("xer.fx.invert_lum",
// "xer.af.bitcrusher"). NOT registered in tests/CMakeLists.txt — the
// integration patch wires it into XER_VideoEditor_Tests
// (RunPluginHostTests + add the source file).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "video_editor_test_util.h"
#include "plugin_host.h"

namespace {
using XER::VideoEditor::PluginDesc;
using XER::VideoEditor::PluginHost;

// Tight 64x64 BGRA8 buffer filled with a single color; stride ==
// width*4 (no padding) so every plugin's pixel loop covers it fully.
std::vector<std::uint8_t> MakeSolidBgra(int w, int h,
                                        std::uint8_t gray,
                                        std::uint8_t alpha = 255) {
    std::vector<std::uint8_t> px(
        static_cast<std::size_t>(w) * h * 4u);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = gray;
        px[i + 1] = gray;
        px[i + 2] = gray;
        px[i + 3] = alpha;
    }
    return px;
}

// Params for `desc` populated with each declared default — safer than
// an empty vector for effects whose param(0) default isn't the
// no-op value.
std::vector<float> DefaultParams(const PluginDesc& desc) {
    std::vector<float> p;
    p.reserve(desc.params.size());
    for (const XER_ParamDesc& pd : desc.params) {
        p.push_back(pd.default_value);
    }
    return p;
}

bool BuffersDiffer(const std::vector<std::uint8_t>& a,
                   const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) return true;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) return true;
    }
    return false;
}

// ------------------------------------------------------------------
// Cases
// ------------------------------------------------------------------

// ApplyVideo/ApplyAudio must refuse a PluginDesc with no entry point
// instead of calling through a null function pointer.
void TestApplyGuardsNullFn() {
    PluginDesc empty;   // video_fn == nullptr, audio_fn == nullptr
    auto frame = MakeSolidBgra(64, 64, 128);
    XER_VE_CHECK(!PluginHost::ApplyVideo(
        empty, frame.data(), 64, 64, 64 * 4, {}));
    float pcm[8] = {};
    XER_VE_CHECK(!PluginHost::ApplyAudio(empty, pcm, 4, 2, 48000, {}));
}

// ApplyVideo must also refuse a null pixel buffer even when the desc
// carries a callable video_fn.
void TestApplyGuardsNullBuffer() {
    PluginDesc desc;
    desc.video_fn =
        +[](XER_VideoFrame* f, const float*, int32_t) -> int {
            return f == nullptr ? 1 : 0;
        };
    XER_VE_CHECK(!PluginHost::ApplyVideo(desc, nullptr, 64, 64,
                                         64 * 4, {}));
}

// A default-constructed host holds no effects.
void TestEmptyHost() {
    PluginHost host;
    XER_VE_CHECK(host.Effects().empty());
    XER_VE_CHECK(host.Find("xer.fx.invert_lum") == nullptr);
    XER_VE_CHECK(host.Find("") == nullptr);
    XER_VE_CHECK(host.OfKind(XER_PLUGIN_VIDEO_FX).empty());
    XER_VE_CHECK(host.OfKind(XER_PLUGIN_AUDIO_FX).empty());
}

// SharedPluginHost() scans <exe>/plugins once — must not crash, and
// Effects() must be a usable list. Count is logged, not asserted:
// the list is legitimately empty when plugins were never deployed
// next to the test exe (or off Windows, where the scan is a stub).
void TestSharedHostLoads() {
    PluginHost& host = XER::VideoEditor::SharedPluginHost();
    const auto& effects = host.Effects();
    std::printf(
        "plugin_host_tests: SharedPluginHost registered %zu "
        "effect(s)\n",
        effects.size());
    for (const PluginDesc& d : effects) {
        std::printf("  - %s (%s, kind=%d, file=%s)\n", d.id.c_str(),
                    d.name.c_str(), static_cast<int>(d.kind),
                    d.file.c_str());
        // Every registered effect has a non-empty id, and Find can
        // round-trip it back to the same desc.
        XER_VE_CHECK(!d.id.empty());
        XER_VE_CHECK(host.Find(d.id) == &d);
    }
    // Find still rejects ids that were never registered.
    XER_VE_CHECK(host.Find("xer.fx.does_not_exist") == nullptr);
    // The "plugin:"-prefixed clip-effect lookup agrees with Find.
    XER_VE_CHECK(PluginHost::ForEffectType("plugin:bogus") == nullptr);
    if (!effects.empty()) {
        const PluginDesc* d =
            PluginHost::ForEffectType("plugin:" + effects.front().id);
        XER_VE_CHECK(d == &effects.front());
    }
}

// If any video effect loaded, the first one must apply cleanly to a
// real frame. Content is NOT asserted — spatial effects (distort,
// blur radius 0, ...) may legitimately leave mid-gray pixels alone.
void TestFirstVideoEffectApplies() {
    const auto video_fx =
        XER::VideoEditor::SharedPluginHost().OfKind(
            XER_PLUGIN_VIDEO_FX);
    if (video_fx.empty()) {
        std::printf(
            "plugin_host_tests: no video fx loaded — skipping "
            "apply check\n");
        return;
    }
    const PluginDesc* desc = video_fx.front();
    XER_VE_CHECK(desc->video_fn != nullptr);
    auto frame = MakeSolidBgra(64, 64, 128);
    XER_VE_CHECK(PluginHost::ApplyVideo(*desc, frame.data(), 64, 64,
                                        64 * 4,
                                        DefaultParams(*desc)));
}

// invert_lum is deterministic: mid-gray 128 has luma 128 -> shift
// 255-256 == -1 at amount 1, so every channel lands on 127. Any
// pixel still at 128 means the plugin didn't run.
void TestInvertLumChangesGray() {
    const PluginDesc* desc = XER::VideoEditor::SharedPluginHost().Find(
        "xer.fx.invert_lum");
    if (desc == nullptr) {
        std::printf(
            "plugin_host_tests: xer.fx.invert_lum not loaded — "
            "skipping\n");
        return;
    }
    auto frame = MakeSolidBgra(64, 64, 128);
    const auto before = frame;
    XER_VE_CHECK(PluginHost::ApplyVideo(*desc, frame.data(), 64, 64,
                                        64 * 4, {1.0f}));
    XER_VE_CHECK(BuffersDiffer(before, frame));
    // And specifically the expected direction: gray moved toward
    // inverted (alpha untouched).
    XER_VE_CHECK(frame[0] != 128 && frame[3] == 255);
}

// bitcrusher (8-bit quantize + 4x sample-hold defaults) must alter a
// 256-frame stereo sine at 48 kHz.
void TestBitcrusherChangesAudio() {
    const PluginDesc* desc = XER::VideoEditor::SharedPluginHost().Find(
        "xer.af.bitcrusher");
    if (desc == nullptr) {
        std::printf(
            "plugin_host_tests: xer.af.bitcrusher not loaded — "
            "skipping\n");
        return;
    }
    XER_VE_CHECK(desc->kind == XER_PLUGIN_AUDIO_FX);
    XER_VE_CHECK(desc->audio_fn != nullptr);
    constexpr int kFrames = 256;
    constexpr int kChannels = 2;
    constexpr int kRate = 48000;
    constexpr double kPi = 3.14159265358979323846;
    std::vector<float> pcm(
        static_cast<std::size_t>(kFrames) * kChannels);
    for (int i = 0; i < kFrames; ++i) {
        const float s = 0.5f * static_cast<float>(
            std::sin(2.0 * kPi * 440.0 * i / kRate));
        pcm[i * 2 + 0] = s;
        pcm[i * 2 + 1] = s;
    }
    const auto before = pcm;
    XER_VE_CHECK(PluginHost::ApplyAudio(
        *desc, pcm.data(), kFrames, kChannels, kRate,
        DefaultParams(*desc)));
    bool differs = false;
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        if (pcm[i] != before[i]) {
            differs = true;
            break;
        }
    }
    XER_VE_CHECK(differs);
}

}  // namespace

int XER_VideoEditor_RunPluginHostTests() {
    TestApplyGuardsNullFn();
    TestApplyGuardsNullBuffer();
    TestEmptyHost();
    TestSharedHostLoads();
    TestFirstVideoEffectApplies();
    TestInvertLumChangesGray();
    TestBitcrusherChangesAudio();
    return XER::VideoEditor::Test::Failures();
}
