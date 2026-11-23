// See color_widgets_smoke.h — headless exercise of the color widgets.
#include "color_widgets_smoke.h"

#include <cmath>
#include <memory>
#include <string>

#include "colorspace/oklch.h"
#include "layout/geometry.h"
#include "theme/tokens.h"
#include "widgets/color_picker.h"
#include "widgets/eyedropper.h"
#include "widgets/gradient_editor.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace CS = XER::UI::ColorSpace;
using XER::UI::Layout::Point;
using XER::UI::Theme::Color;

bool Near(float a, float b, float eps = 0.01f) {
    return std::fabs(a - b) <= eps;
}

bool NearColor(Color a, Color b, float eps = 0.01f) {
    return Near(a.r, b.r, eps) && Near(a.g, b.g, eps) &&
           Near(a.b, b.b, eps) && Near(a.a, b.a, eps);
}

W::Event Mouse(W::Event::Type type, float x, float y,
               W::MouseButton btn = W::MouseButton::None) {
    W::Event ev{};
    ev.type = type;
    ev.position = {x, y};
    ev.mouse_button = btn;
    return ev;
}

W::Event Key(W::KeyCode key) {
    W::Event ev{};
    ev.type = W::Event::Type::KeyDown;
    ev.key = key;
    return ev;
}

// ── OKLCH math ────────────────────────────────────────────────────────────
std::string CheckOklch() {
    // Round-trip the primaries + a mid gray: sRGB -> OKLCH -> sRGB.
    const Color samples[] = {
        {1.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {0.5f, 0.5f, 0.5f, 1.0f},
        {0.25f, 0.7f, 0.1f, 0.8f},
    };
    for (const Color& s : samples) {
        const Color rt = CS::ClampSrgb(
            CS::SrgbFromOklch(CS::OklchFromSrgb(s)));
        if (!NearColor(rt, s, 0.01f)) {
            return "sRGB->OKLCH->sRGB round-trip drifted";
        }
    }
    // Gray must be achromatic.
    const CS::Oklch gray = CS::OklchFromSrgb({0.4f, 0.4f, 0.4f, 1.0f});
    if (gray.c > 1e-3f) {
        return "gray produced nonzero OKLCH chroma";
    }
    // Hue sanity: pure red sits near h ~ 29deg in OKLCH.
    const CS::Oklch red = CS::OklchFromSrgb({1.0f, 0.0f, 0.0f, 1.0f});
    if (red.h < 20.0f || red.h > 40.0f || red.l < 0.5f || red.l > 0.7f) {
        return "red OKLCH coordinates out of expected range";
    }
    // Hex grammar: same lenient contract as lab/color_pick.
    const Color hex = CS::ColorFromHex("#3E5CEB80");
    if (!NearColor(hex, {0x3E / 255.0f, 0x5C / 255.0f, 0xEB / 255.0f,
                         0x80 / 255.0f}, 0.005f)) {
        return "hex parse mismatch";
    }
    if (CS::HexFromColor(hex) != "#3E5CEB") {
        return "hex format mismatch";
    }
    if (CS::HexFromColor(hex, true) != "#3E5CEB80") {
        return "hex+alpha format mismatch";
    }
    // Out-of-gamut OKLCH must soft-proof back into gamut.
    const CS::Oklch wild{0.6f, 0.6f, 145.0f, 1.0f};
    if (CS::SrgbInGamut(CS::SrgbFromOklch(wild))) {
        return "test setup: expected out-of-gamut color";
    }
    const CS::Oklch proofed = CS::SoftProofToSrgb(wild);
    if (!CS::SrgbInGamut(CS::SrgbFromOklch(proofed)) ||
        proofed.h != wild.h) {
        return "soft proof failed to land in gamut or moved the hue";
    }
    return {};
}

// ── ColorPicker ───────────────────────────────────────────────────────────
std::string CheckColorPicker() {
    auto picker = std::make_shared<W::ColorPicker>();
    picker->SetColor({1.0f, 0.0f, 0.0f, 1.0f});
    if (!NearColor(picker->GetColor(), {1.0f, 0.0f, 0.0f, 1.0f}, 0.01f)) {
        return "SetColor did not round-trip";
    }

    int live = 0, committed = 0;
    Color last_live{};
    picker->SetOnColorChange([&](Color c) { ++live; last_live = c; });
    picker->SetOnColorCommitted([&](Color) { ++committed; });

    // Preferred-size layout so the chrome rects exist.
    const auto pref = picker->GetPreferredSize();
    picker->Layout({0.0f, 0.0f, pref.width, pref.height},
                   XER::UI::Theme::Tokens{});
    // TextInput children must attach once the widget is shared-owned.
    if (picker->GetChildren().size() != 4) {
        return "picker did not attach its hex/channel inputs";
    }

    // Field is below the tab strip: pad 10 + tabs 26 + gap 8 -> y=44,
    // x=10. A drag inside it must fire live but not committed until up.
    picker->HandleEvent(Mouse(W::Event::Type::MouseDown, 40.0f, 60.0f,
                              W::MouseButton::Left));
    picker->HandleEvent(Mouse(W::Event::Type::MouseMove, 80.0f, 90.0f,
                              W::MouseButton::Left));
    if (live == 0 || committed != 0) {
        return "field drag did not fire live-only callbacks";
    }
    picker->HandleEvent(Mouse(W::Event::Type::MouseUp, 80.0f, 90.0f,
                              W::MouseButton::Left));
    if (committed != 1) {
        return "drag release did not commit exactly once";
    }
    // Commit feeds recents automatically.
    if (picker->Recent().empty()) {
        return "commit did not push a recent color";
    }

    // Mode tabs: top row cell centers (4 cells across inner width).
    const float cell = (pref.width - 20.0f) / 4.0f;
    picker->HandleEvent(Mouse(W::Event::Type::MouseDown,
                              10.0f + cell * 3.5f, 10.0f + 13.0f,
                              W::MouseButton::Left));
    if (picker->GetMode() != W::ColorPicker::Mode::Oklch) {
        return "mode tab did not switch to OKLCH";
    }

    // OKLCH canonical state survives mode hops.
    const CS::Oklch before = picker->GetColorOklch();
    picker->SetColor({0.2f, 0.6f, 0.9f, 1.0f});
    picker->SetMode(W::ColorPicker::Mode::Rgb);
    picker->SetMode(W::ColorPicker::Mode::Hsv);
    const CS::Oklch after = picker->GetColorOklch();
    if (!Near(after.l, CS::OklchFromSrgb({0.2f, 0.6f, 0.9f, 1.0f}).l,
              0.02f)) {
        return "canonical state drifted across mode switches";
    }
    (void)before;

    // Palette click sets + commits in one gesture.
    picker->SetPalette({{0.1f, 0.9f, 0.2f, 1.0f}, {0.9f, 0.1f, 0.2f, 1.0f}});
    picker->Layout({0.0f, 0.0f, pref.width, pref.height},
                   XER::UI::Theme::Tokens{});
    committed = 0;
    // Palette row sits under the inputs row; recents already exist from
    // the earlier commit, so palette is the second-to-last row:
    // center = height - pad(10) - recent_row(22) - gap(8) - half_row(11).
    const float palette_y =
        picker->GetPreferredSize().height - 10.0f - 22.0f - 8.0f - 11.0f;
    picker->HandleEvent(Mouse(W::Event::Type::MouseDown, 10.0f + 5.0f,
                              palette_y, W::MouseButton::Left));
    if (committed != 1 ||
        !NearColor(picker->GetColor(), {0.1f, 0.9f, 0.2f, 1.0f}, 0.01f)) {
        return "palette swatch click did not set+commit";
    }

    // Recent dedup: committing the same color twice keeps one entry.
    picker->AddRecent({0.5f, 0.5f, 0.5f, 1.0f});
    picker->AddRecent({0.5f, 0.5f, 0.5f, 1.0f});
    int count = 0;
    for (const Color& c : picker->Recent()) {
        if (NearColor(c, {0.5f, 0.5f, 0.5f, 1.0f})) ++count;
    }
    if (count != 1) {
        return "recent list kept a duplicate";
    }
    return {};
}

// ── GradientEditor ────────────────────────────────────────────────────────
std::string CheckGradientEditor() {
    auto ed = std::make_shared<W::GradientEditor>();
    ed->Layout({0.0f, 0.0f, 360.0f, 240.0f},
               XER::UI::Theme::Tokens{});

    const auto& g0 = ed->GetGradient();
    if (g0.stops.size() != 2 || g0.stops[0].id == g0.stops[1].id ||
        g0.stops[0].id == 0) {
        return "default gradient malformed";
    }

    int live = 0, committed = 0;
    std::uint32_t selected = 0;
    ed->SetOnGradientChange([&](const W::Gradient&) { ++live; });
    ed->SetOnGradientCommitted([&](const W::Gradient&) { ++committed; });
    ed->SetOnStopSelected([&](std::uint32_t id) { selected = id; });

    // Stable ids across a third stop (snapshot the ids first — g0 is a
    // reference into the live vector and AddStop mutates it).
    const std::uint32_t id0 = g0.stops[0].id;
    const std::uint32_t id1 = g0.stops[1].id;
    const std::uint32_t new_id =
        ed->AddStop({1.0f, 1.0f, 0.0f, 1.0f}, 0.5f);
    if (new_id == 0 || new_id == id0 || new_id == id1) {
        return "AddStop did not mint a fresh id";
    }
    if (selected != new_id) {
        return "AddStop did not select the new stop";
    }

    // Drag the middle stop right via the stop bar. Stop bar: y after
    // tabs 24 + preview 44 + 2*gap + pad -> track center ≈ pad10 +
    // 24+8+44+8 + 15 = 109; bar x from pad+handle/2 ~17.5, w ~ 325.
    ed->Layout({0.0f, 0.0f, 360.0f, 240.0f},
               XER::UI::Theme::Tokens{});
    const float track_cx = 17.5f + 325.0f * 0.5f;
    const float track_y = 10.0f + 24.0f + 8.0f + 44.0f + 8.0f + 15.0f;
    ed->HandleEvent(Mouse(W::Event::Type::MouseDown, track_cx, track_y,
                          W::MouseButton::Left));
    ed->HandleEvent(Mouse(W::Event::Type::MouseMove, track_cx + 80.0f,
                          track_y, W::MouseButton::Left));
    if (live == 0) {
        return "stop drag did not fire live callbacks";
    }
    ed->HandleEvent(Mouse(W::Event::Type::MouseUp, track_cx + 80.0f,
                          track_y, W::MouseButton::Left));
    if (committed != 1) {
        return "stop drag did not commit on release";
    }

    // Drag-out removal: drag the same stop far below the bar.
    ed->HandleEvent(Mouse(W::Event::Type::MouseDown, track_cx + 80.0f,
                          track_y, W::MouseButton::Left));
    ed->HandleEvent(Mouse(W::Event::Type::MouseMove, track_cx + 80.0f,
                          track_y + 60.0f, W::MouseButton::Left));
    ed->HandleEvent(Mouse(W::Event::Type::MouseUp, track_cx + 80.0f,
                          track_y + 60.0f, W::MouseButton::Left));
    if (ed->GetGradient().stops.size() != 2) {
        return "drag-out did not remove the stop";
    }

    // Minimum two stops is enforced.
    ed->SelectStop(ed->GetGradient().stops[0].id);
    ed->HandleEvent(Key(W::KeyCode::Delete));
    if (ed->GetGradient().stops.size() != 2) {
        return "delete removed below the two-stop floor";
    }

    // Type tabs + angle dial.
    ed->SetType(W::GradientType::Conic);
    ed->SetAngle(725.0f);
    if (ed->GetGradient().type != W::GradientType::Conic ||
        !Near(ed->GetGradient().angle_deg, 5.0f, 0.001f)) {
        return "type/angle setters misbehaved";
    }

    // SampleAt pads endpoints and hits the midpoint blend.
    const Color mid = ed->SampleAt(0.5f);
    const Color a = ed->GetGradient().stops[0].color;
    const Color b = ed->GetGradient().stops[1].color;
    if (!NearColor(mid, {(a.r + b.r) * 0.5f, (a.g + b.g) * 0.5f,
                         (a.b + b.b) * 0.5f, (a.a + b.a) * 0.5f}, 0.05f)) {
        return "SampleAt midpoint not a blend of the end stops";
    }
    if (!NearColor(ed->SampleAt(-1.0f), a) ||
        !NearColor(ed->SampleAt(2.0f), b)) {
        return "SampleAt did not pad the endpoints";
    }
    return {};
}

// ── Eyedropper ────────────────────────────────────────────────────────────
std::string CheckEyedropper() {
    auto drop = std::make_shared<W::Eyedropper>();
    drop->Layout({0.0f, 0.0f, 400.0f, 300.0f},
                 XER::UI::Theme::Tokens{});

    // Source: x -> red ramp, y -> green ramp (per source pixel).
    drop->SetSampleSource([](float x, float y) {
        return Color{std::clamp(x / 100.0f, 0.0f, 1.0f),
                     std::clamp(y / 100.0f, 0.0f, 1.0f), 0.2f, 1.0f};
    });
    drop->SetSampleRadius(0);

    int picks = 0, cancels = 0;
    Color picked{};
    drop->SetOnPick([&](Color c) { ++picks; picked = c; });
    drop->SetOnCancel([&] { ++cancels; });

    // Inactive overlay ignores input entirely.
    if (drop->HandleEvent(Mouse(W::Event::Type::MouseMove, 50.0f, 50.0f))) {
        return "inactive eyedropper consumed input";
    }

    drop->SetActive(true);
    drop->HandleEvent(Mouse(W::Event::Type::MouseMove, 50.0f, 40.0f));
    const Color hover = drop->CurrentSample();
    if (!NearColor(hover, {0.5f, 0.4f, 0.2f, 1.0f}, 0.02f)) {
        return "hover sample wrong";
    }

    // Click picks and auto-deactivates.
    drop->HandleEvent(Mouse(W::Event::Type::MouseDown, 50.0f, 40.0f,
                            W::MouseButton::Left));
    if (picks != 1 || !NearColor(picked, {0.5f, 0.4f, 0.2f, 1.0f}, 0.02f) ||
        drop->IsActive()) {
        return "click did not pick + deactivate";
    }

    // Region averaging: radius 1 -> mean of the 3x3 window.
    drop->SetActive(true);
    drop->SetSampleRadius(1);
    drop->HandleEvent(Mouse(W::Event::Type::MouseMove, 10.0f, 10.0f));
    const Color avg = drop->CurrentSample();
    // Mean r over x in [9,11] = 0.10; mean g over y in [9,11] = 0.10.
    if (!NearColor(avg, {0.10f, 0.10f, 0.2f, 1.0f}, 0.02f)) {
        return "region average wrong";
    }

    // Escape cancels without picking.
    drop->HandleEvent(Key(W::KeyCode::Escape));
    if (picks != 1 || cancels != 1 || drop->IsActive()) {
        return "escape did not cancel cleanly";
    }
    return {};
}

} // namespace

std::string ColorWidgetsSmoke() {
    if (const auto r = CheckOklch(); !r.empty()) {
        return "oklch: " + r;
    }
    if (const auto r = CheckColorPicker(); !r.empty()) {
        return "color_picker: " + r;
    }
    if (const auto r = CheckGradientEditor(); !r.empty()) {
        return "gradient_editor: " + r;
    }
    if (const auto r = CheckEyedropper(); !r.empty()) {
        return "eyedropper: " + r;
    }
    return {};
}

}  // namespace XER::VideoEditor::Ui2
