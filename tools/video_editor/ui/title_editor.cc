#include "title_editor.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
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
    apply_button_ = std::make_shared<Widgets::Button>(
        "Apply", Widgets::ButtonStyle::Primary);
    apply_button_->SetOnClick([this] { Apply(); });
    cancel_button_ = std::make_shared<Widgets::Button>(
        "Cancel", Widgets::ButtonStyle::Secondary);
    cancel_button_->SetOnClick([this] { Cancel(); });
    AddChild(title_);
    AddChild(text_area_);
    AddChild(size_input_);
    AddChild(outline_input_);
    AddChild(apply_button_);
    AddChild(cancel_button_);
}

void TitleEditor::SetStyle(const TitleStyle& style) {
    EnsureChildren();
    text_area_->SetText(style.text);
    size_input_->SetValue(IntToString(style.size_px));
    outline_input_->SetValue(IntToString(style.outline_px));
    align_ = std::clamp(style.align, 0, kAlignCount - 1);
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

std::vector<Layout::Rect> TitleEditor::AlignChipRects() const {
    std::vector<Layout::Rect> rects;
    const auto panel = PanelRect();
    const float x0 = panel.x + kPanelPad + 196.0f;
    const float gap = 6.0f;
    const float width = 68.0f;
    const float y = panel.y + 150.0f;
    for (int i = 0; i < kAlignCount; ++i) {
        rects.push_back({x0 + i * (width + gap), y, width, 24.0f});
    }
    return rects;
}

Layout::Rect TitleEditor::FillSwatchRect() const {
    const auto panel = PanelRect();
    return {panel.x + kPanelPad, panel.y + 212.0f, 26.0f, 26.0f};
}

Layout::Rect TitleEditor::OutlineSwatchRect() const {
    const auto panel = PanelRect();
    return {panel.x + kPanelPad + 264.0f, panel.y + 212.0f, 26.0f,
            26.0f};
}

void TitleEditor::Render(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, {0.0f, 0.0f, 0.0f, 0.58f}, 0.0f);
    const auto panel = PanelRect();
    renderer.FillRect(panel, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(panel, tokens.border, 1.0f, tokens.radius_large);
    renderer.FillRect({panel.x, panel.y, 4.0f, panel.height},
                      tokens.accent, tokens.radius_large);

    const float px = panel.x + kPanelPad;
    const Rendering::TextStyle caption{tokens.muted, tokens.font_small,
                                       600};
    renderer.DrawText("Text", {px, panel.y + 48.0f, 200.0f, 14.0f},
                      caption);
    renderer.DrawText("Size (px)",
                      {px, panel.y + 134.0f, 150.0f, 14.0f}, caption);
    renderer.DrawText("Align",
                      {px + 196.0f, panel.y + 134.0f, 150.0f, 14.0f},
                      caption);
    renderer.DrawText("Fill",
                      {px, panel.y + 196.0f, 150.0f, 14.0f}, caption);
    renderer.DrawText("Outline (px)",
                      {px + 196.0f, panel.y + 196.0f, 150.0f, 14.0f},
                      caption);

    // Align chips.
    const auto chips = AlignChipRects();
    for (int i = 0; i < kAlignCount; ++i) {
        const bool selected = i == align_;
        renderer.FillRect(chips[i],
                          selected ? tokens.accent : tokens.secondary,
                          tokens.radius_small);
        renderer.DrawText(
            kAlignLabels[i], chips[i],
            {selected ? tokens.accent_foreground : tokens.muted,
             tokens.font_small, 600});
    }

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
        {outline.x + outline.width + 8.0f, outline.y + 4.0f, 96.0f,
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
    text_area_->Layout({px, panel.y + 64.0f, pw, 62.0f}, tokens);
    size_input_->Layout({px, panel.y + 150.0f, 110.0f, 32.0f}, tokens);
    outline_input_->Layout({px + 196.0f, panel.y + 212.0f, 60.0f,
                            28.0f},
                           tokens);

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
        const auto chips = AlignChipRects();
        for (int i = 0; i < kAlignCount; ++i) {
            if (chips[i].Contains(event.position)) {
                SelectAlign(i);
                return true;
            }
        }
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
