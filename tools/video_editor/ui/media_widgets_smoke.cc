// See media_widgets_smoke.h — headless exercise of the media-kit
// widgets (Knob) added in src/ui/widgets.
#include "media_widgets_smoke.h"

#include <cmath>
#include <memory>
#include <string>

#include "layout/geometry.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/knob.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace W = XER::UI::Widgets;
using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Rendering::Renderer;
using XER::UI::Rendering::TextStyle;
using XER::UI::Theme::Tokens;

// Recording-free null renderer — the checks assert on widget state and
// callback invocation, not pixels.
class NullRenderer final : public Renderer {
public:
    int text_calls = 0;

    void BeginFrame(Rect) override {}
    void EndFrame() override {}
    void FillRect(Rect, XER::UI::Theme::Color, float) override {}
    void StrokeRect(Rect, XER::UI::Theme::Color, float, float) override {}
    void DrawLine(Point, Point, XER::UI::Theme::Color, float) override {}
    void DrawText(std::string_view, Rect, TextStyle) override {
        ++text_calls;
    }
    XER::UI::Layout::Size MeasureText(std::string_view text,
                                      TextStyle) const override {
        return {static_cast<float>(text.size()) * 7.0f, 14.0f};
    }
};

W::Event Key(W::KeyCode key, bool shift = false) {
    W::Event ev{};
    ev.type = W::Event::Type::KeyDown;
    ev.key = key;
    ev.is_shift = shift;
    return ev;
}

W::Event Click(float x, float y, bool alt = false) {
    W::Event ev{};
    ev.type = W::Event::Type::Click;
    ev.position = {x, y};
    ev.mouse_button = W::MouseButton::Left;
    ev.is_alt = alt;
    return ev;
}

W::Event Pointer(W::Event::Type type, float x, float y,
                 bool shift = false, bool alt = false) {
    W::Event ev{};
    ev.type = type;
    ev.position = {x, y};
    ev.mouse_button = W::MouseButton::Left;
    ev.is_shift = shift;
    ev.is_alt = alt;
    return ev;
}

W::Event Wheel(int delta, bool shift = false) {
    W::Event ev{};
    ev.type = W::Event::Type::MouseWheel;
    ev.wheel_delta = delta;
    ev.is_shift = shift;
    return ev;
}

std::string CheckKnob() {
    auto knob = W::Knob::Create();
    knob->SetRange(-1.0f, 1.0f);
    knob->SetAnchor(0.0f);
    knob->SetDetents({0.0f});
    knob->SetResetValue(0.0f);
    knob->SetLabel("Pan");
    knob->Layout({0.0f, 0.0f, 64.0f, 86.0f});

    int live = 0;
    int committed = 0;
    knob->SetOnValueChange([&](float) { ++live; });
    knob->SetOnValueCommitted([&](float) { ++committed; });

    // ---- vertical drag: up = increase, 150px = full travel -------------
    // Center of the dial is ~(32, ~33) — anywhere inside works for
    // vertical mode.
    knob->HandleEvent(Pointer(W::Event::Type::MouseDown, 32.0f, 40.0f));
    knob->HandleEvent(Pointer(W::Event::Type::MouseMove, 32.0f, 3.0f));
    // 37px / 150px ≈ 0.247 of the 2.0 range ≈ +0.493.
    const float v1 = knob->GetValue();
    if (!knob->IsDragging() || v1 < 0.3f || v1 > 0.7f) {
        return "knob: vertical drag did not raise the value";
    }
    if (live == 0 || committed != 0) {
        return "knob: drag committed before release";
    }
    knob->HandleEvent(Pointer(W::Event::Type::MouseUp, 32.0f, 3.0f));
    if (committed != 1 || knob->IsDragging()) {
        return "knob: drag end did not commit once";
    }

    // ---- Shift drag = 4x slower -----------------------------------------
    knob->SetValue(0.0f);
    knob->HandleEvent(Pointer(W::Event::Type::MouseDown, 32.0f, 40.0f));
    knob->HandleEvent(
        Pointer(W::Event::Type::MouseMove, 32.0f, 3.0f, true));
    const float v2 = knob->GetValue();
    if (v2 <= 0.0f || v2 >= v1 * 0.5f) {
        return "knob: Shift drag was not ~4x finer";
    }
    knob->HandleEvent(Pointer(W::Event::Type::MouseUp, 32.0f, 3.0f));

    // ---- detent snap near the anchor ------------------------------------
    // From 0.5, a 33px downward drag = -33/150*2.0 = -0.44 raw (0.06) —
    // inside the 0-detent pull zone (4% of the 2.0 range = 0.08), so the
    // value lands exactly on the detent.
    knob->SetValue(0.5f);
    knob->HandleEvent(Pointer(W::Event::Type::MouseDown, 32.0f, 40.0f));
    knob->HandleEvent(Pointer(W::Event::Type::MouseMove, 32.0f, 73.0f));
    if (knob->GetValue() != 0.0f) {
        return "knob: detent did not pull the value to the anchor";
    }
    knob->HandleEvent(Pointer(W::Event::Type::MouseUp, 32.0f, 73.0f));
    // Clear the detent so the remaining gesture checks run clean.
    knob->SetDetents({});

    // ---- circular drag: pointer angle maps onto the arc ------------------
    knob->SetDragMode(W::Knob::DragMode::Circular);
    knob->SetValue(-1.0f);
    // Straight above the dial center = arc midpoint = t 0.5 = value 0.
    const float cx = 32.0f;
    const float cy = 33.0f;   // approximate dial center
    knob->HandleEvent(Pointer(W::Event::Type::MouseDown, cx, cy + 30.0f));
    knob->HandleEvent(Pointer(W::Event::Type::MouseMove, cx, cy - 30.0f));
    if (std::fabs(knob->GetValue()) > 0.15f) {
        return "knob: circular drag did not map pointer angle";
    }
    knob->HandleEvent(Pointer(W::Event::Type::MouseUp, cx, cy - 30.0f));
    knob->SetDragMode(W::Knob::DragMode::Vertical);

    // ---- focused wheel: step / Shift fine-step ---------------------------
    knob->SetFocused(true);
    knob->SetValue(0.0f);
    const int committed_before_wheel = committed;
    knob->HandleEvent(Wheel(120));
    // Range -1..1: value 0 is normalized 0.5; one notch = +step 0.01.
    if (std::fabs(knob->GetNormalizedValue() - 0.51f) > 0.001f) {
        return "knob: focused wheel did not step";
    }
    knob->HandleEvent(Wheel(-120, true));
    if (std::fabs(knob->GetNormalizedValue() - 0.5075f) > 0.001f) {
        return "knob: Shift+wheel did not use fineStep";
    }
    knob->Tick(0.5);
    if (committed <= committed_before_wheel) {
        return "knob: wheel nudge never committed";
    }

    // ---- keyboard: arrows / Page / Home / End ----------------------------
    knob->SetValue(0.0f);
    knob->HandleEvent(Key(W::KeyCode::ArrowUp));
    if (std::fabs(knob->GetNormalizedValue() - 0.51f) > 0.001f) {
        return "knob: ArrowUp did not step";
    }
    knob->HandleEvent(Key(W::KeyCode::ArrowUp, true));
    if (std::fabs(knob->GetNormalizedValue() - 0.5125f) > 0.001f) {
        return "knob: Shift+ArrowUp did not fineStep";
    }
    knob->HandleEvent(Key(W::KeyCode::PageDown));
    if (std::fabs(knob->GetNormalizedValue() - 0.4125f) > 0.001f) {
        return "knob: PageDown did not largeStep";
    }
    knob->HandleEvent(Key(W::KeyCode::End));
    if (knob->GetValue() != 1.0f) {
        return "knob: End did not jump to max";
    }
    knob->HandleEvent(Key(W::KeyCode::Home));
    if (knob->GetValue() != -1.0f) {
        return "knob: Home did not jump to min";
    }

    // ---- log scale mapping ------------------------------------------------
    knob->SetRange(20.0f, 20000.0f);
    knob->SetScale(W::Knob::Scale::Log);
    knob->SetNormalizedValue(0.5f);
    if (std::fabs(knob->GetValue() - 632.45f) > 5.0f) {
        return "knob: log midpoint did not land near sqrt(20*20000)";
    }
    knob->SetScale(W::Knob::Scale::Linear);
    knob->SetRange(-1.0f, 1.0f);

    // ---- double-click + Delete reset -------------------------------------
    knob->SetValue(0.8f);
    const int committed_before_reset = committed;
    knob->HandleEvent(Click(32.0f, 33.0f));
    knob->HandleEvent(Click(32.0f, 33.0f));
    if (std::fabs(knob->GetValue()) > 0.001f) {
        return "knob: double-click did not reset";
    }
    if (committed != committed_before_reset + 1) {
        return "knob: reset did not commit";
    }
    knob->SetValue(0.5f);
    knob->HandleEvent(Key(W::KeyCode::Delete));
    if (std::fabs(knob->GetValue()) > 0.001f) {
        return "knob: Delete did not reset";
    }

    // ---- programmatic SetValue is live-only -------------------------------
    const int committed_before_set = committed;
    knob->SetValue(0.3f);
    if (committed != committed_before_set) {
        return "knob: SetValue fired the committed callback";
    }

    // ---- render pass must not crash ---------------------------------------
    NullRenderer renderer;
    Tokens tokens{};
    knob->Render(renderer, tokens);
    if (renderer.text_calls == 0) {
        return "knob: render pass drew no readout/label";
    }
    return {};
}

}  // namespace

std::string MediaWidgetsSmoke() {
    return CheckKnob();
}

}  // namespace XER::VideoEditor::Ui
