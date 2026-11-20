#include "inspector_widget.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <utility>

#include "keyframe_editor.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/checkbox.h"
#include "widgets/combobox.h"
#include "widgets/label.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor::Ui {
namespace {

std::string Basename(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string Number(double value, int precision = 4) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
    return buffer;
}

const std::set<std::string>& KnownEffects() {
    // Mirrors the compositor's dispatch table (ApplyClipEffects); the
    // EffectsPanelWidget catalog carries the same list + defaults.
    static const std::set<std::string> types = {
        "brightness_contrast", "grayscale",   "box_blur",
        "gaussian_blur",      "flip_h",      "flip_v",
        "sharpen",            "edge_detect", "emboss",
        "vignette",           "chromatic_aberration",
        "pixelate",           "oil_paint",   "motion_blur",
        "chroma_key",         "color_balance", "posterize",
    };
    return types;
}

const char* BlendModeName(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal:   return "Normal";
        case BlendMode::Add:      return "Add";
        case BlendMode::Multiply: return "Multiply";
        case BlendMode::Screen:   return "Screen";
        case BlendMode::Overlay:  return "Overlay";
        case BlendMode::Darken:   return "Darken";
        case BlendMode::Lighten:  return "Lighten";
    }
    return "Normal";
}

const std::vector<const char*>& BlendModeNames() {
    static const std::vector<const char*> names = {
        "Normal", "Add", "Multiply", "Screen", "Overlay", "Darken",
        "Lighten",
    };
    return names;
}

// Case-insensitive name -> enum; returns `fallback` when unknown.
BlendMode BlendModeFromName(const std::string& name, BlendMode fallback) {
    auto lower = [](std::string s) {
        for (char& c : s) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return s;
    };
    const std::string needle = lower(name);
    for (std::size_t i = 0; i < BlendModeNames().size(); ++i) {
        if (lower(BlendModeNames()[i]) == needle) {
            return static_cast<BlendMode>(i);
        }
    }
    return fallback;
}

const char* MaskShapeName(MaskShape shape) {
    return shape == MaskShape::Rect ? "rect" : "ellipse";
}

std::string Trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::vector<std::string> Split(const std::string& text, char delimiter) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t pos = text.find(delimiter, start);
        out.push_back(text.substr(
            start, pos == std::string::npos ? pos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return out;
}

std::string FormatEffects(const std::vector<ClipEffect>& effects) {
    std::string text;
    for (const ClipEffect& effect : effects) {
        if (!text.empty()) text += "; ";
        text += effect.type;
        if (!effect.params.empty()) {
            text += ':';
            for (std::size_t i = 0; i < effect.params.size(); ++i) {
                if (i > 0) text += ',';
                char buffer[24];
                std::snprintf(buffer, sizeof(buffer), "%.4g",
                              effect.params[i]);
                text += buffer;
            }
        }
    }
    return text;
}

// Two effects are "the same" for exclusion staging when they display
// identically — params compare through the same %.4g text the effects
// field shows, so a FormatEffects->ParseEffects round-trip can't break
// the match on float-precision noise.
bool SameEffect(const ClipEffect& a, const ClipEffect& b) {
    if (a.type != b.type || a.params.size() != b.params.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.params.size(); ++i) {
        char ba[24];
        char bb[24];
        std::snprintf(ba, sizeof(ba), "%.4g", a.params[i]);
        std::snprintf(bb, sizeof(bb), "%.4g", b.params[i]);
        if (std::strcmp(ba, bb) != 0) return false;
    }
    return true;
}

// Re-derives staged exclusion indices (staged against `basis`) onto
// `current`: each disabled effect keeps its identity and its
// occurrence ordinal — the second "blur:1" of the staged list maps to
// the second "blur:1" of the new list. Effects absent from `current`
// simply drop out of the set.
std::set<int> RemapFxDisabled(const std::vector<ClipEffect>& basis,
                              const std::set<int>& disabled,
                              const std::vector<ClipEffect>& current) {
    std::set<int> out;
    for (const int index : disabled) {
        if (index < 0 || index >= static_cast<int>(basis.size())) {
            continue;
        }
        const ClipEffect& target = basis[static_cast<std::size_t>(index)];
        int ordinal = 0;
        for (int i = 0; i < index; ++i) {
            if (SameEffect(basis[static_cast<std::size_t>(i)], target)) {
                ++ordinal;
            }
        }
        int seen = 0;
        for (std::size_t j = 0; j < current.size(); ++j) {
            if (!SameEffect(current[j], target)) continue;
            if (seen == ordinal) {
                out.insert(static_cast<int>(j));
                break;
            }
            ++seen;
        }
    }
    return out;
}

// Single effect as "type(p1,p2)" for the per-row label.
std::string FormatEffectBrief(const ClipEffect& effect) {
    std::string text = effect.type;
    if (!effect.params.empty()) {
        text += " (";
        for (std::size_t i = 0; i < effect.params.size(); ++i) {
            if (i > 0) text += ", ";
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "%.4g",
                          effect.params[i]);
            text += buffer;
        }
        text += ')';
    }
    return text;
}

constexpr double kKeyTimeEpsilon = 1e-3;

}  // namespace

InspectorWidget::InspectorWidget(Hooks hooks) : hooks_(std::move(hooks)) {
    SetFocusable(true);
    // Sections scroll internally: children are clipped to the panel and
    // MouseWheel adjusts scroll_y_.
    SetClipsChildren(true);
}

void InspectorWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    title_ = std::make_shared<Widgets::Label>("INSPECTOR");
    source_ = std::make_shared<Widgets::Label>("No clip selected", true);
    muted_ = std::make_shared<Widgets::Checkbox>(false);
    muted_->SetTooltip("Mute clip audio");
    effects_input_ = std::make_shared<Widgets::TextInput>("");
    effects_input_->SetPlaceholder(
        "Effects: gaussian_blur:8; brightness_contrast:12,1.2");
    effects_input_->SetOnSubmit([this] { Apply(); });
    apply_btn_ = std::make_shared<Widgets::Button>(
        "Apply", Widgets::ButtonStyle::Primary);
    apply_btn_->SetOnClick([this] { Apply(); });
    // Transition picker: every supported type in the model's stable
    // menu order + a duration field; "Add" commits the pair to the
    // junction after the clip (the shell resolves the adjacent clip).
    transition_combo_ = Widgets::Combobox::Create();
    std::vector<Widgets::Combobox::Option> transition_options;
    for (const Transition::Type type : AllTransitionTypes()) {
        const char* name = TransitionTypeLabel(type);
        transition_options.push_back({name, name});
    }
    transition_combo_->SetOptions(std::move(transition_options));
    transition_combo_->SetPlaceholder("Transition type");
    transition_combo_->SetValue("Crossfade");
    transition_duration_input_ =
        std::make_shared<Widgets::TextInput>("1.0");
    transition_duration_input_->SetPlaceholder("Duration (s)");
    transition_duration_input_->SetTooltip(
        "Transition duration in seconds");
    transition_btn_ = std::make_shared<Widgets::Button>(
        "Add", Widgets::ButtonStyle::Secondary);
    transition_btn_->SetTooltip(
        "Add the picked transition to the next clip");
    transition_btn_->SetOnClick([this] { CommitTransition(); });
    delete_btn_ = std::make_shared<Widgets::Button>(
        "Delete", Widgets::ButtonStyle::Destructive);
    delete_btn_->SetOnClick([this] {
        if (has_clip_ && hooks_.on_delete_clip) {
            hooks_.on_delete_clip(clip_.id);
        }
    });
    status_ = std::make_shared<Widgets::Label>("Ready", true);

    // Blend-mode dropdown (all 7 modes; selection applies immediately).
    blend_combo_ = Widgets::Combobox::Create();
    std::vector<Widgets::Combobox::Option> blend_options;
    for (const char* name : BlendModeNames()) {
        blend_options.push_back({name, name});
    }
    blend_combo_->SetOptions(std::move(blend_options));
    blend_combo_->SetPlaceholder("Blend mode");
    blend_combo_->SetOnSelect(
        [this](const Widgets::Combobox::Option& option) {
            if (syncing_ || !has_clip_) return;
            Clip next = clip_;
            next.blend_mode = BlendModeFromName(
                option.value.empty() ? option.label : option.value,
                clip_.blend_mode);
            CommitClip(next);
            SetStatus(std::string("blend: ") + BlendModeName(next.blend_mode));
        });

    link_input_ = std::make_shared<Widgets::TextInput>("");
    link_input_->SetPlaceholder("Link group (empty = unlinked)");
    link_input_->SetOnSubmit([this] { Apply(); });

    const std::pair<Field, const char*> fields[] = {
        {Field::Start, "Start (s)"},
        {Field::InPoint, "In point (s)"},
        {Field::Duration, "Duration (s)"},
        {Field::Rate, "Speed x (0.01-100)"},
        {Field::OffsetX, "Offset X"},
        {Field::OffsetY, "Offset Y"},
        {Field::Scale, "Scale"},
        {Field::Rotation, "Rotation (deg)"},
        {Field::Opacity, "Opacity"},
        {Field::GainDb, "Gain (dB)"},
        {Field::Pan, "Pan (-1..1)"},
        {Field::FadeIn, "Fade in (s)"},
        {Field::FadeOut, "Fade out (s)"},
    };
    rows_.reserve(std::size(fields));
    for (const auto& [field, label] : fields) {
        Row row;
        row.field = field;
        row.label = label;
        row.input = std::make_shared<Widgets::TextInput>("");
        row.input->SetPlaceholder(label);
        row.input->SetOnSubmit([this] { Apply(); });
        rows_.push_back(std::move(row));
    }

    // Keyframe rows — one per animatable property.
    for (KeyProp prop : AllKeyProps()) {
        KeyRow row;
        row.select = std::make_shared<Widgets::Button>(
            KeyPropName(prop), Widgets::ButtonStyle::Ghost);
        row.select->SetTooltip("Show this property in the curve editor");
        row.select->SetOnClick([this, prop] {
            active_prop_ = prop;
            if (key_editor_ != nullptr && has_clip_) {
                key_editor_->SetClip(&clip_, prop);
            }
            SyncKeyframeRows();
            Invalidate();
        });
        row.toggle = std::make_shared<Widgets::Button>(
            "<>", Widgets::ButtonStyle::Ghost);
        row.toggle->SetTooltip("Toggle keyframe at playhead");
        row.toggle->SetOnClick(
            [this, prop] { ToggleKeyAtPlayhead(prop); });
        row.prev = std::make_shared<Widgets::Button>(
            "<", Widgets::ButtonStyle::Ghost);
        row.prev->SetTooltip("Previous keyframe");
        row.prev->SetOnClick([this, prop] { SeekKey(prop, false); });
        row.next = std::make_shared<Widgets::Button>(
            ">", Widgets::ButtonStyle::Ghost);
        row.next->SetTooltip("Next keyframe");
        row.next->SetOnClick([this, prop] { SeekKey(prop, true); });
        row.clear = std::make_shared<Widgets::Button>(
            "x", Widgets::ButtonStyle::Ghost);
        row.clear->SetTooltip("Remove all keys for this property");
        row.clear->SetOnClick(
            [this, prop] { ApplyKeyframes(prop, {}); });
        key_rows_.push_back(std::move(row));
    }

    // Embedded curve editor — edits AllKeyProps()[active] for clip_.
    KeyframeEditorWidget::Hooks editor_hooks;
    editor_hooks.on_set_keyframes =
        [this](const std::string& clip_id, KeyProp prop,
               std::vector<Keyframe> keys) {
            if (has_clip_ && clip_id == clip_.id) {
                clip_.keyframes[prop] = keys;
                SyncKeyframeRows();
            }
            if (hooks_.on_set_keyframes) {
                hooks_.on_set_keyframes(clip_id, prop, std::move(keys));
            }
        };
    editor_hooks.on_status = [this](const std::string& text, bool err) {
        SetStatus(text, err);
    };
    key_editor_ = std::make_shared<KeyframeEditorWidget>(editor_hooks);

    add_rect_btn_ = std::make_shared<Widgets::Button>(
        "+ Rect", Widgets::ButtonStyle::Secondary);
    add_rect_btn_->SetTooltip("Add a rectangular mask");
    add_rect_btn_->SetOnClick([this] {
        if (!has_clip_) return;
        Clip next = clip_;
        ClipMask mask;
        mask.shape = MaskShape::Rect;
        next.masks.push_back(mask);
        CommitClip(next);
    });
    add_ellipse_btn_ = std::make_shared<Widgets::Button>(
        "+ Ellipse", Widgets::ButtonStyle::Secondary);
    add_ellipse_btn_->SetTooltip("Add an elliptical mask");
    add_ellipse_btn_->SetOnClick([this] {
        if (!has_clip_) return;
        Clip next = clip_;
        next.masks.push_back(ClipMask{});   // Ellipse is the default
        CommitClip(next);
    });

    AddChild(title_);
    AddChild(source_);
    for (const Row& row : rows_) AddChild(row.input);
    AddChild(blend_combo_);
    AddChild(link_input_);
    for (const KeyRow& row : key_rows_) {
        AddChild(row.select);
        AddChild(row.toggle);
        AddChild(row.prev);
        AddChild(row.next);
        AddChild(row.clear);
    }
    AddChild(key_editor_);
    AddChild(add_rect_btn_);
    AddChild(add_ellipse_btn_);
    AddChild(effects_input_);
    AddChild(muted_);
    AddChild(apply_btn_);
    AddChild(transition_combo_);
    AddChild(transition_duration_input_);
    AddChild(transition_btn_);
    AddChild(delete_btn_);
    AddChild(status_);
    SetClip(has_clip_ ? &clip_ : nullptr);
}

double InspectorWidget::ClipLocalU() const {
    if (!has_clip_) return 0.0;
    return std::clamp(playhead_ - clip_.timeline_start, 0.0, clip_.duration);
}

void InspectorWidget::RebuildDynamicRows() {
    RebuildMaskRows();
    RebuildFxRows();
    RebuildAudioFxRows();
}

void InspectorWidget::RebuildMaskRows() {
    namespace Widgets = XER::UI::Widgets;
    // See retired_mask_children_ in the header: this rebuild may be
    // running inside a row child's own callback — disable + unparent,
    // destroy next rebuild.
    retired_mask_children_.clear();
    for (MaskRow& row : mask_rows_) {
        const std::shared_ptr<Widgets::Widget> children[] = {
            row.cx, row.cy, row.w, row.h, row.feather, row.inverted,
            row.remove};
        for (const auto& child : children) {
            child->SetEnabled(false);
            RemoveChild(child);
            retired_mask_children_.push_back(child);
        }
    }
    mask_rows_.clear();

    if (has_clip_) {
        mask_rows_.reserve(clip_.masks.size());
        for (std::size_t i = 0; i < clip_.masks.size(); ++i) {
            MaskRow row;
            auto make_field = [this](const char* placeholder) {
                auto input =
                    std::make_shared<Widgets::TextInput>("");
                input->SetPlaceholder(placeholder);
                input->SetTooltip(placeholder);
                input->SetOnSubmit([this] { Apply(); });
                return input;
            };
            row.cx = make_field("cx");
            row.cy = make_field("cy");
            row.w = make_field("w");
            row.h = make_field("h");
            row.feather = make_field("feather");
            row.inverted = std::make_shared<Widgets::Checkbox>(false);
            row.inverted->SetTooltip("Invert mask");
            const std::size_t index = i;
            row.inverted->SetOnChanged([this, index](bool checked) {
                if (syncing_ || !has_clip_ ||
                    index >= clip_.masks.size()) {
                    return;
                }
                Clip next = clip_;
                next.masks[index].inverted = checked;
                CommitClip(next);
            });
            row.remove = std::make_shared<Widgets::Button>(
                "x", Widgets::ButtonStyle::Ghost);
            row.remove->SetTooltip("Delete mask");
            row.remove->SetOnClick([this, index] {
                if (!has_clip_ || index >= clip_.masks.size()) return;
                Clip next = clip_;
                next.masks.erase(next.masks.begin() +
                                 static_cast<std::ptrdiff_t>(index));
                CommitClip(next);
            });
            AddChild(row.cx);
            AddChild(row.cy);
            AddChild(row.w);
            AddChild(row.h);
            AddChild(row.feather);
            AddChild(row.inverted);
            AddChild(row.remove);
            mask_rows_.push_back(std::move(row));
        }
    }
    RequestLayout();
}

void InspectorWidget::RebuildFxRows() {
    namespace Widgets = XER::UI::Widgets;
    // Same deferred destruction as RebuildMaskRows — a row callback
    // can trigger this rebuild mid-dispatch (own graveyard so the
    // back-to-back mask rebuild in RebuildDynamicRows can't free the
    // firing child early).
    retired_fx_children_.clear();
    for (FxRow& row : fx_rows_) {
        const std::shared_ptr<Widgets::Widget> children[] = {
            row.enabled, row.remove};
        for (const auto& child : children) {
            child->SetEnabled(false);
            RemoveChild(child);
            retired_fx_children_.push_back(child);
        }
    }
    fx_rows_.clear();
    // Staged exclusions survive rebuilds — a rebuild can be triggered
    // by mask churn or any CommitClip resync, not just an effects
    // edit, and clearing unconditionally would silently re-apply an
    // effect the user unchecked. Remap by identity so an edited list
    // keeps the exclusion on the right effect; a different clip (or a
    // list that no longer contains it) drops the stale entry.
    const bool same_clip =
        has_clip_ && !fx_clip_id_.empty() && clip_.id == fx_clip_id_;
    fx_disabled_ = same_clip
                       ? RemapFxDisabled(fx_effects_basis_, fx_disabled_,
                                         clip_.effects)
                       : std::set<int>();
    fx_clip_id_ = has_clip_ ? clip_.id : std::string();
    fx_effects_basis_ =
        has_clip_ ? clip_.effects : std::vector<ClipEffect>();

    if (has_clip_) {
        fx_rows_.reserve(clip_.effects.size());
        for (std::size_t i = 0; i < clip_.effects.size(); ++i) {
            FxRow row;
            row.enabled = std::make_shared<Widgets::Checkbox>(true);
            // No per-effect enabled flag exists in the model — the
            // checkbox stages exclusion: unchecked rows are dropped by
            // the next Apply (params stay editable in the text field).
            row.enabled->SetTooltip(
                "Uncheck to drop this effect on Apply");
            const std::size_t index = i;
            row.enabled->SetOnChanged([this, index](bool checked) {
                if (syncing_) return;
                if (checked) {
                    fx_disabled_.erase(static_cast<int>(index));
                } else {
                    fx_disabled_.insert(static_cast<int>(index));
                    SetStatus("effect excluded — Apply to commit");
                }
            });
            row.remove = std::make_shared<Widgets::Button>(
                "x", Widgets::ButtonStyle::Ghost);
            row.remove->SetTooltip("Remove effect");
            row.remove->SetOnClick([this, index] {
                if (!has_clip_ || index >= clip_.effects.size()) return;
                Clip next = clip_;
                next.effects.erase(next.effects.begin() +
                                   static_cast<std::ptrdiff_t>(index));
                CommitClip(next);
            });
            AddChild(row.enabled);
            AddChild(row.remove);
            fx_rows_.push_back(std::move(row));
        }
    }
    RequestLayout();
}

void InspectorWidget::RebuildAudioFxRows() {
    namespace Widgets = XER::UI::Widgets;
    // Same deferred destruction as RebuildFxRows — the click handler
    // can reenter a rebuild through SetClip mid-dispatch.
    retired_afx_children_.clear();
    for (const auto& btn : afx_edit_btns_) {
        btn->SetEnabled(false);
        RemoveChild(btn);
        retired_afx_children_.push_back(btn);
    }
    afx_edit_btns_.clear();

    if (has_clip_) {
        afx_edit_btns_.reserve(clip_.audio_fx.size());
        for (std::size_t i = 0; i < clip_.audio_fx.size(); ++i) {
            const std::size_t index = i;
            auto btn = std::make_shared<Widgets::Button>(
                "Editor", Widgets::ButtonStyle::Ghost);
            btn->SetTooltip(
                "Open the plugin's native editor view");
            btn->SetOnClick([this, index] {
                if (!has_clip_ || index >= clip_.audio_fx.size() ||
                    !hooks_.on_open_audio_fx_editor) {
                    return;
                }
                hooks_.on_open_audio_fx_editor(clip_.id, index);
            });
            AddChild(btn);
            afx_edit_btns_.push_back(std::move(btn));
        }
    }
}

void InspectorWidget::SyncMaskRows() {
    namespace Widgets = XER::UI::Widgets;
    if (mask_rows_.size() != clip_.masks.size()) {
        RebuildMaskRows();   // fresh empty rows — fill them below
    }
    for (std::size_t i = 0; i < mask_rows_.size(); ++i) {
        const ClipMask& mask = clip_.masks[i];
        MaskRow& row = mask_rows_[i];
        row.cx->SetValue(Number(mask.cx, 3));
        row.cy->SetValue(Number(mask.cy, 3));
        row.w->SetValue(Number(mask.w, 3));
        row.h->SetValue(Number(mask.h, 3));
        row.feather->SetValue(Number(mask.feather, 3));
        row.inverted->SetChecked(mask.inverted);
    }
}

void InspectorWidget::SyncFxRows() {
    if (fx_rows_.size() != clip_.effects.size()) {
        RebuildFxRows();   // remaps basis + staged exclusions itself
    } else if (clip_.id != fx_clip_id_) {
        fx_clip_id_ = clip_.id;
        fx_effects_basis_ = clip_.effects;
        fx_disabled_.clear();
    } else {
        // Same row count but the contents may have changed (Apply
        // commits round-trip through here) — re-derive staged
        // exclusions against the current effect set.
        fx_disabled_ = RemapFxDisabled(fx_effects_basis_, fx_disabled_,
                                       clip_.effects);
        fx_effects_basis_ = clip_.effects;
    }
    // Drawn labels pick values up from clip_ — nothing else to sync;
    // keep checkboxes consistent with the disabled set.
    for (std::size_t i = 0; i < fx_rows_.size(); ++i) {
        fx_rows_[i].enabled->SetChecked(
            fx_disabled_.count(static_cast<int>(i)) == 0);
    }
}

void InspectorWidget::SyncKeyframeRows() {
    if (!children_attached_) return;
    const double u = ClipLocalU();
    const auto& props = AllKeyProps();
    for (std::size_t i = 0; i < props.size() && i < key_rows_.size(); ++i) {
        KeyProp prop = props[i];
        const auto it = clip_.keyframes.find(prop);
        const std::size_t count =
            (has_clip_ && it != clip_.keyframes.end()) ? it->second.size()
                                                     : 0;
        char label[48];
        std::snprintf(label, sizeof(label), "%s · %zu", KeyPropName(prop),
                      count);
        key_rows_[i].select->SetText(label);
        key_rows_[i].select->SetStyle(prop == active_prop_
                                          ? XER::UI::Widgets::ButtonStyle::Secondary
                                          : XER::UI::Widgets::ButtonStyle::Ghost);
        bool keyed = false;
        if (it != clip_.keyframes.end()) {
            keyed = std::any_of(
                it->second.begin(), it->second.end(),
                [u](const Keyframe& k) {
                    return std::fabs(k.time - u) < kKeyTimeEpsilon;
                });
        }
        // Diamond = key exists at the playhead's clip-local time.
        key_rows_[i].toggle->SetText(keyed ? "\xe2\x97\x86" : "\xe2\x97\x87");
        const bool enable = has_clip_;
        key_rows_[i].select->SetEnabled(enable);
        key_rows_[i].toggle->SetEnabled(enable);
        key_rows_[i].prev->SetEnabled(enable && count > 0);
        key_rows_[i].next->SetEnabled(enable && count > 0);
        key_rows_[i].clear->SetEnabled(enable && count > 0);
    }
}

void InspectorWidget::SetClip(const Clip* clip) {
    syncing_ = true;
    has_clip_ = clip != nullptr;
    if (clip != nullptr) clip_ = *clip;
    if (children_attached_) {
        source_->SetText(clip == nullptr
                             ? "No clip selected"
                             : Basename(clip_.source_path));
        const auto set = [this](Field field, double value,
                                int precision = 4) {
            auto it = std::find_if(rows_.begin(), rows_.end(),
                                   [field](const Row& row) {
                                       return row.field == field;
                                   });
            if (it != rows_.end()) {
                it->input->SetValue(Number(value, precision));
            }
        };
        set(Field::Start, clip == nullptr ? 0.0 : clip_.timeline_start);
        set(Field::InPoint, clip == nullptr ? 0.0 : clip_.in_point);
        set(Field::Duration, clip == nullptr ? 0.0 : clip_.duration);
        set(Field::Rate, clip == nullptr ? 1.0 : clip_.playback_rate, 3);
        set(Field::OffsetX, clip == nullptr ? 0.0 : clip_.transform.offset_x, 4);
        set(Field::OffsetY, clip == nullptr ? 0.0 : clip_.transform.offset_y, 4);
        set(Field::Scale, clip == nullptr ? 1.0 : clip_.transform.scale, 4);
        set(Field::Rotation,
            clip == nullptr ? 0.0 : clip_.transform.rotation_deg, 2);
        set(Field::Opacity,
            clip == nullptr ? 1.0 : clip_.transform.opacity, 3);
        set(Field::GainDb, clip == nullptr ? 0.0 : clip_.gain_db, 2);
        set(Field::Pan, clip == nullptr ? 0.0 : clip_.pan, 2);
        set(Field::FadeIn, clip == nullptr ? 0.0 : clip_.fade_in, 3);
        set(Field::FadeOut, clip == nullptr ? 0.0 : clip_.fade_out, 3);
        muted_->SetChecked(clip != nullptr && clip_.muted);
        blend_combo_->SetValue(
            clip == nullptr ? std::string("Normal")
                            : BlendModeName(clip_.blend_mode));
        blend_combo_->SetEnabled(has_clip_);
        link_input_->SetValue(clip == nullptr ? std::string()
                                              : clip_.link_group);
        link_input_->SetEnabled(has_clip_);
        effects_input_->SetValue(clip == nullptr
                                     ? std::string()
                                     : FormatEffects(clip_.effects));
        effects_input_->SetEnabled(has_clip_);
        apply_btn_->SetEnabled(has_clip_);
        transition_combo_->SetEnabled(has_clip_);
        transition_duration_input_->SetEnabled(has_clip_);
        transition_btn_->SetEnabled(has_clip_);
        delete_btn_->SetEnabled(has_clip_);
        for (Row& row : rows_) row.input->SetEnabled(has_clip_);
        muted_->SetEnabled(has_clip_);
        add_rect_btn_->SetEnabled(has_clip_);
        add_ellipse_btn_->SetEnabled(has_clip_);
        status_->SetText(has_clip_ ? "Edit values, then Apply" : "Ready");
        if (key_editor_ != nullptr) {
            key_editor_->SetClip(clip, active_prop_);
        }
        RebuildDynamicRows();
        SyncMaskRows();
        SyncFxRows();
        SyncKeyframeRows();
    }
    syncing_ = false;
    Invalidate();
}

void InspectorWidget::SetPlayhead(double timeline_seconds) {
    playhead_ = timeline_seconds;
    if (children_attached_) {
        SyncKeyframeRows();
        if (key_editor_ != nullptr) {
            key_editor_->SetPlayhead(timeline_seconds);
        }
    }
}

void InspectorWidget::CommitClip(const Clip& next) {
    clip_ = next;
    if (hooks_.on_apply_clip) hooks_.on_apply_clip(next);
    if (children_attached_) {
        // Structural fields changed — keep rows/effects text in sync.
        // syncing_ stays up across the row syncs: Checkbox::SetChecked
        // re-fires on_changed_ when the value actually differs.
        syncing_ = true;
        effects_input_->SetValue(FormatEffects(clip_.effects));
        link_input_->SetValue(clip_.link_group);
        blend_combo_->SetValue(BlendModeName(clip_.blend_mode));
        SyncMaskRows();
        SyncFxRows();
        syncing_ = false;
        SyncKeyframeRows();
        if (key_editor_ != nullptr) {
            key_editor_->SetClip(&clip_, active_prop_);
        }
    }
    RequestLayout();
    Invalidate();
}

void InspectorWidget::ApplyKeyframes(KeyProp prop,
                                     std::vector<Keyframe> keys) {
    if (!has_clip_) return;
    if (hooks_.on_set_keyframes) {
        clip_.keyframes[prop] = std::move(keys);   // optimistic local
        hooks_.on_set_keyframes(clip_.id, prop,
                              clip_.keyframes[prop]);
    } else {
        Clip next = clip_;
        next.keyframes[prop] = std::move(keys);
        CommitClip(next);
        return;
    }
    SyncKeyframeRows();
    if (key_editor_ != nullptr) key_editor_->SetClip(&clip_, active_prop_);
    Invalidate();
}

void InspectorWidget::ToggleKeyAtPlayhead(KeyProp prop) {
    if (!has_clip_) return;
    const double u = ClipLocalU();
    std::vector<Keyframe> keys;
    const auto it = clip_.keyframes.find(prop);
    if (it != clip_.keyframes.end()) keys = it->second;
    const auto at = std::find_if(keys.begin(), keys.end(),
                                 [u](const Keyframe& k) {
                                     return std::fabs(k.time - u) <
                                            kKeyTimeEpsilon;
                                 });
    if (at != keys.end()) {
        keys.erase(at);
        ApplyKeyframes(prop, std::move(keys));
        SetStatus(std::string("key removed: ") + KeyPropName(prop));
        return;
    }
    Keyframe key;
    key.time = u;
    key.value = EvaluateClipProperty(clip_, prop, u,
                                     KeyPropFallback(clip_, prop));
    keys.push_back(key);
    std::sort(keys.begin(), keys.end(),
              [](const Keyframe& a, const Keyframe& b) {
                  return a.time < b.time;
              });
    ApplyKeyframes(prop, std::move(keys));
    SetStatus(std::string("key added: ") + KeyPropName(prop));
}

void InspectorWidget::SeekKey(KeyProp prop, bool forward) {
    if (!has_clip_) return;
    const auto it = clip_.keyframes.find(prop);
    if (it == clip_.keyframes.end() || it->second.empty()) {
        SetStatus("no keys", true);
        return;
    }
    const double u = ClipLocalU();
    const std::vector<Keyframe>& keys = it->second;
    double target = -1.0;
    if (forward) {
        for (const Keyframe& k : keys) {
            if (k.time > u + kKeyTimeEpsilon) {
                target = k.time;
                break;
            }
        }
        if (target < 0.0) target = keys.back().time;
    } else {
        for (auto rit = keys.rbegin(); rit != keys.rend(); ++rit) {
            if (rit->time < u - kKeyTimeEpsilon) {
                target = rit->time;
                break;
            }
        }
        if (target < 0.0) target = keys.front().time;
    }
    if (hooks_.on_seek) {
        hooks_.on_seek(clip_.timeline_start + target);
    } else {
        SetStatus("on_seek not wired", true);
    }
}

bool InspectorWidget::ParseField(const Row& row, double* value,
                                 std::string* error_out) const {
    const std::string text = row.input->GetValue();
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (text.empty() || end == text.c_str() || *end != '\0' ||
        errno == ERANGE || !std::isfinite(parsed)) {
        if (error_out != nullptr) {
            *error_out = row.label + " must be a finite number";
        }
        return false;
    }
    *value = parsed;
    return true;
}

bool InspectorWidget::ParseEffects(std::vector<ClipEffect>* effects,
                                   std::string* error_out) const {
    if (effects == nullptr) return false;
    effects->clear();
    const std::string text = Trim(effects_input_->GetValue());
    if (text.empty()) return true;
    for (const std::string& raw_item : Split(text, ';')) {
        const std::string item = Trim(raw_item);
        if (item.empty()) continue;
        // "plugin:<id>" / "ocio:<name>" carry a ':' inside the type
        // itself — for those, the params boundary is the ':' AFTER the
        // prefix (FormatEffects writes "type:p1,p2", so an id with a
        // trailing ":p1,p2" still splits cleanly).
        std::size_t colon = item.find(':');
        if (item.rfind("plugin:", 0) == 0) {
            colon = item.find(':', 7);
        } else if (item.rfind("ocio:", 0) == 0) {
            colon = item.find(':', 5);
        }
        ClipEffect effect;
        effect.type = Trim(item.substr(0, colon));
        const bool prefixed =
            effect.type.rfind("plugin:", 0) == 0 ||
            effect.type.rfind("ocio:", 0) == 0;
        if (effect.type.empty() ||
            (!prefixed && KnownEffects().count(effect.type) == 0)) {
            if (error_out != nullptr) {
                *error_out = "Unknown effect '" + effect.type + "'";
            }
            return false;
        }
        if (colon != std::string::npos) {
            bool params_ok = true;
            std::vector<float> parsed;
            for (const std::string& raw_param :
                 Split(item.substr(colon + 1), ',')) {
                const std::string param = Trim(raw_param);
                if (param.empty()) continue;
                char* end = nullptr;
                errno = 0;
                const float value = std::strtof(param.c_str(), &end);
                if (end == param.c_str() || *end != '\0' ||
                    errno == ERANGE || !std::isfinite(value)) {
                    params_ok = false;
                    break;
                }
                parsed.push_back(value);
            }
            if (!params_ok && prefixed) {
                // e.g. "ocio:filmic:look" — the colon was part of the
                // name, not a params boundary. Prefixed types are
                // opaque pass-throughs: keep the whole token as the
                // type so FormatEffects round-trips it verbatim.
                effect.type = Trim(item);
            } else if (!params_ok) {
                if (error_out != nullptr) {
                    *error_out = "Invalid parameter for '" +
                                 effect.type + "'";
                }
                return false;
            } else {
                effect.params = std::move(parsed);
            }
        }
        effects->push_back(std::move(effect));
    }
    // Rows the user unchecked are dropped (see FxRow::enabled tooltip).
    // Indices were staged against clip_.effects (the fx basis), not this
    // freshly parsed list — remap by identity so a text-field reorder
    // or edit can't make the exclusion drop the wrong effect.
    const std::set<int> drops =
        RemapFxDisabled(fx_effects_basis_, fx_disabled_, *effects);
    for (auto rit = drops.rbegin(); rit != drops.rend(); ++rit) {
        effects->erase(effects->begin() + *rit);
    }
    return true;
}

bool InspectorWidget::ParseMasks(std::vector<ClipMask>* masks,
                                 std::string* error_out) const {
    if (masks == nullptr) return false;
    *masks = clip_.masks;   // shape + inverted come from the clip copy
    if (mask_rows_.size() != masks->size()) return true;  // stale rows
    using InputMember =
        std::shared_ptr<XER::UI::Widgets::TextInput> MaskRow::*;
    const std::pair<InputMember, float ClipMask::*> fields[] = {
        {&MaskRow::cx, &ClipMask::cx},
        {&MaskRow::cy, &ClipMask::cy},
        {&MaskRow::w, &ClipMask::w},
        {&MaskRow::h, &ClipMask::h},
        {&MaskRow::feather, &ClipMask::feather},
    };
    for (std::size_t i = 0; i < mask_rows_.size(); ++i) {
        for (const auto& [member, target] : fields) {
            const std::string text =
                (mask_rows_[i].*member)->GetValue();
            char* end = nullptr;
            errno = 0;
            const float value = std::strtof(text.c_str(), &end);
            if (text.empty() || end == text.c_str() || *end != '\0' ||
                errno == ERANGE || !std::isfinite(value)) {
                if (error_out != nullptr) {
                    char buffer[64];
                    std::snprintf(buffer, sizeof(buffer),
                                  "mask %zu fields must be finite", i + 1);
                    *error_out = buffer;
                }
                return false;
            }
            (*masks)[i].*target = value;
        }
    }
    return true;
}

void InspectorWidget::Apply() {
    if (!has_clip_) return;
    Clip next = clip_;
    double value = 0.0;
    double rate = clip_.playback_rate;
    std::string error;
    for (const Row& row : rows_) {
        if (!ParseField(row, &value, &error)) {
            SetStatus(error, true);
            return;
        }
        switch (row.field) {
            case Field::Start: next.timeline_start = value; break;
            case Field::InPoint: next.in_point = value; break;
            case Field::Duration: next.duration = value; break;
            case Field::Rate:
                if (value < 0.01 || value > 100.0) {
                    SetStatus("Speed must be within 0.01-100", true);
                    return;
                }
                rate = value;
                break;
            case Field::OffsetX:
                next.transform.offset_x = static_cast<float>(value); break;
            case Field::OffsetY:
                next.transform.offset_y = static_cast<float>(value); break;
            case Field::Scale:
                next.transform.scale = static_cast<float>(value); break;
            case Field::Rotation:
                next.transform.rotation_deg = static_cast<float>(value); break;
            case Field::Opacity:
                next.transform.opacity = static_cast<float>(value); break;
            case Field::GainDb: next.gain_db = static_cast<float>(value); break;
            case Field::Pan:
                if (value < -1.0 || value > 1.0) {
                    SetStatus("Pan must be within -1..1", true);
                    return;
                }
                next.pan = static_cast<float>(value);
                break;
            case Field::FadeIn: next.fade_in = value; break;
            case Field::FadeOut: next.fade_out = value; break;
        }
    }
    if (!ParseEffects(&next.effects, &error)) {
        SetStatus(error, true);
        return;
    }
    if (!ParseMasks(&next.masks, &error)) {
        SetStatus(error, true);
        return;
    }
    next.link_group = Trim(link_input_->GetValue());
    next.blend_mode = BlendModeFromName(
        blend_combo_->GetValue().empty() ? blend_combo_->GetText()
                                         : blend_combo_->GetValue(),
        clip_.blend_mode);
    next.muted = muted_->IsChecked();

    // Speed: mirror SetClipRate — keep the consumed source span so a
    // following ReplaceClip agrees with the dedicated model op.
    const bool rate_changed =
        std::fabs(rate - clip_.playback_rate) > 1e-9;
    if (rate_changed) {
        const double consumed = clip_.duration * clip_.playback_rate;
        const bool duration_edited =
            std::fabs(next.duration - clip_.duration) > 1e-9;
        if (!duration_edited) next.duration = consumed / rate;
        next.playback_rate = rate;
    }

    clip_ = next;
    // One undo step per Apply: ReplaceClip already carries the new
    // rate + the pre-adjusted duration, so firing on_set_rate as well
    // would stack a second history op for the same edit. on_set_rate
    // stays as the fallback for a host with no ApplyClip hook wired.
    if (hooks_.on_apply_clip) {
        hooks_.on_apply_clip(next);
    } else if (rate_changed && hooks_.on_set_rate) {
        hooks_.on_set_rate(clip_.id, rate);
    }
    if (key_editor_ != nullptr) key_editor_->SetClip(&clip_, active_prop_);
    // clip_ changed under the rows (effects may have been dropped,
    // masks re-validated) — resync for immediate consistency; the app's
    // SetClip round-trip then confirms.
    syncing_ = true;
    SyncMaskRows();
    SyncFxRows();
    syncing_ = false;
    SyncKeyframeRows();
    RequestLayout();   // drawn effect/mask readouts may have changed
}

void InspectorWidget::CommitTransition() {
    if (!has_clip_ || !hooks_.on_add_transition) return;
    // Label -> enum via the model's display table (the combo's options
    // are the same table); unmatched text falls back to Crossfade.
    const std::string label = transition_combo_->GetValue().empty()
                                  ? transition_combo_->GetText()
                                  : transition_combo_->GetValue();
    Transition::Type type = Transition::Type::Crossfade;
    for (const Transition::Type candidate : AllTransitionTypes()) {
        if (label == TransitionTypeLabel(candidate)) {
            type = candidate;
            break;
        }
    }
    const std::string text =
        Trim(transition_duration_input_->GetValue());
    char* end = nullptr;
    errno = 0;
    const double duration = std::strtod(text.c_str(), &end);
    if (text.empty() || end == text.c_str() || *end != '\0' ||
        errno == ERANGE || !std::isfinite(duration) || duration <= 0.0) {
        SetStatus("Transition duration must be a positive number", true);
        return;
    }
    hooks_.on_add_transition(clip_.id, type, duration);
}

void InspectorWidget::SetStatus(const std::string& text, bool is_error) {
    if (children_attached_) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void InspectorWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    if (has_clip_) {
        renderer.FillRect({bounds.x + 10.0f, bounds.y + 34.0f,
                           bounds.width - 20.0f, 3.0f},
                          tokens.accent, 2.0f);
    }
    // Drawn lines (section headers, inline labels, mask/fx/audiofx
    // readouts) — computed during Layout, clipped to the panel here.
    renderer.PushClip(bounds);
    for (const DrawnLine& line : drawn_) {
        if (line.rect.y + line.rect.height < bounds.y ||
            line.rect.y > bounds.y + bounds.height) {
            continue;
        }
        renderer.DrawText(
            line.text, line.rect,
            XER::UI::Rendering::TextStyle{
                line.header ? tokens.muted : tokens.foreground,
                line.header ? tokens.font_small : tokens.font_body,
                line.header ? 600 : 400});
    }
    renderer.PopClip();
}

void InspectorWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float inner_w = bounds.width - pad * 2.0f;
    const float inner_x = bounds.x + pad;

    drawn_.clear();
    // Content flows top->bottom offset by the scroll position; the
    // resulting height is fed back through content_height_.
    scroll_y_ = std::clamp(
        scroll_y_, 0.0f,
        std::max(0.0f, content_height_ - (bounds.height - pad * 2.0f)));
    float y = bounds.y + pad * 0.6f - scroll_y_;

    const auto place = [&](const std::shared_ptr<XER::UI::Widgets::Widget>& w,
                           float height) {
        w->Layout({inner_x, y, inner_w, height}, tokens);
        y += height;
    };
    const auto gap = [&](float g) { y += g; };
    const auto label = [&](const std::string& text, float height,
                           float x_offset = 0.0f,
                           bool header = false) {
        drawn_.push_back({{inner_x + x_offset, y,
                           inner_w - x_offset, height},
                          text, header});
        y += height;
    };

    place(title_, 24.0f);
    place(source_, 22.0f);
    gap(10.0f);

    // ---- details (CapCut-style clip summary, read-only) ----
    if (has_clip_) {
        label("DETAILS", 18.0f, 0.0f, true);
        const char* kind = "media";
        if (clip_.is_adjustment) kind = "adjustment";
        else if (clip_.source.kind == SourceKind::ColorMatte)
            kind = "color matte";
        else if (clip_.source.kind == SourceKind::ColorBars)
            kind = "color bars";
        else if (clip_.source.kind == SourceKind::Text)
            kind = "title";
        char line[160];
        std::snprintf(line, sizeof(line), "type      %s", kind);
        label(line, 16.0f);
        std::snprintf(line, sizeof(line), "rate      %.2fx",
                      clip_.playback_rate);
        label(line, 16.0f);
        std::snprintf(line, sizeof(line), "blend     %s",
                      BlendModeName(clip_.blend_mode));
        label(line, 16.0f);
        int key_count = 0;
        for (const auto& [prop, keys] : clip_.keyframes) {
            key_count += static_cast<int>(keys.size());
        }
        std::snprintf(line, sizeof(line), "masks %d   fx %d   keys %d",
                      static_cast<int>(clip_.masks.size()),
                      static_cast<int>(clip_.effects.size()), key_count);
        label(line, 16.0f);
        gap(6.0f);
    }

    const auto input_row = [&](Field field) {
        for (Row& row : rows_) {
            if (row.field == field) {
                place(row.input, 30.0f);
                gap(6.0f);
                return;
            }
        }
    };

    // ---- timing ----
    label("TIMING", 18.0f, 0.0f, true);
    input_row(Field::Start);
    input_row(Field::InPoint);
    input_row(Field::Duration);
    input_row(Field::Rate);
    gap(6.0f);

    // ---- transform ----
    label("TRANSFORM", 18.0f, 0.0f, true);
    input_row(Field::OffsetX);
    input_row(Field::OffsetY);
    input_row(Field::Scale);
    input_row(Field::Rotation);
    input_row(Field::Opacity);
    // Blend mode: drawn label + combobox inline.
    drawn_.push_back({{inner_x, y, 90.0f, 30.0f}, "Blend", false});
    blend_combo_->Layout(
        {inner_x + 92.0f, y, std::max(40.0f, inner_w - 92.0f), 30.0f},
        tokens);
    y += 36.0f;
    gap(4.0f);

    // ---- audio ----
    label("AUDIO", 18.0f, 0.0f, true);
    input_row(Field::GainDb);
    input_row(Field::Pan);
    input_row(Field::FadeIn);
    input_row(Field::FadeOut);
    gap(4.0f);

    // ---- link group ----
    label("LINK", 18.0f, 0.0f, true);
    place(link_input_, 30.0f);
    gap(2.0f);
    label("same group moves/trims together", 16.0f, 0.0f, true);
    gap(6.0f);

    // ---- keyframes ----
    label("KEYFRAMES", 18.0f, 0.0f, true);
    for (const KeyRow& row : key_rows_) {
        row.select->Layout({inner_x, y, inner_w - 116.0f, 26.0f}, tokens);
        row.toggle->Layout({inner_x + inner_w - 112.0f, y, 30.0f, 26.0f},
                           tokens);
        row.prev->Layout({inner_x + inner_w - 80.0f, y, 24.0f, 26.0f},
                         tokens);
        row.next->Layout({inner_x + inner_w - 54.0f, y, 24.0f, 26.0f},
                         tokens);
        row.clear->Layout({inner_x + inner_w - 28.0f, y, 26.0f, 26.0f},
                          tokens);
        y += 30.0f;
    }
    place(key_editor_, 140.0f);
    gap(8.0f);

    // ---- masks ----
    label("MASKS", 18.0f, 0.0f, true);
    add_rect_btn_->Layout({inner_x, y, 70.0f, 26.0f}, tokens);
    add_ellipse_btn_->Layout({inner_x + 76.0f, y, 80.0f, 26.0f}, tokens);
    y += 32.0f;
    for (std::size_t i = 0; i < mask_rows_.size(); ++i) {
        MaskRow& row = mask_rows_[i];
        const char* shape =
            i < clip_.masks.size()
                ? MaskShapeName(clip_.masks[i].shape)
                : "?";
        char heading[48];
        std::snprintf(heading, sizeof(heading), "mask %zu (%s)", i + 1,
                      shape);
        drawn_.push_back({{inner_x, y + 4.0f, inner_w - 64.0f, 20.0f},
                          heading, false});
        row.inverted->Layout(
            {inner_x + inner_w - 64.0f, y + 3.0f, 34.0f, 22.0f}, tokens);
        row.remove->Layout(
            {inner_x + inner_w - 26.0f, y + 1.0f, 24.0f, 24.0f}, tokens);
        y += 28.0f;
        const float field_w = (inner_w - 16.0f) / 5.0f;
        row.cx->Layout({inner_x, y, field_w, 28.0f}, tokens);
        row.cy->Layout({inner_x + (field_w + 4.0f), y, field_w, 28.0f},
                       tokens);
        row.w->Layout({inner_x + (field_w + 4.0f) * 2.0f, y, field_w,
                       28.0f},
                      tokens);
        row.h->Layout({inner_x + (field_w + 4.0f) * 3.0f, y, field_w,
                       28.0f},
                      tokens);
        row.feather->Layout(
            {inner_x + (field_w + 4.0f) * 4.0f, y, field_w, 28.0f},
            tokens);
        y += 34.0f;
    }
    gap(4.0f);

    // ---- video effects ----
    label("EFFECTS", 18.0f, 0.0f, true);
    place(effects_input_, 30.0f);
    gap(4.0f);
    for (std::size_t i = 0; i < fx_rows_.size(); ++i) {
        FxRow& row = fx_rows_[i];
        row.enabled->Layout({inner_x, y + 1.0f, 30.0f, 20.0f}, tokens);
        const std::string text =
            i < clip_.effects.size()
                ? FormatEffectBrief(clip_.effects[i])
                : std::string("?");
        drawn_.push_back({{inner_x + 34.0f, y + 2.0f, inner_w - 66.0f,
                           20.0f},
                          text, false});
        row.remove->Layout(
            {inner_x + inner_w - 26.0f, y, 24.0f, 24.0f}, tokens);
        y += 26.0f;
    }
    gap(4.0f);

    // ---- hosted audio fx (name + per-row Editor button) ----
    if (!clip_.audio_fx.empty()) {
        label("AUDIO FX (VST3)", 18.0f, 0.0f, true);
        constexpr float kEditorBtnW = 58.0f;
        for (std::size_t i = 0; i < clip_.audio_fx.size(); ++i) {
            const ClipAudioFx& fx = clip_.audio_fx[i];
            const std::string text =
                (fx.name.empty() ? Basename(fx.plugin_path) : fx.name) +
                (fx.enabled ? " — on" : " — bypassed");
            drawn_.push_back({{inner_x, y + 2.0f,
                               inner_w - kEditorBtnW - 6.0f, 18.0f},
                              text, false});
            if (i < afx_edit_btns_.size()) {
                const auto& btn = afx_edit_btns_[i];
                btn->Layout({inner_x + inner_w - kEditorBtnW, y,
                             kEditorBtnW, 22.0f},
                            tokens);
                btn->SetEnabled(hooks_.on_open_audio_fx_editor !=
                                nullptr);
            }
            y += 24.0f;
        }
        gap(4.0f);
    }

    // ---- transition picker ----
    label("TRANSITION", 18.0f, 0.0f, true);
    drawn_.push_back({{inner_x, y, 90.0f, 30.0f}, "Type", false});
    transition_combo_->Layout(
        {inner_x + 92.0f, y, std::max(40.0f, inner_w - 92.0f), 30.0f},
        tokens);
    y += 36.0f;
    drawn_.push_back({{inner_x, y, 90.0f, 30.0f}, "Dur (s)", false});
    transition_duration_input_->Layout(
        {inner_x + 92.0f, y, std::max(40.0f, inner_w - 92.0f), 30.0f},
        tokens);
    transition_btn_->Layout(
        {inner_x + inner_w - 78.0f, y, 78.0f, 30.0f}, tokens);
    y += 40.0f;

    // ---- actions ----
    muted_->Layout({inner_x, y + 3.0f, 42.0f, 24.0f}, tokens);
    drawn_.push_back(
        {{inner_x + 46.0f, y + 4.0f, 44.0f, 22.0f}, "muted", false});
    apply_btn_->Layout({inner_x + inner_w - 78.0f, y, 78.0f, 30.0f},
                       tokens);
    delete_btn_->Layout({inner_x, y, 76.0f, 30.0f}, tokens);
    y += 40.0f;
    place(status_, 24.0f);

    // Scrollable content height vs. the scrolled-out offset.
    content_height_ = (y - bounds.y - pad * 0.6f) + scroll_y_ + pad;
}

bool InspectorWidget::HandleEvent(const XER::UI::Widgets::Event& event) {
    if (event.type == XER::UI::Widgets::Event::Type::MouseWheel) {
        const auto bounds = GetBounds();
        const float view = bounds.height;
        const float range = std::max(0.0f, content_height_ - view);
        if (range <= 0.0f) return false;
        scroll_y_ = std::clamp(
            scroll_y_ - static_cast<float>(event.wheel_delta) * 0.5f, 0.0f,
            range);
        RequestLayout();
        Invalidate();
        return true;
    }
    if (event.type == XER::UI::Widgets::Event::Type::KeyDown &&
        event.key == XER::UI::Widgets::KeyCode::Enter && has_clip_) {
        Apply();
        return true;
    }
    return false;
}

void InspectorWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
