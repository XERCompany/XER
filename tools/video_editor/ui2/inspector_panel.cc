// InspectorPanel — see inspector_panel.h for the contract. Selection
// arrives as Clip COPIES from the provider; every edit runs through
// CommitAll -> InspectorHooks::on_commit_clips, which the shell maps
// to TimelineHistory::ReplaceClip (coalesced per clip). The panel
// keeps two tiny layout widgets local to this file — Column (vertical
// stack that reports its content height to the ScrollArea) and Row
// (label + control strip) — so section contents size themselves
// honestly inside the collapsible Cards.
#include "inspector_panel.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <utility>

#include "helper/string_utils.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/card.h"
#include "widgets/checkbox.h"
#include "widgets/context_menu.h"
#include "widgets/knob.h"
#include "widgets/label.h"
#include "widgets/scroll_area.h"
#include "widgets/scrub_number_field.h"
#include "widgets/select.h"
#include "widgets/slider.h"

// windows.h may have re-defined DrawText before we got here.
#ifdef DrawText
#undef DrawText
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;

constexpr float kRowH = 28.0f;
constexpr float kLabelW = 88.0f;
constexpr float kGap = 4.0f;

// ---- Column: vertical stack sized by children preferred heights ----
class Column final : public W::Widget {
public:
    void Layout(const Layout::Rect& bounds) override {
        Layout(bounds, CachedLayoutTokens());
    }
    void Layout(const Layout::Rect& bounds,
                const XER::UI::Theme::Tokens& tokens) override {
        SetBounds(bounds);
        float y = bounds.y;
        for (const auto& child : GetChildren()) {
            if (!child->IsVisible()) continue;
            float h = child->GetPreferredSize().height;
            const auto min_s = child->GetMinSize();
            const auto max_s = child->GetMaxSize();
            h = std::clamp(h, min_s.height, max_s.height);
            child->Layout({bounds.x, y, bounds.width, h}, tokens);
            y += h + gap_;
        }
        content_height_ = y - bounds.y - gap_;
    }
    void Render(XER::UI::Rendering::Renderer&,
                const XER::UI::Theme::Tokens&) override {}
    bool HandleEvent(const W::Event&) override { return false; }
    float GetContentHeight() const override {
        float h = 0.0f;
        bool first = true;
        for (const auto& child : GetChildren()) {
            if (!child->IsVisible()) continue;
            if (!first) h += gap_;
            h += std::max(0.0f, child->GetPreferredSize().height);
            first = false;
        }
        return h;
    }
    Layout::Size GetMinSize() const override {
        return {0.0f, GetContentHeight()};
    }
    Layout::Size GetPreferredSize() const override {
        float w = 0.0f;
        for (const auto& child : GetChildren()) {
            w = std::max(w, child->GetPreferredSize().width);
        }
        return {w, GetContentHeight()};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, GetContentHeight()};
    }
    void SetGap(float gap) { gap_ = gap; }

private:
    float gap_ = kGap;
    float content_height_ = 0.0f;
};

// ---- Row: fixed-height label + control strip -------------------------
// `spans` are fractional widths (sum ~1) for the control children; a
// span of 0 gives the child its preferred width. The optional label
// occupies the leading kLabelW.
class Row final : public W::Widget {
public:
    Row(std::string label, std::vector<std::shared_ptr<Widget>> controls,
        std::vector<float> spans)
        : label_(std::move(label)),
          controls_(std::move(controls)),
          spans_(std::move(spans)) {
        for (const auto& c : controls_) AddChild(c);
    }
    void Layout(const Layout::Rect& bounds) override {
        Layout(bounds, CachedLayoutTokens());
    }
    void Layout(const Layout::Rect& bounds,
                const XER::UI::Theme::Tokens& tokens) override {
        SetBounds(bounds);
        float x = bounds.x;
        const float label_w = label_.empty() ? 0.0f : kLabelW;
        float avail = bounds.width - label_w;
        if (!label_.empty()) {
            // Label is drawn, not a child — keeps row widget count low.
        }
        // Preferred widths for zero-span children.
        float fixed = 0.0f;
        float span_sum = 0.0f;
        for (std::size_t i = 0; i < controls_.size(); ++i) {
            const float s = i < spans_.size() ? spans_[i] : 1.0f;
            if (s <= 0.0f) {
                fixed += controls_[i]->GetPreferredSize().width + kGap;
            } else {
                span_sum += s;
            }
        }
        const float flex = std::max(0.0f, avail - fixed);
        x += label_w;
        for (std::size_t i = 0; i < controls_.size(); ++i) {
            const float s = i < spans_.size() ? spans_[i] : 1.0f;
            float w = s <= 0.0f ? controls_[i]->GetPreferredSize().width
                                : span_sum > 0.0f ? flex * s / span_sum
                                                  : 0.0f;
            controls_[i]->Layout({x, bounds.y, w, bounds.height},
                                 tokens);
            x += w + kGap;
        }
    }
    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        if (!label_.empty()) {
            const Layout::Rect b = GetBounds();
            renderer.DrawText(
                label_, {b.x + 2.0f, b.y, kLabelW - 4.0f, b.height},
                {tokens.muted, Theme::kFontCaption, 500});
        }
    }
    bool HandleEvent(const W::Event&) override { return false; }
    Layout::Size GetMinSize() const override { return {80.0f, kRowH}; }
    Layout::Size GetPreferredSize() const override {
        return {240.0f, kRowH};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, kRowH};
    }

private:
    std::string label_;
    std::vector<std::shared_ptr<Widget>> controls_;
    std::vector<float> spans_;
};

std::shared_ptr<Row> FieldRow(const std::string& label,
                              std::shared_ptr<W::Widget> field) {
    return std::make_shared<Row>(label,
                                 std::vector<std::shared_ptr<W::Widget>>{std::move(field)},
                                 std::vector<float>{1.0f});
}

// Blend-mode display names — enum order (BlendMode in
// timeline_model.h, 29 entries: the classic seven + the Skia set).
const char* kBlendNames[] = {
    "Normal",       "Add",         "Multiply",   "Screen",
    "Overlay",      "Darken",      "Lighten",    "Clear",
    "Src",          "Dst",         "Src Over",   "Dst Over",
    "Src In",       "Dst In",      "Src Out",    "Dst Out",
    "Src ATop",     "Dst ATop",    "Xor",        "Plus",
    "Modulate",     "Color Dodge", "Color Burn", "Hard Light",
    "Soft Light",   "Difference",  "Exclusion",  "Hue",
    "Saturation",   "Color",       "Luminosity",
};
constexpr int kBlendCount =
    static_cast<int>(sizeof(kBlendNames) / sizeof(kBlendNames[0]));

}  // namespace

std::shared_ptr<InspectorPanel> InspectorPanel::Create() {
    return std::shared_ptr<InspectorPanel>(new InspectorPanel());
}

InspectorPanel::InspectorPanel() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void InspectorPanel::SetHooks(InspectorHooks hooks) {
    hooks_ = std::move(hooks);
}

void InspectorPanel::SetSelectionProvider(
    std::function<std::vector<Clip>()> provider) {
    selection_provider_ = std::move(provider);
    RefreshSelection();
}

void InspectorPanel::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    title_ = std::make_shared<W::Label>("No selection", true);

    auto column = std::make_shared<Column>();
    column->SetGap(Theme::kSpaceSm);
    column_ = column;
    scroll_ = std::make_shared<W::ScrollArea>(column_);

    const auto make_card = [](const char* title, bool open = true) {
        auto card = std::make_shared<W::Card>();
        card->SetTitle(title);
        card->SetCollapsible(true);
        card->SetCollapsed(!open);
        card->SetDensity(W::Card::Density::Compact);
        return card;
    };

    // ---- TRANSFORM --------------------------------------------------
    transform_card_ = make_card("Transform");
    {
        auto col = std::make_shared<Column>();
        col->SetGap(2.0f);
        pos_x_ = MakeField(-1e6f, 1e6f, 0.001f, "",
                           [this](float v) {
                               ApplyTransformField(TransformField::PosX, v);
                           });
        pos_y_ = MakeField(-1e6f, 1e6f, 0.001f, "",
                           [this](float v) {
                               ApplyTransformField(TransformField::PosY, v);
                           });
        scale_ = MakeField(0.0f, 8.0f, 0.01f, "x",
                           [this](float v) {
                               ApplyTransformField(TransformField::Scale, v);
                           });
        rotation_ =
            MakeField(-3600.0f, 3600.0f, 0.5f, "deg",
                      [this](float v) {
                          ApplyTransformField(TransformField::Rotation, v);
                      });
        opacity_ = MakeField(0.0f, 1.0f, 0.01f, "",
                             [this](float v) {
                                 ApplyTransformField(TransformField::Opacity,
                                                     v);
                             });
        opacity_->SetFormat("%.2f");
        col->AddChild(FieldRow("Position X", pos_x_));
        col->AddChild(FieldRow("Position Y", pos_y_));
        col->AddChild(FieldRow("Scale", scale_));
        col->AddChild(FieldRow("Rotation", rotation_));
        col->AddChild(FieldRow("Opacity", opacity_));
        transform_card_->SetContent(col);
    }

    // ---- SPEED & DURATION ---------------------------------------------
    speed_card_ = make_card("Speed & Duration");
    {
        auto col = std::make_shared<Column>();
        col->SetGap(2.0f);
        speed_ = MakeField(1.0f, 1000.0f, 1.0f, "%",
                           [this](float v) { ApplySpeedPercent(v); });
        speed_->SetFormat("%.0f");
        duration_ =
            MakeField(0.001f, 1e6f, 0.1f, "s",
                      [this](float v) {
                          ApplyDurationSeconds(static_cast<double>(v));
                      });
        duration_->SetFormat("%.2f");
        col->AddChild(FieldRow("Speed", speed_));
        col->AddChild(FieldRow("Duration", duration_));
        speed_card_->SetContent(col);
    }

    // ---- BLEND ---------------------------------------------------------
    blend_card_ = make_card("Blend");
    {
        auto col = std::make_shared<Column>();
        col->SetGap(2.0f);
        blend_select_ = std::make_shared<W::Select>(
            std::vector<std::string>(kBlendNames,
                                     kBlendNames + kBlendCount),
            0);
        blend_select_->SetTooltip("Photoshop-style layer blend mode");
        blend_select_->SetOnChanged([this](int index) {
            if (syncing_) return;
            if (index >= 0 && index < kBlendCount) {
                ApplyBlendMode(static_cast<BlendMode>(index));
            }
        });
        col->AddChild(FieldRow("Mode", blend_select_));
        blend_card_->SetContent(col);
    }

    // ---- FX RACK ----------------------------------------------------------
    fx_card_ = make_card("FX Rack");
    {
        auto col = std::make_shared<Column>();
        col->SetGap(2.0f);
        fx_column_ = col;
        fx_add_btn_ = std::make_shared<W::Button>(
            "+ Add FX", W::ButtonStyle::Secondary);
        fx_add_btn_->SetSize(W::ButtonSize::Small);
        fx_add_btn_->SetTooltip("Append an effect to the clip's chain");
        fx_add_btn_->SetOnClick([this] {
            // Catalog menu is built lazily on click (the shell's
            // fx_catalog provider may not be set at attach time).
            if (!hooks_.fx_catalog) return;
            std::vector<W::ContextMenu::Item> items;
            for (const auto& [type, name] : hooks_.fx_catalog()) {
                W::ContextMenu::Item item;
                item.label = name;
                item.action = [this, type] { AddEffect(type); };
                items.push_back(std::move(item));
            }
            if (items.empty()) {
                W::ContextMenu::Item none;
                none.label = "No effects available";
                none.enabled = false;
                items.push_back(std::move(none));
            }
            auto menu = W::ContextMenu::Create(std::move(items));
            menu->SetAnchor(fx_add_btn_->GetBounds().TopLeft());
            ShowOverlay(menu);
        });
        col->AddChild(std::make_shared<Row>(
            "", std::vector<std::shared_ptr<W::Widget>>{fx_add_btn_},
            std::vector<float>{1.0f}));
        fx_card_->SetContent(col);
    }

    // ---- AUDIO --------------------------------------------------------------
    audio_card_ = make_card("Audio");
    {
        auto col = std::make_shared<Column>();
        col->SetGap(2.0f);
        volume_knob_ = W::Knob::Create();
        volume_knob_->SetRange(-60.0f, 12.0f);
        volume_knob_->SetLabel("Gain");
        volume_knob_->SetUnit("dB");
        volume_knob_->SetAnchor(0.0f);
        volume_knob_->SetOnValueChange([this](float v) {
            if (!syncing_) {
                BeginGesture();
                ApplyGainDb(v);
            }
        });
        volume_knob_->SetOnValueCommitted([this](float) { EndGesture(); });
        pan_slider_ = std::make_shared<W::Slider>();
        pan_slider_->SetRange(-1.0f, 1.0f);
        pan_slider_->SetOnValueChanged([this](float v) {
            if (!syncing_) {
                BeginGesture();
                ApplyPan(v);
                EndGesture();   // slider has no committed hook — a
                                // drag still coalesces via ReplaceClip
            }
        });
        mute_check_ = std::make_shared<W::Checkbox>(false);
        mute_check_->SetOnChanged([this](bool checked) {
            if (!syncing_) ApplyMuted(checked);
        });
        voice_isolate_btn_ = std::make_shared<W::Button>(
            "Voice Isolate (AI)", W::ButtonStyle::Secondary);
        voice_isolate_btn_->SetSize(W::ButtonSize::Small);
        voice_isolate_btn_->SetTooltip(
            "Run GTCRN voice isolation on this clip's audio");
        voice_isolate_btn_->SetOnClick([this] { ApplyVoiceIsolate(); });

        col->AddChild(FieldRow("Volume", volume_knob_));
        col->AddChild(FieldRow("Pan", pan_slider_));
        col->AddChild(FieldRow("Mute", mute_check_));
        col->AddChild(FieldRow("AI", voice_isolate_btn_));
        audio_card_->SetContent(col);
    }

    scroll_->SetContent(column_);
    column->AddChild(transform_card_);
    column->AddChild(speed_card_);
    column->AddChild(blend_card_);
    column->AddChild(fx_card_);
    column->AddChild(audio_card_);

    AddChild(title_);
    AddChild(scroll_);
}

std::shared_ptr<W::ScrubNumberField> InspectorPanel::MakeField(
    float minimum, float maximum, float step,
    const std::string& unit,
    std::function<void(float)> on_change,
    std::function<void(float)> on_commit) {
    auto field = W::ScrubNumberField::Create();
    field->SetRange(minimum, maximum);
    field->SetStep(step);
    field->SetSmallStep(step * 0.1f);
    field->SetLargeStep(step * 10.0f);
    if (!unit.empty()) field->SetUnit(unit);
    field->SetFormat("%.3g");
    field->SetAllowWheelScrub(true);
    field->SetBoundFeedback(W::ScrubNumberField::BoundFeedback::Flash);
    field->SetOnValueChange([this, on_change = std::move(on_change)](
                                float v) {
        if (syncing_) return;
        BeginGesture();
        on_change(v);
    });
    field->SetOnValueCommitted([this, on_commit = std::move(on_commit)](
                                   float v) {
        if (syncing_) return;
        if (on_commit) on_commit(v);
        EndGesture();
    });
    return field;
}

// ---- selection -------------------------------------------------------

Clip* InspectorPanel::Primary() {
    return selected_.empty() ? nullptr : &selected_.front();
}

const Clip* InspectorPanel::Primary() const {
    return selected_.empty() ? nullptr : &selected_.front();
}

void InspectorPanel::RefreshSelection() {
    std::vector<Clip> next =
        selection_provider_ ? selection_provider_()
                            : std::vector<Clip>{};
    std::string key;
    for (const Clip& c : next) {
        key += c.id;
        key += ';';
    }
    const bool selection_changed = key != selection_key_;
    selection_key_ = key;
    selected_ = std::move(next);
    if (selection_changed) {
        // New selection — the FX bypass staging belongs to the old
        // clip; expanded rows reset too.
        bypassed_fx_.clear();
        expanded_fx_.clear();
        fx_basis_sig_.clear();
    }
    RebuildSections();
    SyncFieldsFromModel();
    Invalidate();
}

// ---- commit funnel ---------------------------------------------------

template <typename Mut>
void InspectorPanel::CommitAll(Mut&& mutator) {
    if (selected_.empty()) return;
    for (Clip& clip : selected_) {
        mutator(clip);
    }
    if (hooks_.on_commit_clips) {
        hooks_.on_commit_clips(selected_);
    }
}

void InspectorPanel::BeginGesture() {
    if (in_gesture_) return;
    in_gesture_ = true;
    if (hooks_.on_begin_macro) hooks_.on_begin_macro();
}

void InspectorPanel::EndGesture() {
    if (!in_gesture_) return;
    in_gesture_ = false;
    if (hooks_.on_end_macro) hooks_.on_end_macro();
}

// ---- verbs ---------------------------------------------------------------

void InspectorPanel::ApplyTransformField(TransformField field,
                                         float value) {
    CommitAll([field, value](Clip& c) {
        switch (field) {
            case TransformField::PosX: c.transform.offset_x = value; break;
            case TransformField::PosY: c.transform.offset_y = value; break;
            case TransformField::Scale:
                c.transform.scale = std::max(0.0f, value);
                break;
            case TransformField::Rotation:
                c.transform.rotation_deg = value;
                break;
            case TransformField::Opacity:
                c.transform.opacity = std::clamp(value, 0.0f, 1.0f);
                break;
        }
    });
}

void InspectorPanel::ApplySpeedPercent(float percent) {
    CommitAll([percent](Clip& c) {
        const double rate =
            std::clamp(static_cast<double>(percent) / 100.0, 0.01,
                       100.0);
        c.playback_rate = rate;
    });
}

void InspectorPanel::ApplyDurationSeconds(double seconds) {
    CommitAll([seconds](Clip& c) {
        c.duration = std::max(0.001, seconds);
    });
}

void InspectorPanel::ApplyBlendMode(BlendMode mode) {
    CommitAll([mode](Clip& c) { c.blend_mode = mode; });
}

void InspectorPanel::ApplyGainDb(float gain_db) {
    CommitAll([gain_db](Clip& c) {
        c.gain_db = std::clamp(gain_db, -60.0f, 24.0f);
    });
}

void InspectorPanel::ApplyPan(float pan) {
    CommitAll([pan](Clip& c) {
        c.pan = std::clamp(pan, -1.0f, 1.0f);
    });
}

void InspectorPanel::ApplyMuted(bool muted) {
    CommitAll([muted](Clip& c) { c.muted = muted; });
}

void InspectorPanel::SetEffectParam(int effect_index, int param_index,
                                    float value) {
    CommitAll([effect_index, param_index, value](Clip& c) {
        if (effect_index < 0 ||
            effect_index >= static_cast<int>(c.effects.size())) {
            return;
        }
        auto& params = c.effects[static_cast<std::size_t>(effect_index)]
                           .params;
        if (param_index < 0 ||
            param_index >= static_cast<int>(params.size())) {
            return;
        }
        params[static_cast<std::size_t>(param_index)] = value;
    });
}

std::string InspectorPanel::EffectSignature(const ClipEffect& e) {
    std::string sig = e.type;
    sig += '(';
    for (const float p : e.params) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.4g,", p);
        sig += buf;
    }
    sig += ')';
    return sig;
}

void InspectorPanel::SetEffectBypassed(int effect_index,
                                       bool bypassed) {
    if (bypassed) {
        // Commit the removal; keep the effect + its slot so the row
        // lingers grayed and can be re-enabled in place.
        const Clip* primary = Primary();
        if (primary == nullptr || effect_index < 0 ||
            effect_index >= static_cast<int>(primary->effects.size())) {
            return;
        }
        BypassedFx stashed;
        stashed.effect =
            primary->effects[static_cast<std::size_t>(effect_index)];
        stashed.index = effect_index;
        bypassed_fx_.push_back(stashed);
        CommitAll([effect_index](Clip& c) {
            if (effect_index < 0 ||
                effect_index >=
                    static_cast<int>(c.effects.size())) {
                return;
            }
            c.effects.erase(
                c.effects.begin() + effect_index);
        });
        RebuildSections();
        return;
    }
    // Re-enable: find the bypassed entry whose index lines up — the
    // lingering row passes its stored index.
    for (auto it = bypassed_fx_.begin(); it != bypassed_fx_.end();
         ++it) {
        if (it->index == effect_index) {
            const ClipEffect restore = it->effect;
            const int slot = it->index;
            bypassed_fx_.erase(it);
            CommitAll([restore, slot](Clip& c) {
                const int at = std::clamp(
                    slot, 0, static_cast<int>(c.effects.size()));
                c.effects.insert(c.effects.begin() + at, restore);
            });
            RebuildSections();
            return;
        }
    }
}

void InspectorPanel::RemoveEffect(int effect_index) {
    CommitAll([effect_index](Clip& c) {
        if (effect_index < 0 ||
            effect_index >= static_cast<int>(c.effects.size())) {
            return;
        }
        c.effects.erase(c.effects.begin() + effect_index);
    });
    // Removing an active effect can shift the stored slots of pending
    // bypass rows — drop stale entries at/after that slot so a later
    // re-enable never lands out of range.
    for (auto it = bypassed_fx_.begin(); it != bypassed_fx_.end();) {
        if (it->index >= effect_index) {
            it = bypassed_fx_.erase(it);
        } else {
            ++it;
        }
    }
    RebuildSections();
}

void InspectorPanel::AddEffect(const std::string& fx_type) {
    if (fx_type.empty()) return;
    std::vector<float> params;
    if (hooks_.fx_param_specs) {
        for (const InspectorParamSpec& spec :
             hooks_.fx_param_specs(fx_type)) {
            params.push_back(spec.default_value);
        }
    }
    CommitAll([fx_type, params = std::move(params)](Clip& c) {
        c.effects.push_back({fx_type, params});
    });
    RebuildSections();
}

void InspectorPanel::ApplyVoiceIsolate() {
    if (!hooks_.on_apply_voice_isolate) return;
    std::vector<std::string> ids;
    for (const Clip& c : selected_) ids.push_back(c.id);
    hooks_.on_apply_voice_isolate(ids);
}

// ---- section rebuild / field sync ------------------------------------------

void InspectorPanel::RebuildSections() {
    EnsureChildren();
    const Clip* primary = Primary();
    if (title_ != nullptr) {
        if (primary == nullptr) {
            title_->SetText("No selection");
        } else if (selected_.size() == 1) {
            std::string name = primary->name.empty()
                                   ? primary->source_path
                                   : primary->name;
            const std::size_t pos = name.find_last_of("/\\");
            if (pos != std::string::npos) name = name.substr(pos + 1);
            title_->SetText(name.empty() ? "Clip" : name);
        } else {
            title_->SetText(std::to_string(selected_.size()) +
                            " clips selected");
        }
    }
    if (voice_isolate_btn_ != nullptr) {
        const bool show =
            primary != nullptr && hooks_.voice_isolate_available &&
            hooks_.voice_isolate_available(*primary);
        voice_isolate_btn_->SetVisible(show);
    }

    // FX rows rebuild only when the effects signature changes —
    // param scrubs re-sync into the same row widgets otherwise.
    std::string sig = selection_key_;
    if (primary != nullptr) {
        for (const ClipEffect& e : primary->effects) {
            sig += EffectSignature(e);
        }
    }
    sig += std::to_string(bypassed_fx_.size());
    if (sig != fx_basis_sig_ && fx_column_ != nullptr) {
        fx_basis_sig_ = sig;
        RebuildFxRows();
    }
    if (fx_card_ != nullptr && fx_card_->IsVisible() !=
                                  (primary != nullptr)) {
        // Keep the FX rack visible only with a selection.
        fx_card_->SetVisible(primary != nullptr);
    }
    const bool has_selection = primary != nullptr;
    for (W::Card* card :
         {transform_card_.get(), speed_card_.get(), blend_card_.get(),
          audio_card_.get()}) {
        if (card != nullptr && card->IsVisible() != has_selection) {
            card->SetVisible(has_selection);
        }
    }
    RequestLayout();
    Invalidate();
}

void InspectorPanel::RebuildFxRows() {
    if (fx_column_ == nullptr) return;
    // fx_column_ is the Column holding [per-effect rows..., add row].
    // Rebuild all but the trailing add-row.
    auto children = fx_column_->GetChildren();
    while (children.size() > 1) {
        fx_column_->RemoveChild(children.front());
        children = fx_column_->GetChildren();
    }
    const Clip* primary = Primary();
    if (primary == nullptr) return;

    // Active rows — eye on, expandable, removable.
    for (int i = 0; i < static_cast<int>(primary->effects.size());
         ++i) {
        const ClipEffect& fx =
            primary->effects[static_cast<std::size_t>(i)];
        const std::string name =
            hooks_.fx_display_name ? hooks_.fx_display_name(fx.type)
                                   : fx.type;
        auto eye = std::make_shared<W::Checkbox>(true);
        eye->SetTooltip("Uncheck to bypass (removes from chain — "
                        "re-check restores)");
        const int index = i;
        eye->SetOnChanged([this, index](bool checked) {
            if (!checked) SetEffectBypassed(index, true);
        });
        auto expand = std::make_shared<W::Button>(
            expanded_fx_.count(i) ? "v" : ">", W::ButtonStyle::Ghost);
        expand->SetSize(W::ButtonSize::Small);
        expand->SetTooltip("Show parameters");
        expand->SetOnClick([this, index] {
            if (expanded_fx_.count(index)) {
                expanded_fx_.erase(index);
            } else {
                expanded_fx_.insert(index);
            }
            fx_basis_sig_.clear();   // force row rebuild
            RebuildSections();
        });
        auto label = std::make_shared<W::Label>(name, true);
        auto remove = std::make_shared<W::Button>(
            "x", W::ButtonStyle::Ghost);
        remove->SetSize(W::ButtonSize::Small);
        remove->SetTooltip("Remove effect");
        remove->SetOnClick([this, index] { RemoveEffect(index); });

        auto row_col = std::make_shared<Column>();
        row_col->SetGap(1.0f);
        row_col->AddChild(std::make_shared<Row>(
            "", std::vector<std::shared_ptr<W::Widget>>{
                    eye, expand, label, remove},
            std::vector<float>{0.0f, 0.0f, 1.0f, 0.0f}));

        if (expanded_fx_.count(i)) {
            const auto specs =
                hooks_.fx_param_specs ? hooks_.fx_param_specs(fx.type)
                                      : std::vector<InspectorParamSpec>{};
            for (int p = 0;
                 p < static_cast<int>(fx.params.size()); ++p) {
                InspectorParamSpec spec;
                spec.name = "param " + std::to_string(p);
                if (p < static_cast<int>(specs.size())) {
                    spec = specs[static_cast<std::size_t>(p)];
                }
                const int param_index = p;
                auto field = W::ScrubNumberField::Create();
                field->SetRange(spec.minimum, spec.maximum);
                field->SetStep(spec.step);
                field->SetValue(
                    fx.params[static_cast<std::size_t>(p)]);
                field->SetFormat("%.3g");
                field->SetOnValueChange(
                    [this, index, param_index](float v) {
                        if (syncing_) return;
                        BeginGesture();
                        SetEffectParam(index, param_index, v);
                    });
                field->SetOnValueCommitted(
                    [this](float) { EndGesture(); });
                row_col->AddChild(FieldRow(spec.name, field));
            }
        }
        // Insert before the trailing add-row.
        fx_column_->AddChild(row_col);
    }

    // Bypassed rows — grayed, eye off, restore/remove only.
    for (const BypassedFx& stashed : bypassed_fx_) {
        const std::string name =
            (hooks_.fx_display_name
                 ? hooks_.fx_display_name(stashed.effect.type)
                 : stashed.effect.type) +
            "  (bypassed)";
        auto eye = std::make_shared<W::Checkbox>(false);
        const int slot = stashed.index;
        eye->SetTooltip("Re-check to restore this effect");
        eye->SetOnChanged([this, slot](bool checked) {
            if (checked) SetEffectBypassed(slot, false);
        });
        auto label = std::make_shared<W::Label>(name, true);
        auto row = std::make_shared<Row>(
            "", std::vector<std::shared_ptr<W::Widget>>{eye, label},
            std::vector<float>{0.0f, 1.0f});
        fx_column_->AddChild(row);
    }

    // Keep the add-row LAST: RebuildFxRows currently appends — reorder
    // by removing + re-adding the add row if it isn't last.
    children = fx_column_->GetChildren();
    for (const auto& child : children) {
        const auto& grand = child->GetChildren();
        for (const auto& g : grand) {
            if (g == fx_add_btn_) {
                fx_column_->RemoveChild(child);
                fx_column_->AddChild(child);
                return;
            }
        }
    }
}

void InspectorPanel::SyncFieldsFromModel() {
    const Clip* primary = Primary();
    if (primary == nullptr) return;
    syncing_ = true;
    const auto restore = [this] { syncing_ = false; };
    if (pos_x_ != nullptr) pos_x_->SetValue(primary->transform.offset_x);
    if (pos_y_ != nullptr) pos_y_->SetValue(primary->transform.offset_y);
    if (scale_ != nullptr) scale_->SetValue(primary->transform.scale);
    if (rotation_ != nullptr) {
        rotation_->SetValue(primary->transform.rotation_deg);
    }
    if (opacity_ != nullptr) {
        opacity_->SetValue(primary->transform.opacity);
    }
    if (speed_ != nullptr) {
        speed_->SetValue(
            static_cast<float>(primary->playback_rate * 100.0));
    }
    if (duration_ != nullptr) {
        duration_->SetValue(static_cast<float>(primary->duration));
    }
    if (blend_select_ != nullptr) {
        blend_select_->SetSelectedIndex(
            static_cast<int>(primary->blend_mode));
    }
    if (volume_knob_ != nullptr) volume_knob_->SetValue(primary->gain_db);
    if (pan_slider_ != nullptr) pan_slider_->SetValue(primary->pan);
    if (mute_check_ != nullptr) mute_check_->SetChecked(primary->muted);
    restore();
}

// ---- widget contract ------------------------------------------------------

void InspectorPanel::Render(XER::UI::Rendering::Renderer& renderer,
                            const XER::UI::Theme::Tokens& tokens) {
    renderer.FillRect(GetBounds(), tokens.panel, 0.0f);
}

void InspectorPanel::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float x = bounds.x + Theme::kSpaceSm;
    const float w = bounds.width - Theme::kSpaceSm * 2.0f;
    float y = bounds.y + Theme::kSpaceSm;
    title_->Layout({x, y, w, 20.0f}, tokens);
    y += 20.0f + 4.0f;
    scroll_->Layout(
        {bounds.x + Theme::kSpaceSm / 2.0f, y,
         bounds.width - Theme::kSpaceSm,
         bounds.y + bounds.height - Theme::kSpaceSm - y},
        tokens);
}

void InspectorPanel::Tick(double /*dt_seconds*/) {
    // Card collapse animations RequestLayout themselves; nothing
    // to drain here — selection changes arrive via RefreshSelection.
}

}  // namespace XER::VideoEditor::Ui2
