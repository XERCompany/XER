#include "title_editor.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/button_group.h"
#include "widgets/field.h"
#include "widgets/label.h"
#include "widgets/text_input.h"
#include "widgets/textarea.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
using XER::UI::Theme::Color;

constexpr float kPanelWidth = 460.0f;
constexpr float kPanelHeight = 330.0f;
constexpr float kPanelPad = 22.0f;

// Finds the first direct child of `parent` of type T — the dialog
// reaches its non-member children (ButtonGroup, Field) this way so
// the header's member set stays unchanged.
template <typename T>
T* FindChild(XER::UI::Widgets::Widget* parent) {
    if (parent == nullptr) return nullptr;
    for (const auto& child : parent->GetChildren()) {
        if (auto* typed = dynamic_cast<T*>(child.get())) {
            return typed;
        }
    }
    return nullptr;
}

// The Field whose inner control is `control` (the Field owns the
// widget it captions).
XER::UI::Widgets::Field* FieldHosting(
    XER::UI::Widgets::Widget* parent,
    const XER::UI::Widgets::Widget* control) {
    if (parent == nullptr || control == nullptr) return nullptr;
    for (const auto& child : parent->GetChildren()) {
        auto* field = dynamic_cast<XER::UI::Widgets::Field*>(child.get());
        if (field == nullptr) continue;
        for (const auto& inner : field->GetChildren()) {
            if (inner.get() == control) return field;
        }
    }
    return nullptr;
}

// Keeps the align strip's active-mode highlight in sync — selected is
// Primary, the rest Secondary (the lab_panel mode convention).
void SyncAlignButtons(XER::UI::Widgets::Widget* parent, int selected) {
    namespace Widgets = XER::UI::Widgets;
    auto* group = FindChild<Widgets::ButtonGroup>(parent);
    if (group == nullptr) return;
    const auto& buttons = group->GetChildren();
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        static_cast<Widgets::Button*>(buttons[i].get())
            ->SetStyle(static_cast<int>(i) == selected
                           ? Widgets::ButtonStyle::Primary
                           : Widgets::ButtonStyle::Secondary);
    }
}

constexpr const char* kAlignLabels[] = {"Left", "Center", "Right"};
constexpr int kAlignCount =
    static_cast<int>(sizeof(kAlignLabels) / sizeof(kAlignLabels[0]));

// Clicking a color swatch steps through this palette — a deliberate
// small set of title-friendly colors; alpha is always opaque here
// (transparency comes from clip opacity, not the glyph fill).
constexpr std::array<std::uint8_t, 4> kColorPalette[] = {
    {255, 255, 255, 255},   // white
    {0, 0, 0, 255},         // black
    {160, 160, 160, 255},   // gray
    {191, 34, 34, 255},     // red
    {230, 126, 34, 255},    // orange
    {255, 214, 0, 255},     // yellow
    {34, 139, 34, 255},     // green
    {0, 170, 190, 255},     // cyan
    {30, 90, 200, 255},     // blue
    {148, 90, 220, 255},    // violet
};
constexpr int kColorPaletteCount =
    static_cast<int>(sizeof(kColorPalette) / sizeof(kColorPalette[0]));

Color RgbaToColor(const std::array<std::uint8_t, 4>& rgba) {
    return {rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f,
            rgba[3] / 255.0f};
}

std::string RgbaToHex(const std::array<std::uint8_t, 4>& rgba) {
    char buffer[16];
    if (rgba[3] == 255) {
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", rgba[0],
                      rgba[1], rgba[2]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X",
                      rgba[0], rgba[1], rgba[2], rgba[3]);
    }
    return buffer;
}

// Steps `color` to the next palette entry (wraps; unknown colors start
// from the first entry — same convention as SettingsDialog::CycleBackground).
void CycleColor(std::array<std::uint8_t, 4>& color) {
    int index = -1;
    for (int i = 0; i < kColorPaletteCount; ++i) {
        if (kColorPalette[i] == color) {
            index = i;
            break;
        }
    }
    color = kColorPalette[(index + 1) % kColorPaletteCount];
}

// Lenient integer parse: empty/garbage -> fallback, no exceptions.
int ParseInt(const std::string& text, int fallback, int lo, int hi) {
    if (text.empty()) return fallback;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str()) return fallback;
    return static_cast<int>(
        std::clamp<long>(value, static_cast<long>(lo),
                         static_cast<long>(hi)));
}

std::string IntToString(int value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

void DrawSwatch(Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens,
                const Layout::Rect& rect,
                const std::array<std::uint8_t, 4>& rgba) {
    const Color color = RgbaToColor(rgba);
    if (color.a < 1.0f) {
        // Checkerboard hint behind translucent colors.
        renderer.FillRect(rect, {0.75f, 0.75f, 0.75f, 1.0f},
                          tokens.radius_small);
        renderer.FillRect(
            {rect.x, rect.y, rect.width * 0.5f, rect.height * 0.5f},
            {0.45f, 0.45f, 0.45f, 1.0f}, 0.0f);
        renderer.FillRect(
            {rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f,
             rect.width * 0.5f, rect.height * 0.5f},
            {0.45f, 0.45f, 0.45f, 1.0f}, 0.0f);
    }
    renderer.FillRect(rect, color, tokens.radius_small);
    renderer.StrokeRect(rect, tokens.border, 1.0f, tokens.radius_small);
}

}  // namespace

TitleEditor::TitleEditor(Hooks hooks) : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(false);
}

void TitleEditor::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;
    // Icon-capable buttons resolve through the pack (idempotent).
    VeIcons::InstallButtonIconResolver();
    title_ = std::make_shared<Widgets::Label>("Edit title");
    text_area_ = std::make_shared<Widgets::Textarea>();
    text_area_->SetPlaceholder("Title text\u2026");
    size_input_ = std::make_shared<Widgets::TextInput>("64");
    outline_input_ = std::make_shared<Widgets::TextInput>("0");
    // Enter inside a numeric field applies the dialog (the textarea
    // keeps Enter for newlines).
    const auto submit = [this] { Apply(); };
    size_input_->SetOnSubmit(submit);
    outline_input_->SetOnSubmit(submit);

    // Field captions host each control (lab_panel convention).
    const auto wrap_field =
        [this](const char* caption,
               const std::shared_ptr<XER::UI::Widgets::Widget>&
                   control) {
            auto field = std::make_shared<Widgets::Field>();
            field->SetLabel(caption);
            field->AddChild(control);
            AddChild(field);
        };
    wrap_field("Text", text_area_);
    wrap_field("Size (px)", size_input_);
    wrap_field("Outline (px)", outline_input_);

    // Align as a segmented strip: ButtonGroup joins the three buttons
    // edge-to-edge; the active mode keeps Primary while the rest drop
    // to Secondary.
    auto align_group = std::make_shared<Widgets::ButtonGroup>(
        Widgets::ButtonGroup::Orientation::Horizontal);
    for (int i = 0; i < kAlignCount; ++i) {
        auto button = std::make_shared<Widgets::Button>(
            kAlignLabels[i], Widgets::ButtonStyle::Secondary);
        button->SetOnClick([this, i] { SelectAlign(i); });
        align_group->AddChild(button);
    }
    // Field caption over the strip, same convention as the inputs.
    wrap_field("Align", align_group);

    apply_button_ = std::make_shared<Widgets::Button>(
        "Apply", Widgets::ButtonStyle::Primary);
    apply_button_->SetOnClick([this] { Apply(); });
    cancel_button_ = std::make_shared<Widgets::Button>(
        "Cancel", Widgets::ButtonStyle::Secondary);
    cancel_button_->SetOnClick([this] { Cancel(); });
    AddChild(title_);
    AddChild(apply_button_);
    AddChild(cancel_button_);
    SyncAlignButtons(this, align_);
}

void TitleEditor::SetStyle(const TitleStyle& style) {
    EnsureChildren();
    text_area_->SetText(style.text);
    size_input_->SetValue(IntToString(style.size_px));
    outline_input_->SetValue(IntToString(style.outline_px));
    align_ = std::clamp(style.align, 0, kAlignCount - 1);
    SyncAlignButtons(this, align_);
    color_ = style.color;
    outline_color_ = style.outline_color;
    Invalidate();
}

TitleStyle TitleEditor::GetStyle() const {
    TitleStyle out;
    if (text_area_) out.text = text_area_->GetText();
    out.size_px = size_input_
                      ? ParseInt(size_input_->GetValue(), out.size_px, 1,
                                 1024)
                      : out.size_px;
    out.align = align_;
    out.outline_px =
        outline_input_
            ? ParseInt(outline_input_->GetValue(), out.outline_px, 0,
                       256)
            : out.outline_px;
    out.color = color_;
    out.outline_color = outline_color_;
    return out;
}

void TitleEditor::SelectAlign(int index) {
    if (index < 0 || index >= kAlignCount || index == align_) return;
    align_ = index;
    SyncAlignButtons(this, align_);
    Invalidate();
}

void TitleEditor::CycleFillColor() {
    CycleColor(color_);
    Invalidate();
}

void TitleEditor::CycleOutlineColor() {
    CycleColor(outline_color_);
    Invalidate();
}

void TitleEditor::Apply() {
    if (hooks_.on_apply) {
        hooks_.on_apply(GetStyle());
    }
}

void TitleEditor::Cancel() {
    if (hooks_.on_cancel) hooks_.on_cancel();
}

Layout::Rect TitleEditor::PanelRect() const {
    const auto bounds = GetBounds();
    const float width = std::min(kPanelWidth, bounds.width - 40.0f);
    return {bounds.x + (bounds.width - width) * 0.5f,
            bounds.y + (bounds.height - kPanelHeight) * 0.5f,
            width, kPanelHeight};
}

Layout::Rect TitleEditor::FillSwatchRect() const {
    const auto panel = PanelRect();
    return {panel.x + kPanelPad, panel.y + 214.0f, 26.0f, 26.0f};
}

Layout::Rect TitleEditor::OutlineSwatchRect() const {
    const auto panel = PanelRect();
    return {panel.x + kPanelPad + 306.0f, panel.y + 214.0f, 26.0f,
            26.0f};
}

void TitleEditor::Render(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, VeTheme::kScrim, 0.0f);
    // Card chrome per the panel convention: surface fill + hairline
    // border at the large radius, accent edge bar.
    const auto panel = PanelRect();
    renderer.FillRect(panel, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(panel, tokens.border, 1.0f, tokens.radius_large);
    renderer.FillRect({panel.x, panel.y, 4.0f, panel.height},
                      tokens.accent, tokens.radius_large);

    // The Fill swatch keeps a drawn caption (its swatch is a custom
    // hit region, not a widget); input/strip captions are drawn by
    // their Field wrappers now.
    const float px = panel.x + kPanelPad;
    const Rendering::TextStyle caption{tokens.muted, tokens.font_small,
                                       600};
    renderer.DrawText("Fill",
                      {px, panel.y + 196.0f, 150.0f, 14.0f}, caption);

    // Fill swatch + hex readout.
    const auto fill = FillSwatchRect();
    DrawSwatch(renderer, tokens, fill, color_);
    renderer.DrawText(
        RgbaToHex(color_),
        {fill.x + fill.width + 10.0f, fill.y + 4.0f, 120.0f,
         fill.height},
        {tokens.muted, tokens.font_body, 500});

    // Outline swatch + hex readout (px field is the left half).
    const auto outline = OutlineSwatchRect();
    DrawSwatch(renderer, tokens, outline, outline_color_);
    renderer.DrawText(
        RgbaToHex(outline_color_),
        {outline.x + outline.width + 8.0f, outline.y + 4.0f, 76.0f,
         outline.height},
        {tokens.muted, tokens.font_body, 500});
}

void TitleEditor::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const auto panel = PanelRect();
    const float px = panel.x + kPanelPad;
    const float pw = panel.width - kPanelPad * 2.0f;

    title_->Layout({px, panel.y + 14.0f, pw, 26.0f}, tokens);

    // Field = caption band (font_small*1.4 + half the small gap) +
    // the control height — same math lab_panel uses for its rows.
    const float caption_h =
        tokens.font_small * 1.4f + tokens.spacing_small * 0.5f;
    if (auto* field = FieldHosting(this, text_area_.get())) {
        field->Layout({px, panel.y + 44.0f, pw, caption_h + 62.0f},
                      tokens);
    }
    const float input_h = caption_h + 32.0f;
    if (auto* field = FieldHosting(this, size_input_.get())) {
        field->Layout({px, panel.y + 136.0f, 110.0f, input_h}, tokens);
    }
    // The align strip sits in a Field too — find the Field hosting
    // the ButtonGroup.
    if (auto* field = FieldHosting(
            this, FindChild<XER::UI::Widgets::ButtonGroup>(this))) {
        field->Layout({px + 196.0f, panel.y + 136.0f, pw - 196.0f,
                       input_h},
                      tokens);
    }
    if (auto* field = FieldHosting(this, outline_input_.get())) {
        field->Layout({px + 196.0f, panel.y + 196.0f, 100.0f, input_h},
                      tokens);
    }

    const float button_y = panel.y + panel.height - 48.0f;
    cancel_button_->Layout({panel.x + panel.width - kPanelPad - 196.0f,
                            button_y, 94.0f, 34.0f},
                           tokens);
    apply_button_->Layout({panel.x + panel.width - kPanelPad - 94.0f,
                           button_y, 94.0f, 34.0f},
                          tokens);
}

bool TitleEditor::HandleEvent(const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;
    if (event.type == Event::Type::KeyDown) {
        if (event.key == XER::UI::Widgets::KeyCode::Escape) {
            Cancel();
            return true;
        }
        if (event.key == XER::UI::Widgets::KeyCode::Enter) {
            Apply();
            return true;
        }
    }
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        // The align strip buttons handle their own clicks now; the
        // color swatches stay drawn hit regions.
        if (FillSwatchRect().Contains(event.position)) {
            CycleFillColor();
            return true;
        }
        if (OutlineSwatchRect().Contains(event.position)) {
            CycleOutlineColor();
            return true;
        }
        // Backdrop click outside the card cancels (modal convention).
        if (!PanelRect().Contains(event.position)) {
            Cancel();
            return true;
        }
    }
    // Modal: consume pointer events inside and outside the panel so the
    // workspace below stays inert while the dialog is up.
    return event.type == Event::Type::MouseDown ||
           event.type == Event::Type::MouseUp ||
           event.type == Event::Type::Click;
}

void TitleEditor::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
