// Inspector panel: full clip editing (Phase 2). Static numeric fields
// are edited through one Apply gesture (the shell converts the edited
// Clip into a TimelineHistory ReplaceClip command, so undo stays
// transactional); structural edits that map to dedicated model ops go
// through their own hooks:
//   * keyframe add/remove/clear/nav  -> on_set_keyframes (+ on_seek)
//   * playback rate                  -> on_set_rate (SetClipRate keeps
//                                       the consumed source span; the
//                                       panel pre-adjusts `duration`
//                                       in the same ApplyClip so a
//                                       following ReplaceClip agrees)
//   * everything else (blend mode, pan, link group, masks, effect
//     list membership)               -> on_apply_clip(Clip)
//
// Sections: source header · timing/transform/audio fields · blend-mode
// combobox · link group · keyframe rows + embedded KeyframeEditorWidget
// · mask list · video-effect list · read-only audio_fx chain · actions.
// Content is taller than the panel — the widget scrolls internally via
// MouseWheel + child clipping, so the app can lay it out in a fixed rect.
#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Checkbox;
class Combobox;
class Label;
class TextInput;
}

namespace XER::VideoEditor::Ui {

class KeyframeEditorWidget;

class InspectorWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        std::function<void(const Clip&)> on_apply_clip;
        std::function<void(const std::string&)> on_delete_clip;
        std::function<void(const std::string&)> on_add_transition;
        std::function<void(const std::string&, bool)> on_status;
        // Full replacement key list for one property (SetClipKeyframes).
        std::function<void(const std::string& clip_id, KeyProp,
                           std::vector<Keyframe>)> on_set_keyframes;
        // Premiere Speed semantics (SetClipRate adjusts duration).
        std::function<void(const std::string& clip_id, double rate)>
            on_set_rate;
        // Move the playhead (keyframe prev/next navigation).
        std::function<void(double)> on_seek;
    };

    explicit InspectorWidget(Hooks hooks = {});

    void SetClip(const Clip* clip);
    const std::string& ClipId() const { return clip_.id; }
    // Timeline seconds — converted to clip-local for "key at playhead".
    void SetPlayhead(double timeline_seconds);
    double Playhead() const { return playhead_; }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {230.0f, 360.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {320.0f, 720.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }
    // Total scrollable content height (ScrollArea-compatible).
    float GetContentHeight() const override { return content_height_; }

private:
    enum class Field {
        Start, InPoint, Duration, Rate,
        OffsetX, OffsetY, Scale, Rotation, Opacity,
        GainDb, Pan, FadeIn, FadeOut
    };
    struct Row {
        Field field;
        std::string label;
        std::shared_ptr<XER::UI::Widgets::TextInput> input;
    };
    // Per-animatable-property keyframe controls.
    struct KeyRow {
        std::shared_ptr<XER::UI::Widgets::Button> select;  // "PosX · n"
        std::shared_ptr<XER::UI::Widgets::Button> toggle;  // diamond at u
        std::shared_ptr<XER::UI::Widgets::Button> prev;
        std::shared_ptr<XER::UI::Widgets::Button> next;
        std::shared_ptr<XER::UI::Widgets::Button> clear;
    };
    struct MaskRow {
        std::shared_ptr<XER::UI::Widgets::TextInput> cx;
        std::shared_ptr<XER::UI::Widgets::TextInput> cy;
        std::shared_ptr<XER::UI::Widgets::TextInput> w;
        std::shared_ptr<XER::UI::Widgets::TextInput> h;
        std::shared_ptr<XER::UI::Widgets::TextInput> feather;
        std::shared_ptr<XER::UI::Widgets::Checkbox> inverted;
        std::shared_ptr<XER::UI::Widgets::Button> remove;
    };
    struct FxRow {
        std::shared_ptr<XER::UI::Widgets::Checkbox> enabled;
        std::shared_ptr<XER::UI::Widgets::Button> remove;
    };
    // Text painted by Render (section headers, inline labels, read-only
    // rows) — drawn instead of Label children so clicks on non-control
    // areas still reach the panel (Click events do not bubble).
    struct DrawnLine {
        XER::UI::Layout::Rect rect;
        std::string text;
        bool header = false;   // bold section header vs normal label
    };

    void EnsureChildren();
    void RebuildDynamicRows();   // masks + effects per clip_
    void RebuildMaskRows();
    void RebuildFxRows();
    void SyncMaskRows();
    void SyncFxRows();
    void SyncKeyframeRows();
    void Apply();
    // Structural single-field commit (mask add/remove, effect remove,
    // blend select, inverted toggle) — bypasses text-field parsing.
    void CommitClip(const Clip& next);
    void ApplyKeyframes(KeyProp prop, std::vector<Keyframe> keys);
    void ToggleKeyAtPlayhead(KeyProp prop);
    void SeekKey(KeyProp prop, bool forward);
    double ClipLocalU() const;
    bool ParseField(const Row& row, double* value,
                    std::string* error_out) const;
    bool ParseEffects(std::vector<ClipEffect>* effects,
                      std::string* error_out) const;
    bool ParseMasks(std::vector<ClipMask>* masks,
                    std::string* error_out) const;
    void SetStatus(const std::string& text, bool is_error = false);

    Hooks hooks_;
    Clip clip_;
    bool has_clip_ = false;
    bool children_attached_ = false;
    bool syncing_ = false;
    double playhead_ = 0.0;                 // timeline seconds
    KeyProp active_prop_ = KeyProp::Opacity;
    std::set<int> fx_disabled_;             // excluded on next Apply
    float scroll_y_ = 0.0f;
    float content_height_ = 0.0f;
    std::vector<DrawnLine> drawn_;

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Label> source_;
    std::vector<Row> rows_;
    std::shared_ptr<XER::UI::Widgets::Combobox> blend_combo_;
    std::shared_ptr<XER::UI::Widgets::TextInput> link_input_;
    std::vector<KeyRow> key_rows_;   // AllKeyProps() order
    std::shared_ptr<KeyframeEditorWidget> key_editor_;
    std::vector<MaskRow> mask_rows_;
    std::shared_ptr<XER::UI::Widgets::Button> add_rect_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> add_ellipse_btn_;
    std::shared_ptr<XER::UI::Widgets::TextInput> effects_input_;
    std::vector<FxRow> fx_rows_;
    std::shared_ptr<XER::UI::Widgets::Checkbox> muted_;
    std::shared_ptr<XER::UI::Widgets::Button> apply_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> transition_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> delete_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
};

}  // namespace XER::VideoEditor::Ui
