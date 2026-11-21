#include "settings_dialog.h"

#include <algorithm>
#include <cmath>
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

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
using XER::UI::Theme::Color;

constexpr float kPanelWidth = 520.0f;
constexpr float kPanelHeight = 360.0f;
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

// Keeps the fps strip's active-preset highlight in sync: selected is
// Primary, the rest Secondary — same convention as lab_panel's mode
// buttons.
void SyncFpsButtons(XER::UI::Widgets::Widget* parent, int selected) {
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

// Frame-rate presets; the last entry is the "Custom" chip which reveals
// the num/den fields instead of supplying a rate.
struct FpsPreset {
    const char* label;
    int num;
    int den;
};
constexpr FpsPreset kFpsPresets[] = {
    {"23.976", 24000, 1001}, {"24", 24, 1},  {"25", 25, 1},
    {"29.97", 30000, 1001},  {"30", 30, 1},  {"50", 50, 1},
    {"60", 60, 1},           {"Custom", 0, 0},
};
constexpr int kFpsPresetCount =
    static_cast<int>(sizeof(kFpsPresets) / sizeof(kFpsPresets[0]));

// Small canvas-background palette; clicking the swatch cycles it.
constexpr std::array<std::uint8_t, 4> kBgPalette[] = {
    {0, 0, 0, 255},       {38, 38, 38, 255},   {128, 128, 128, 255},
    {255, 255, 255, 255}, {191, 34, 34, 255},  {34, 139, 34, 255},
    {30, 90, 200, 255},   {0, 0, 0, 0},
};
constexpr int kBgPaletteCount =
    static_cast<int>(sizeof(kBgPalette) / sizeof(kBgPalette[0]));

Color BgToColor(const std::array<std::uint8_t, 4>& bg) {
    return {bg[0] / 255.0f, bg[1] / 255.0f, bg[2] / 255.0f,
            bg[3] / 255.0f};
}

std::string BgToHex(const std::array<std::uint8_t, 4>& bg) {
    char buffer[16];
    if (bg[3] == 255) {
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", bg[0],
                      bg[1], bg[2]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", bg[0],
                      bg[1], bg[2], bg[3]);
    }
    return buffer;
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

}  // namespace

SettingsDialog::SettingsDialog(Hooks hooks) : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(false);
}

void SettingsDialog::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;
    // Icon-capable buttons resolve through the pack (idempotent).
    VeIcons::InstallButtonIconResolver();
    title_ = std::make_shared<Widgets::Label>("Project settings");
    name_label_ = std::make_shared<Widgets::Label>(
        project_name_.empty() ? "Untitled project"
                              : "Project: " + project_name_,
        true);
    width_input_ = std::make_shared<Widgets::TextInput>("1920");
    height_input_ = std::make_shared<Widgets::TextInput>("1080");
    fps_num_input_ = std::make_shared<Widgets::TextInput>("30");
    fps_den_input_ = std::make_shared<Widgets::TextInput>("1");
    // Enter inside any numeric field applies the dialog.
    const auto submit = [this] { Apply(); };
    width_input_->SetOnSubmit(submit);
    height_input_->SetOnSubmit(submit);
    fps_num_input_->SetOnSubmit(submit);
    fps_den_input_->SetOnSubmit(submit);

    // Each input sits under its Field caption (lab_panel convention).
    const auto wrap_field =
        [this](const char* caption,
               const std::shared_ptr<Widgets::TextInput>& input) {
            auto field = std::make_shared<Widgets::Field>();
            field->SetLabel(caption);
            field->AddChild(input);
            AddChild(field);
        };
    wrap_field("Width (px)", width_input_);
    wrap_field("Height (px)", height_input_);
    wrap_field("num", fps_num_input_);
    wrap_field("den", fps_den_input_);

    // Frame-rate presets as a segmented strip (the lab_panel mode
    // convention): ButtonGroup joins the buttons edge-to-edge; the
    // selected preset keeps Primary while the rest drop to Secondary.
    auto fps_group = std::make_shared<Widgets::ButtonGroup>(
        Widgets::ButtonGroup::Orientation::Horizontal);
    for (int i = 0; i < kFpsPresetCount; ++i) {
        auto button = std::make_shared<Widgets::Button>(
            kFpsPresets[i].label, Widgets::ButtonStyle::Secondary);
        button->SetOnClick([this, i] { SelectFpsPreset(i); });
        fps_group->AddChild(button);
    }
    AddChild(fps_group);

    ok_button_ = std::make_shared<Widgets::Button>(
        "OK", Widgets::ButtonStyle::Primary);
    ok_button_->SetOnClick([this] { Apply(); });
    cancel_button_ = std::make_shared<Widgets::Button>(
        "Cancel", Widgets::ButtonStyle::Secondary);
    cancel_button_->SetOnClick([this] { Cancel(); });
    AddChild(title_);
    AddChild(name_label_);
    AddChild(ok_button_);
    AddChild(cancel_button_);
    SyncFpsButtons(this, fps_preset_);
}

void SettingsDialog::SetProjectName(const std::string& name) {
    project_name_ = name;
    if (name_label_) {
        name_label_->SetText(name.empty() ? "Untitled project"
                                          : "Project: " + name);
    }
    Invalidate();
}

int SettingsDialog::FpsPresetIndexFor(int fps_num, int fps_den) const {
    for (int i = 0; i < kFpsPresetCount - 1; ++i) {
        if (kFpsPresets[i].num == fps_num && kFpsPresets[i].den == fps_den) {
            return i;
        }
    }
    return kFpsPresetCount - 1;  // Custom
}

bool SettingsDialog::FpsCustomSelected() const {
    return fps_preset_ == kFpsPresetCount - 1;
}

void SettingsDialog::SetSettings(const ProjectSettings& settings) {
    EnsureChildren();
    width_input_->SetValue(IntToString(settings.w));
    height_input_->SetValue(IntToString(settings.h));
    fps_preset_ = FpsPresetIndexFor(settings.fps_num, settings.fps_den);
    fps_num_input_->SetValue(IntToString(settings.fps_num));
    fps_den_input_->SetValue(IntToString(settings.fps_den));
    bg_ = settings.bg;
    const bool custom = FpsCustomSelected();
    if (auto* field = FieldHosting(this, fps_num_input_.get())) {
        field->SetVisible(custom);
    }
    if (auto* field = FieldHosting(this, fps_den_input_.get())) {
        field->SetVisible(custom);
    }
    SyncFpsButtons(this, fps_preset_);
    Invalidate();
}

ProjectSettings SettingsDialog::GetSettings() const {
    ProjectSettings out;
    if (width_input_) {
        out.w = ParseInt(width_input_->GetValue(), out.w, 1, 16384);
    }
    if (height_input_) {
        out.h = ParseInt(height_input_->GetValue(), out.h, 1, 16384);
    }
    if (FpsCustomSelected()) {
        out.fps_num = fps_num_input_
                          ? ParseInt(fps_num_input_->GetValue(), 30, 1, 960)
                          : 30;
        out.fps_den = fps_den_input_
                          ? ParseInt(fps_den_input_->GetValue(), 1, 1, 1001)
                          : 1;
    } else {
        out.fps_num = kFpsPresets[fps_preset_].num;
        out.fps_den = kFpsPresets[fps_preset_].den;
    }
    out.bg = bg_;
    return out;
}

void SettingsDialog::SelectFpsPreset(int index) {
    if (index < 0 || index >= kFpsPresetCount || index == fps_preset_) {
        return;
    }
    fps_preset_ = index;
    const bool custom = FpsCustomSelected();
    if (auto* field = FieldHosting(this, fps_num_input_.get())) {
        field->SetVisible(custom);
    }
    if (auto* field = FieldHosting(this, fps_den_input_.get())) {
        field->SetVisible(custom);
    }
    SyncFpsButtons(this, fps_preset_);
    Invalidate();
}

void SettingsDialog::CycleBackground() {
    // Find the current entry (if any) and step to the next palette slot.
    int index = -1;
    for (int i = 0; i < kBgPaletteCount; ++i) {
        if (kBgPalette[i] == bg_) {
            index = i;
            break;
        }
    }
    bg_ = kBgPalette[(index + 1) % kBgPaletteCount];
    Invalidate();
}

void SettingsDialog::Apply() {
    if (hooks_.on_apply) {
        hooks_.on_apply(GetSettings());
    }
}

void SettingsDialog::Cancel() {
    if (hooks_.on_cancel) {
        hooks_.on_cancel();
    }
}

Layout::Rect SettingsDialog::PanelRect() const {
    const auto bounds = GetBounds();
    const float width = std::min(kPanelWidth, bounds.width - 40.0f);
    return {bounds.x + (bounds.width - width) * 0.5f,
            bounds.y + (bounds.height - kPanelHeight) * 0.5f,
            width, kPanelHeight};
}

Layout::Rect SettingsDialog::BgSwatchRect() const {
    const auto panel = PanelRect();
    return {panel.x + kPanelPad, panel.y + 278.0f, 26.0f, 26.0f};
}

void SettingsDialog::Render(
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

    // Section headers — the drawn-header convention: muted, small,
    // weight 600, drawn at the rects Layout() uses for the rows below.
    const float px = panel.x + kPanelPad;
    const Rendering::TextStyle caption{tokens.muted, tokens.font_small,
                                       600};
    renderer.DrawText("Canvas", {px, panel.y + 66.0f, 200.0f, 16.0f},
                      caption);
    renderer.DrawText("Frame rate",
                      {px, panel.y + 144.0f, 200.0f, 16.0f}, caption);
    renderer.DrawText("Background",
                      {px, panel.y + 258.0f, 200.0f, 16.0f}, caption);

    // Background swatch (click cycles the palette) + hex readout.
    const auto swatch = BgSwatchRect();
    const Color bg = BgToColor(bg_);
    if (bg.a < 1.0f) {
        // Checkerboard hint behind translucent colors.
        renderer.FillRect(swatch, {0.75f, 0.75f, 0.75f, 1.0f},
                          tokens.radius_small);
        renderer.FillRect(
            {swatch.x, swatch.y, swatch.width * 0.5f,
             swatch.height * 0.5f},
            {0.45f, 0.45f, 0.45f, 1.0f}, 0.0f);
        renderer.FillRect(
            {swatch.x + swatch.width * 0.5f,
             swatch.y + swatch.height * 0.5f, swatch.width * 0.5f,
             swatch.height * 0.5f},
            {0.45f, 0.45f, 0.45f, 1.0f}, 0.0f);
    }
    renderer.FillRect(swatch, bg, tokens.radius_small);
    renderer.StrokeRect(swatch, tokens.border, 1.0f, tokens.radius_small);
    renderer.DrawText(
        BgToHex(bg_),
        {swatch.x + swatch.width + 10.0f, swatch.y + 4.0f, 120.0f,
         swatch.height},
        {tokens.muted, tokens.font_body, 500});
}

void SettingsDialog::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const auto panel = PanelRect();
    const float px = panel.x + kPanelPad;
    const float pw = panel.width - kPanelPad * 2.0f;

    title_->Layout({px, panel.y + 14.0f, pw, 26.0f}, tokens);
    name_label_->Layout({px, panel.y + 42.0f, pw, 18.0f}, tokens);

    // Field = caption band (font_small*1.4 + half the small gap) + the
    // 32px control — same math lab_panel uses for its field rows.
    const float caption_h =
        tokens.font_small * 1.4f + tokens.spacing_small * 0.5f;
    const float field_h = caption_h + 32.0f;
    const float field_y = panel.y + 86.0f;
    if (auto* field = FieldHosting(this, width_input_.get())) {
        field->Layout({px, field_y, 110.0f, field_h}, tokens);
    }
    if (auto* field = FieldHosting(this, height_input_.get())) {
        field->Layout({px + 130.0f, field_y, 110.0f, field_h}, tokens);
    }

    // Frame-rate preset strip under the "Frame rate" drawn header.
    if (auto* group =
            FindChild<XER::UI::Widgets::ButtonGroup>(this)) {
        group->Layout({px, panel.y + 164.0f, pw, 28.0f}, tokens);
    }

    // Custom num/den fields (hidden unless the Custom preset is up).
    const float custom_y = panel.y + 198.0f;
    if (auto* field = FieldHosting(this, fps_num_input_.get())) {
        field->Layout({px, custom_y, 100.0f, field_h}, tokens);
    }
    if (auto* field = FieldHosting(this, fps_den_input_.get())) {
        field->Layout({px + 120.0f, custom_y, 100.0f, field_h}, tokens);
    }

    const float button_y = panel.y + panel.height - 48.0f;
    cancel_button_->Layout({panel.x + panel.width - kPanelPad - 196.0f,
                            button_y, 94.0f, 34.0f},
                           tokens);
    ok_button_->Layout({panel.x + panel.width - kPanelPad - 94.0f,
                        button_y, 94.0f, 34.0f},
                       tokens);
}

bool SettingsDialog::HandleEvent(const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;
    if (event.type == Event::Type::KeyDown &&
        event.key == XER::UI::Widgets::KeyCode::Escape) {
        Cancel();
        return true;
    }
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        // The fps strip buttons handle their own clicks now; the
        // background swatch stays a drawn hit region.
        if (BgSwatchRect().Contains(event.position)) {
            CycleBackground();
            return true;
        }
    }
    // Modal: consume pointer events inside and outside the panel so the
    // workspace below stays inert while the dialog is up.
    return event.type == Event::Type::MouseDown ||
           event.type == Event::Type::MouseUp ||
           event.type == Event::Type::Click;
}

void SettingsDialog::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
