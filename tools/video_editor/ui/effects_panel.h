// Effects panel: searchable catalog of every ClipEffect.type the
// compositor dispatches (see Compositor::Impl::ApplyClipEffects in
// tools/video_editor/compositor.cc — the `type` table is mirrored in
// kCatalog below, with each type's default parameter vector taken from
// the compositor's documented fallbacks).
//
// Typing in the search box filters by name/description; selecting a row
// (click, double-click, or Enter) plus the "Add" button emits
// Hooks::on_add_effect with a ClipEffect carrying sane defaults — the
// app wraps the mutation in TimelineHistory and the inspector's effects
// section then exposes the params for editing.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
class TextInput;
}

namespace XER::VideoEditor::Ui {

class EffectsPanelWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        std::function<void(const std::string& clip_id,
                           const ClipEffect&)> on_add_effect;
        std::function<void(const std::string&, bool)> on_status;
    };

    explicit EffectsPanelWidget(Hooks hooks = {});

    // The clip new effects are applied to (nullptr = panel idles and
    // the Add button stays disabled).
    void SetClip(const Clip* clip);
    const std::string& ClipId() const { return clip_id_; }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {200.0f, 200.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {280.0f, 420.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    struct EffectInfo {
        std::string type;               // ClipEffect.type ("script:id" for JS)
        std::vector<float> default_params;
        std::string description;
    };

    static const std::vector<EffectInfo>& Catalog();

    void EnsureChildren();
    void RecomputeFilter();
    void UpdateAddEnabled();
    int RowAt(const XER::UI::Layout::Point& position) const;
    void AddSelected();
    void SetStatus(const std::string& text, bool is_error = false);

    Hooks hooks_;
    std::string clip_id_;
    std::vector<int> filtered_;   // indices into Catalog()
    int selected_ = -1;           // index into filtered_
    int hovered_ = -1;
    float scroll_y_ = 0.0f;
    float content_height_ = 0.0f;
    float rows_top_ = 0.0f;       // y where the list begins (layout)
    float rows_bottom_ = 0.0f;    // y where the list viewport ends
    bool children_attached_ = false;
    // Double-click detection: second click on the same row adds it.
    int last_click_row_ = -1;
    std::chrono::steady_clock::time_point last_click_time_{};

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::TextInput> search_;
    std::shared_ptr<XER::UI::Widgets::Button> add_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
};

}  // namespace XER::VideoEditor::Ui
