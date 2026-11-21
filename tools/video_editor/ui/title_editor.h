// Modal title editor for Text generated clips (SourceKind::Text) in the
// XER video editor. Same overlay pattern as SettingsDialog: dimmed
// backdrop, centered card, Escape/backdrop cancel, Apply commits.
//
// The dialog owns no project state — the shell pushes the clip's
// current style via SetStyle() before showing it and serializes the
// on_apply TitleStyle back into the clip's source param / fields.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
class Textarea;
class TextInput;
}

namespace XER::VideoEditor::Ui {

// Everything the editor exposes about a Text clip's look. `align` is
// 0 = left, 1 = center, 2 = right. Colors are RGBA bytes — the same
// convention as Marker::color in timeline_model.h.
struct TitleStyle {
    std::string text;
    int size_px = 64;
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
    int align = 1;
    int outline_px = 0;
    std::array<std::uint8_t, 4> outline_color{0, 0, 0, 255};
};

class TitleEditor final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Fired by Apply with the sanitized style. The host writes it
        // back to the clip and hides the overlay.
        std::function<void(const TitleStyle&)> on_apply;
        // Fired by Cancel / Escape / backdrop click. Optional — the
        // host usually just hides the overlay.
        std::function<void()> on_cancel;
    };

    explicit TitleEditor(Hooks hooks = {});

    // Fills every field — call before showing.
    void SetStyle(const TitleStyle& style);
    // Reads the current field values (clamped/sanitized; never throws).
    TitleStyle GetStyle() const;

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    void EnsureChildren();
    void Apply();
    void Cancel();
    // Advances the stored color through the palette on swatch click.
    void CycleFillColor();
    void CycleOutlineColor();
    void SelectAlign(int index);

    XER::UI::Layout::Rect PanelRect() const;
    XER::UI::Layout::Rect FillSwatchRect() const;
    XER::UI::Layout::Rect OutlineSwatchRect() const;

    Hooks hooks_;
    bool children_attached_ = false;
    int align_ = 1;
    std::array<std::uint8_t, 4> color_{255, 255, 255, 255};
    std::array<std::uint8_t, 4> outline_color_{0, 0, 0, 255};

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Textarea> text_area_;
    std::shared_ptr<XER::UI::Widgets::TextInput> size_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> outline_input_;
    std::shared_ptr<XER::UI::Widgets::Button> apply_button_;
    std::shared_ptr<XER::UI::Widgets::Button> cancel_button_;
};

}  // namespace XER::VideoEditor::Ui
