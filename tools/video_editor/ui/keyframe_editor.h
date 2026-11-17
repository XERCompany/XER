// Compact keyframe curve editor for ONE animated clip property.
// Renders the property's time->value curve (same easing semantics as
// TimelineModel::EvaluateClipProperty — Hold steps, quadratic EaseIn/Out,
// smoothstep EaseInOut), keyframe diamonds, and the playhead.
//
// Interaction model (After-Effects-lite):
//   left-click empty point  — insert a key at (time, value)
//   left-drag a diamond     — move the key in time AND value
//   right-click a diamond   — delete the key
//   double-click a diamond  — cycle easing Linear -> EaseIn -> EaseOut ->
//                             EaseInOut -> Hold
//   Delete/Backspace        — delete the selected key
// Edits are committed through Hooks::on_set_keyframes with the FULL
// replacement key list (the app's history wraps SetClipKeyframes).
//
// The widget keeps an internal copy of the clip; the host must call
// SetClip() again whenever the model clip changes (e.g. after the hook
// round-trips through TimelineHistory) to stay in sync.
#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::VideoEditor::Ui {

// ---- shared keyframe helpers (inspector rows + this editor) ----

// Short display name for an animatable property.
inline const char* KeyPropName(KeyProp prop) {
    switch (prop) {
        case KeyProp::PosX:     return "PosX";
        case KeyProp::PosY:     return "PosY";
        case KeyProp::Scale:    return "Scale";
        case KeyProp::Rotation: return "Rotation";
        case KeyProp::Opacity:  return "Opacity";
        case KeyProp::GainDb:   return "GainDb";
    }
    return "?";
}

inline const char* KeyEaseName(KeyEase ease) {
    switch (ease) {
        case KeyEase::Linear:    return "Linear";
        case KeyEase::EaseIn:    return "EaseIn";
        case KeyEase::EaseOut:   return "EaseOut";
        case KeyEase::EaseInOut: return "EaseInOut";
        case KeyEase::Hold:      return "Hold";
    }
    return "?";
}

// The static (non-animated) value a property falls back to when the clip
// has no keys — mirrors EvaluateClipProperty's `fallback` argument.
inline float KeyPropFallback(const Clip& clip, KeyProp prop) {
    switch (prop) {
        case KeyProp::PosX:     return clip.transform.offset_x;
        case KeyProp::PosY:     return clip.transform.offset_y;
        case KeyProp::Scale:    return clip.transform.scale;
        case KeyProp::Rotation: return clip.transform.rotation_deg;
        case KeyProp::Opacity:  return clip.transform.opacity;
        case KeyProp::GainDb:   return clip.gain_db;
    }
    return 0.0f;
}

// All animatable properties in UI order.
inline const std::vector<KeyProp>& AllKeyProps() {
    static const std::vector<KeyProp> props = {
        KeyProp::PosX,    KeyProp::PosY,   KeyProp::Scale,
        KeyProp::Rotation, KeyProp::Opacity, KeyProp::GainDb,
    };
    return props;
}

class KeyframeEditorWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Replace the full sorted key list for `prop` on `clip_id`
        // (SetClipKeyframes on the app side).
        std::function<void(const std::string&, KeyProp,
                           std::vector<Keyframe>)> on_set_keyframes;
        std::function<void(const std::string&, bool)> on_status;
    };

    explicit KeyframeEditorWidget(Hooks hooks = {});

    // `clip` may be nullptr (clears the editor). The clip is copied —
    // call again whenever the model clip changes.
    void SetClip(const Clip* clip, KeyProp prop);
    void SetProperty(KeyProp prop);
    KeyProp Property() const { return prop_; }
    // Playhead in TIMELINE seconds (converted to clip-local internally).
    void SetPlayhead(double timeline_seconds);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {180.0f, 90.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {280.0f, 140.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 240.0f};
    }

private:
    XER::UI::Layout::Rect PlotRect() const;
    // Value axis range for the active property: a sensible per-prop
    // default widened to include every key value plus the fallback.
    std::pair<float, float> ValueRange() const;
    double TimeSpan() const;  // seconds shown on the x axis (>= epsilon)
    float TimeToX(double u, const XER::UI::Layout::Rect& plot) const;
    float ValueToY(float v, const XER::UI::Layout::Rect& plot) const;
    double XToTime(float x, const XER::UI::Layout::Rect& plot) const;
    float YToValue(float y, const XER::UI::Layout::Rect& plot) const;

    std::vector<Keyframe>& Keys();        // clip_.keyframes[prop_]
    const std::vector<Keyframe>& Keys() const;
    int HitKey(const XER::UI::Layout::Point& position,
               const XER::UI::Layout::Rect& plot) const;
    void SortDedupeKeys();
    // Push the current local key list through the hook (and keep the
    // local clip copy in sync so the curve redraws immediately).
    void CommitKeys(const char* status);
    void SetStatus(const std::string& text, bool is_error = false);

    Hooks hooks_;
    Clip clip_;
    bool has_clip_ = false;
    KeyProp prop_ = KeyProp::Opacity;
    double playhead_ = 0.0;   // timeline seconds

    int hover_key_ = -1;
    int selected_key_ = -1;
    int drag_key_ = -1;
    bool drag_moved_ = false;
    bool just_dragged_ = false;  // suppress the click after a drag
    // Press position — the tree synthesizes Click on every left MouseUp
    // whose release still hits this widget, so a rubber-band-style drag
    // across empty plot space must not become an "add key" click.
    XER::UI::Layout::Point press_pos_{};
    bool press_valid_ = false;
    // Double-click detection (no dedicated event in the widgets Event
    // enum): two Clicks on the same key within the window cycle easing.
    int last_click_key_ = -1;
    std::chrono::steady_clock::time_point last_click_time_{};
    // Index+time of a key created by the first click of a double-click —
    // the second click selects it instead of cycling its easing.
    int added_key_ = -1;
    std::chrono::steady_clock::time_point added_key_time_{};
};

}  // namespace XER::VideoEditor::Ui
