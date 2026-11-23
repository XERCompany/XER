// InspectorPanel — the M3 inspector of the ui2 rebuild (spec §6
// "Inspector" dock tab): collapsible Card sections over the timeline
// selection — TRANSFORM, SPEED, BLEND, FX RACK, AUDIO.
//
// Ownership rules (surface_hooks.h): the panel NEVER touches Project
// or TimelineHistory. The shell pushes the current selection as Clip
// copies through SetSelectionProvider (VeApp adapts its timeline
// selection state) and the panel emits mutated Clip copies back
// through InspectorHooks::on_commit_clips — the shell runs them
// through TimelineHistory::ReplaceClip (whose same-clip command
// coalescing preserves "one scrub gesture = one undo step"), wrapped
// in BeginMacro/EndMacro for multi-clip gestures.
//
// Model notes honored here:
//   * ClipEffect has NO enabled flag — the rack's eye checkbox keeps
//     the legacy "exclusion staging" semantics: unchecking commits a
//     removal (undoable); the row lingers grayed in `bypassed_` so a
//     re-check re-inserts the effect in place during the session.
//   * Audio controls map to Clip::gain_db / Clip::pan / Clip::muted;
//     voice isolate leaves through a dedicated hook (clip.audio_fx
//     is VST3-only — builtin xer.af.* routing is the shell's call).
#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "timeline_model.h"   // Clip, Transform, BlendMode, ClipEffect
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Card;
class Checkbox;
class Knob;
class Label;
class ScrollArea;
class ScrubNumberField;
class Select;
class Slider;
}

namespace XER::VideoEditor::Ui2 {

// Param editor metadata for one ClipEffect::params slot — the shell
// resolves these from the plugin registry (PluginDesc::params) or a
// core-effect table; the panel renders a labeled ScrubNumberField.
struct InspectorParamSpec {
    std::string name;
    float minimum = -1000000.0f;
    float maximum = 1000000.0f;
    float step = 0.01f;
    float default_value = 0.0f;
};

struct InspectorHooks {
    // The commit seam — mutated Clip copies (usually size 1). The
    // shell ReplaceClip's each through history; ReplaceClipCommand's
    // same-clip coalescing keeps a scrub gesture at one undo step.
    std::function<void(const std::vector<Clip>&)> on_commit_clips;
    // Gesture grouping for multi-clip edits (and a single macro
    // marker for hosts that don't rely on coalescing): fired on the
    // first live change / on commit respectively.
    std::function<void()> on_begin_macro;
    std::function<void()> on_end_macro;
    std::function<void(const std::string&, bool is_error)> on_status;

    // FX metadata seams (resolved against builtin_fx/plugin/script
    // registries + the core-type table by the shell):
    std::function<std::string(const std::string& fx_type)>
        fx_display_name;
    std::function<std::vector<InspectorParamSpec>(
        const std::string& fx_type)>
        fx_param_specs;
    // "+ Add FX" menu entries — (ClipEffect::type, display name).
    std::function<
        std::vector<std::pair<std::string, std::string>>()>
        fx_catalog;
    // Voice isolate: availability predicate + apply hook (clip ids).
    std::function<bool(const Clip&)> voice_isolate_available;
    std::function<void(const std::vector<std::string>& clip_ids)>
        on_apply_voice_isolate;
};

class InspectorPanel final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<InspectorPanel> Create();

    void SetHooks(InspectorHooks hooks);
    const InspectorHooks& GetHooks() const { return hooks_; }

    // The selection seam (spec: "selection provider hook from VeApp") —
    // returns Clip COPIES; the panel edits these and commits them
    // back. RefreshSelection() re-pulls + resyncs silently.
    void SetSelectionProvider(
        std::function<std::vector<Clip>()> provider);
    void RefreshSelection();
    int SelectedClipCount() const {
        return static_cast<int>(selected_.size());
    }
    const std::vector<Clip>& SelectedClips() const { return selected_; }

    // ---- commit verbs (public: widget callbacks AND the smoke drive
    // these — the whole pipeline provider->edit->commit->undo runs
    // through them) ------------------------------------------------
    enum class TransformField : int {
        PosX = 0, PosY, Scale, Rotation, Opacity,
    };
    void ApplyTransformField(TransformField field, float value);
    void ApplySpeedPercent(float percent);        // playback_rate*100
    void ApplyDurationSeconds(double seconds);
    void ApplyBlendMode(BlendMode mode);
    void ApplyGainDb(float gain_db);
    void ApplyPan(float pan);
    void ApplyMuted(bool muted);
    void SetEffectParam(int effect_index, int param_index,
                        float value);
    void SetEffectBypassed(int effect_index, bool bypassed);
    void RemoveEffect(int effect_index);
    void AddEffect(const std::string& fx_type);
    void ApplyVoiceIsolate();

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
        return {320.0f, 640.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit InspectorPanel();

    void EnsureChildren();
    void RebuildSections();        // rebuild fx rows / sync fields
    void SyncFieldsFromModel();    // silent SetValue push
    Clip* Primary();
    const Clip* Primary() const;
    // The mutate->commit funnel: runs mutator on every selected clip
    // copy then emits the whole vector through on_commit_clips.
    template <typename Mut>
    void CommitAll(Mut&& mutator);
    void BeginGesture();           // on_begin_macro once per gesture
    void EndGesture();             // on_end_macro on commit
    // Effect identity for bypass staging (type + params text — the
    // same identity scheme the legacy inspector remapped on).
    static std::string EffectSignature(const ClipEffect& e);
    void RebuildFxRows();
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> MakeField(
        float minimum, float maximum, float step,
        const std::string& unit,
        std::function<void(float)> on_change,
        std::function<void(float)> on_commit = nullptr);

    std::vector<Clip> selected_;
    std::string selection_key_;    // ids — rebuilt rows only on change
    std::string fx_basis_sig_;     // effects signature of the rows
    std::set<int> expanded_fx_;    // fx rack rows showing params
    // Session-local bypass staging (ClipEffect has no enabled flag):
    // eye-off removed the effect from the clip but keeps the row
    // grayed here so a re-check re-inserts it in place.
    struct BypassedFx {
        ClipEffect effect;
        int index = 0;   // preferred re-insertion slot
    };
    std::vector<BypassedFx> bypassed_fx_;
    bool in_gesture_ = false;

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::ScrollArea> scroll_;
    std::shared_ptr<XER::UI::Widgets::Widget> column_;
    // Section cards.
    std::shared_ptr<XER::UI::Widgets::Card> transform_card_;
    std::shared_ptr<XER::UI::Widgets::Card> speed_card_;
    std::shared_ptr<XER::UI::Widgets::Card> blend_card_;
    std::shared_ptr<XER::UI::Widgets::Card> fx_card_;
    std::shared_ptr<XER::UI::Widgets::Card> audio_card_;
    // Transform fields.
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> pos_x_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> pos_y_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> scale_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> rotation_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> opacity_;
    // Speed/duration.
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> speed_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> duration_;
    // Blend.
    std::shared_ptr<XER::UI::Widgets::Select> blend_select_;
    // FX rack — a column of per-effect row widgets (rebuilt on
    // signature change) + the add button.
    std::shared_ptr<XER::UI::Widgets::Widget> fx_column_;
    std::shared_ptr<XER::UI::Widgets::Button> fx_add_btn_;
    // Audio.
    std::shared_ptr<XER::UI::Widgets::Knob> volume_knob_;
    std::shared_ptr<XER::UI::Widgets::Slider> pan_slider_;
    std::shared_ptr<XER::UI::Widgets::Checkbox> mute_check_;
    std::shared_ptr<XER::UI::Widgets::Button> voice_isolate_btn_;

    bool children_attached_ = false;
    bool syncing_ = false;         // silent SetValue reentrancy guard
    InspectorHooks hooks_;
    std::function<std::vector<Clip>()> selection_provider_;
};

}  // namespace XER::VideoEditor::Ui2
