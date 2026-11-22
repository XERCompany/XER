// ColorPanel — the M3 "Color" right-dock tab of the ui2 rebuild
// (spec §6): a compact color-workspace panel —
//
//   [ target clip label ]
//   Tint card     — kit ColorPicker (OKLCH-canonical) whose committed
//                   color maps onto the temperature/tint param axes
//   Wheels card   — three compact wheel+slider pairs (Shadows /
//                   Midtones / Highlights, i.e. lift / gamma / gain):
//                   each wheel is a locally-drawn hue disc with a
//                   draggable offset dot + a horizontal level Slider
//                   + a reset Button
//   WB row        — "Pick white balance" eyedropper button: arms the
//                   PROGRAM monitor's armed-click-point seam
//                   (MonitorSurface::SetClickPointsArmed); the shell
//                   samples the current frame and feeds the picked
//                   color back through ApplyPickedColor
//   Scopes row    — WFM/VEC/HST/PAR chips that bounce the request to
//                   the shell (which switches the Scopes dock tab and
//                   the ScopesPanel mode — the panel itself stays
//                   view-only)
//
// Ownership rules (surface_hooks.h / spec §6): the panel NEVER
// touches Project or TimelineHistory. The shell pushes the target
// clip as a Clip copy through SetClipProvider (same seam shape as
// InspectorPanel); every control mutation funnels into a ColorParams
// value object and leaves through ColorHooks::on_color_changed —
// the shell's adapter (VeApp::CommitClipColor) folds the params onto
// builtin clip effects through TimelineHistory::ReplaceClip, whose
// same-clip coalescing keeps one drag gesture at one undo step.
//
// Documented seam choice — the ClipEffect types the params target:
//   * wheels[*].level        -> builtin:xer.fx.lift_gamma_gain
//                               {lift, gamma, gain} (the level slider
//                               IS the band luminance: shadows->lift,
//                               midtones->gamma, highlights->gain)
//   * wheels[*].offset x/y   -> folded to per-channel gains on
//                               builtin:xer.fx.channel_mixer (the
//                               kernel set has no band-local chroma
//                               op — the fold weights mids strongest;
//                               see VeApp::CommitClipColor)
//   * temperature / tint     -> builtin:xer.fx.temperature /
//                               builtin:xer.fx.tint, written by both
//                               the OKLCH picker's cast axes and the
//                               eyedropper's neutralizing correction
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "layout/geometry.h"
#include "theme/color.h"
#include "timeline_model.h"   // Clip (provider copy)
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Card;
class ColorPicker;
class Label;
class ScrollArea;
class Slider;
class Widget;
}

namespace XER::VideoEditor::Ui2 {

// One wheel's view state — also the param payload.
struct ColorWheelState {
    // Hue-offset vector on the wheel disc, each axis in [-1,1].
    // x = warm(+)/cool(-) axis, y = magenta(+)/green(-) axis — the
    // adapter folds them onto channel-mixer gains.
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    // Band luminance in [-1,1]: shadows->lift, midtones->gamma
    // (mapped exponentially), highlights->gain.
    float level = 0.0f;
};

// The full param payload emitted through on_color_changed. Field
// semantics match the seam mapping documented at the top of this
// file — the shell adapter owns the ClipEffect translation.
struct ColorParams {
    float temperature = 0.0f;   // [-100,100] -> xer.fx.temperature
    float tint = 0.0f;          // [-100,100] -> xer.fx.tint
    // 0 = shadows (lift), 1 = midtones (gamma), 2 = highlights (gain).
    std::array<ColorWheelState, 3> wheels;
};

struct ColorHooks {
    // The commit seam — fired with the owning clip id and the full
    // param snapshot on every accepted control change (live drags
    // included; ReplaceClip coalescing keeps them one undo step).
    std::function<void(const std::string& clip_id,
                       const ColorParams&)> on_color_changed;
    // Gesture bracketing for wheel drags (BeginMacro/EndMacro).
    std::function<void()> on_begin_macro;
    std::function<void()> on_end_macro;
    // White-balance eyedropper arm request — the shell arms the
    // program monitor's click-point seam; the picked color comes
    // back through ApplyPickedColor().
    std::function<void()> on_arm_pick;
    // Scopes row — 0 WFM / 1 VEC / 2 HST / 3 PAR (ScopesPanel::Mode
    // order). The shell activates the Scopes dock tab + mode.
    std::function<void(int)> on_scope_mode;
    std::function<void(const std::string&, bool is_error)> on_status;
};

class ColorPanel final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<ColorPanel> Create();

    void SetHooks(ColorHooks hooks);
    const ColorHooks& GetHooks() const { return hooks_; }

    // Selection seam (InspectorPanel parity): returns a Clip COPY;
    // empty Clip::id means "no editable target" — controls stay up
    // but commits short-circuit. RefreshClip() re-pulls + resyncs.
    void SetClipProvider(std::function<Clip()> provider);
    void RefreshClip();
    const std::string& ClipId() const { return clip_id_; }

    // Current param state (the on_color_changed payload shape).
    const ColorParams& Params() const { return params_; }

    // ---- commit verbs (widget callbacks AND the smoke drive
    // these — the whole provider->edit->commit->undo pipeline runs
    // through them) -------------------------------------------------
    void SetWheelOffset(int wheel, float offset_x, float offset_y);
    void SetWheelLevel(int wheel, float level);
    void ResetWheel(int wheel);
    void ResetAll();
    void ApplyTemperatureTint(float temperature, float tint);

    // ---- eyedropper seam ------------------------------------------
    // The WB button's target (public so smoke + the shell can drive
    // the identical path): fires on_arm_pick and latches the armed
    // state until ApplyPickedColor / CancelPick lands.
    void ArmWhiteBalance();
    bool PickArmed() const { return pick_armed_; }
    // The SetOnSample-style seam the monitor's armed-point mode
    // feeds: converts the picked sRGB color into the neutralizing
    // temperature/tint correction, commits, and disarms.
    void ApplyPickedColor(XER::UI::Theme::Color color);
    void CancelPick();

    // ---- test/discovery surface ------------------------------------
    // The three wheel widgets (locally drawn hue discs) — exposed so
    // headless checks can hit-test real pointer events against them.
    XER::UI::Widgets::Widget* WheelSurface(int wheel) const;

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {240.0f, 160.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {320.0f, 560.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit ColorPanel();

    void EnsureChildren();
    void SyncControls();            // silent push of params_ -> widgets
    void EmitChanged();             // on_color_changed funnel
    void BeginGesture();
    void EndGesture();
    void RebuildFromClip(const Clip& clip);  // clip fx -> params_ (pull)
    // Picked color -> neutralizing correction (the fx-param mapping
    // documented in color_panel.cc); also used inverted by the
    // ColorPicker (picked color IS the cast).
    static std::pair<float, float> TempTintFromColor(
        float r, float g, float b, bool neutralize);

    std::string clip_id_;
    ColorParams params_;
    bool pick_armed_ = false;
    bool in_gesture_ = false;
    bool syncing_ = false;
    bool children_attached_ = false;
    ColorHooks hooks_;
    std::function<Clip()> clip_provider_;

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::ScrollArea> scroll_;
    std::shared_ptr<XER::UI::Widgets::Widget> column_;
    std::shared_ptr<XER::UI::Widgets::Card> tint_card_;
    std::shared_ptr<XER::UI::Widgets::Card> wheels_card_;
    std::shared_ptr<XER::UI::Widgets::Card> scopes_card_;
    std::shared_ptr<XER::UI::Widgets::ColorPicker> picker_;
    // Three wheel+slider+reset triples (shadows/midtones/highlights).
    // Wheels are the local ColorWheel widget — stored as Widget to
    // keep the header light.
    std::array<std::shared_ptr<XER::UI::Widgets::Widget>, 3> wheels_;
    std::array<std::shared_ptr<XER::UI::Widgets::Slider>, 3> levels_;
    std::array<std::shared_ptr<XER::UI::Widgets::Button>, 3> resets_;
    std::shared_ptr<XER::UI::Widgets::Button> pick_btn_;
    std::array<std::shared_ptr<XER::UI::Widgets::Button>, 4> scope_btns_;
};

}  // namespace XER::VideoEditor::Ui2
