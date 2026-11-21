// Modal title editor for Text generated clips (SourceKind::Text) in the
// XER video editor. Same overlay pattern as SettingsDialog: dimmed
// backdrop, centered card, Escape/backdrop cancel, Apply commits.
//
// The dialog owns no project state — the shell pushes the clip's
// current style via SetStyle() before showing it and serializes the
// on_apply TitleStyle back into the clip's source param / fields via
// SerializeClipTextStyle (text_style.h).
//
// TitleStyle mirrors ClipTextStyle 1:1 but stays UI-friendly: colors
// are RGBA bytes (ClipTextStyle persists BGRA) and enum fields are
// plain indices for the align/valign chip strips.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "frame.h"
#include "layout/geometry.h"
#include "text_style.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class ButtonGroup;
class Combobox;
class Label;
class Slider;
class Textarea;
class TextInput;
class Toggle;
}

namespace XER::VideoEditor::Ui {

// Everything the editor exposes about a Text clip's look. `align`/
// `valign` are 0/1/2 (left|center|right, top|middle|bottom). Colors
// are RGBA bytes — the same convention as Marker::color in
// timeline_model.h. `*_px` fields are pixels at the rendered size;
// box w/h of 0 means "fit the text".
struct TitleStyle {
    std::string text;
    std::string font_family;   // "" -> platform default UI face
    int font_weight = 400;     // 100..900
    bool italic = false;
    bool underline = false;
    bool strike = false;
    int size_px = 64;
    float letter_spacing = 0.0f;
    float line_spacing = 0.0f;  // absolute px advance; 0 -> auto
    int align = 1;
    int valign = 0;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float corner_radius = 0.0f;
    float rotate_deg = 0.0f;
    float baseline_px = 0.0f;
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};  // RGBA fill
    float opacity = 1.0f;       // 0..1
    float outline_px = 0.0f;
    std::array<std::uint8_t, 4> outline_color{0, 0, 0, 255};
    std::array<std::uint8_t, 4> bg_color{0, 0, 0, 0};       // box bg
    bool shadow_explicit = false;  // false -> automatic drop shadow
    float shadow_x = 0.0f;
    float shadow_y = 0.0f;
    float shadow_blur = 0.0f;
    std::array<std::uint8_t, 4> shadow_color{0, 0, 0, 128};
};

// TitleStyle <-> ClipTextStyle converters (RGBA <-> BGRA color swap
// plus enum/index mapping) — the app serializes clips through these,
// never by hand-mangling the param string.
ClipTextStyle TitleStyleToClipStyle(const TitleStyle& style);
TitleStyle TitleStyleFromClipStyle(const ClipTextStyle& style);

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

    // Applies a font choice from the Font Manager (family + weight +
    // slant) without touching the rest of the style.
    void SetFont(const std::string& family, int weight, bool italic);

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
    void SelectAlign(int index);
    void SelectVAlign(int index);
    void RefreshWeightOptions();     // styles of the chosen family
    void RefreshFontOptions();       // FontCatalog family list
    void MarkPreviewDirty();
    void UpdatePreview();            // rasterizes GetStyle() preview
    // `which`: 0 = align strip, 1 = valign strip.
    void SyncStripButtons(int which);

    XER::UI::Layout::Rect PanelRect() const;
    XER::UI::Layout::Rect PreviewRect() const;

    Hooks hooks_;
    bool children_attached_ = false;
    int align_ = 1;
    int valign_ = 0;
    bool preview_dirty_ = true;
    video::Frame preview_frame_;
    int preview_w_ = 0;
    int preview_h_ = 0;

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Textarea> text_area_;
    std::shared_ptr<XER::UI::Widgets::Combobox> font_combo_;
    std::shared_ptr<XER::UI::Widgets::Combobox> weight_combo_;
    std::shared_ptr<XER::UI::Widgets::TextInput> size_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> tracking_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> leading_input_;
    std::shared_ptr<XER::UI::Widgets::ButtonGroup> align_group_;
    std::shared_ptr<XER::UI::Widgets::ButtonGroup> valign_group_;
    std::shared_ptr<XER::UI::Widgets::ButtonGroup> deco_group_;
    std::shared_ptr<XER::UI::Widgets::Toggle> bold_toggle_;
    std::shared_ptr<XER::UI::Widgets::Toggle> italic_toggle_;
    std::shared_ptr<XER::UI::Widgets::Toggle> underline_toggle_;
    std::shared_ptr<XER::UI::Widgets::Toggle> strike_toggle_;
    std::shared_ptr<XER::UI::Widgets::TextInput> x_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> y_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> rotate_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> w_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> h_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> radius_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> baseline_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> fill_hex_input_;
    std::shared_ptr<XER::UI::Widgets::Slider> opacity_slider_;
    std::shared_ptr<XER::UI::Widgets::TextInput> outline_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> outline_hex_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> bg_hex_input_;
    std::shared_ptr<XER::UI::Widgets::Toggle> shadow_toggle_;
    std::shared_ptr<XER::UI::Widgets::TextInput> shadow_x_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> shadow_y_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> shadow_blur_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> shadow_hex_input_;
    std::shared_ptr<XER::UI::Widgets::Button> apply_button_;
    std::shared_ptr<XER::UI::Widgets::Button> cancel_button_;
};

}  // namespace XER::VideoEditor::Ui
