#include "settings_dialog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
using XER::UI::Theme::Color;

constexpr float kPanelWidth = 520.0f;
constexpr float kPanelHeight = 340.0f;
constexpr float kPanelPad = 22.0f;

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
    title_ = std::make_shared<Widgets::Label>("Project settings");
    name_label_ = std::make_shared<Widgets::Label>(
        project_name_.empty() ? "Untitled project"
                              : "Project: " + project_name_,
        true);
    width_label_ = std::make_shared<Widgets::Label>("W", true);
    width_input_ = std::make_shared<Widgets::TextInput>("1920");
    height_label_ = std::make_shared<Widgets::Label>("H", true);
    height_input_ = std::make_shared<Widgets::TextInput>("1080");
    fps_num_label_ = std::make_shared<Widgets::Label>("num", true);
    fps_num_input_ = std::make_shared<Widgets::TextInput>("30");
    fps_den_label_ = std::make_shared<Widgets::Label>("den", true);
    fps_den_input_ = std::make_shared<Widgets::TextInput>("1");
    // Enter inside any numeric field applies the dialog.
    const auto submit = [this] { Apply(); };
    width_input_->SetOnSubmit(submit);
    height_input_->SetOnSubmit(submit);
    fps_num_input_->SetOnSubmit(submit);
    fps_den_input_->SetOnSubmit(submit);
    ok_button_ = std::make_shared<Widgets::Button>(
        "OK", Widgets::ButtonStyle::Primary);
    ok_button_->SetOnClick([this] { Apply(); });
    cancel_button_ = std::make_shared<Widgets::Button>(
        "Cancel", Widgets::ButtonStyle::Secondary);
    cancel_button_->SetOnClick([this] { Cancel(); });
    AddChild(title_);
    AddChild(name_label_);
    AddChild(width_label_);
    AddChild(width_input_);
    AddChild(height_label_);
    AddChild(height_input_);
    AddChild(fps_num_label_);
    AddChild(fps_num_input_);
    AddChild(fps_den_label_);
    AddChild(fps_den_input_);
    AddChild(ok_button_);
    AddChild(cancel_button_);
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
    fps_num_label_->SetVisible(custom);
    fps_num_input_->SetVisible(custom);
    fps_den_label_->SetVisible(custom);
    fps_den_input_->SetVisible(custom);
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
    if (fps_num_label_) {
        fps_num_label_->SetVisible(custom);
        fps_num_input_->SetVisible(custom);
        fps_den_label_->SetVisible(custom);
        fps_den_input_->SetVisible(custom);
    }
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

std::vector<Layout::Rect> SettingsDialog::FpsChipRects() const {
    std::vector<Layout::Rect> rects;
    const auto panel = PanelRect();
    const float px = panel.x + kPanelPad;
    const float pw = panel.width - kPanelPad * 2.0f;
    const float gap = 5.0f;
    const float chip_w =
        (pw - gap * (kFpsPresetCount - 1)) / kFpsPresetCount;
    const float y = panel.y + 146.0f;
    for (int i = 0; i < kFpsPresetCount; ++i) {
        rects.push_back(
            {px + i * (chip_w + gap), y, chip_w, 24.0f});
    }
    return rects;
}

Layout::Rect SettingsDialog::BgSwatchRect() const {
    const auto panel = PanelRect();
    return {panel.x + kPanelPad, panel.y + 236.0f, 26.0f, 26.0f};
}

void SettingsDialog::Render(
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
    renderer.DrawText("Canvas", {px, panel.y + 68.0f, 200.0f, 16.0f},
                      caption);
    renderer.DrawText("Frame rate",
                      {px, panel.y + 126.0f, 200.0f, 16.0f}, caption);
    renderer.DrawText("Background",
                      {px, panel.y + 216.0f, 200.0f, 16.0f}, caption);

    // Frame-rate preset chips.
    const auto chips = FpsChipRects();
    for (int i = 0; i < kFpsPresetCount; ++i) {
        const bool selected = i == fps_preset_;
        renderer.FillRect(chips[i],
                          selected ? tokens.accent : tokens.secondary,
                          tokens.radius_small);
        renderer.DrawText(
            kFpsPresets[i].label, chips[i],
            {selected ? tokens.accent_foreground : tokens.muted,
             tokens.font_small, 600});
    }

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

    const float field_y = panel.y + 88.0f;
    width_label_->Layout({px, field_y + 6.0f, 16.0f, 20.0f}, tokens);
    width_input_->Layout({px + 20.0f, field_y, 96.0f, 32.0f}, tokens);
    height_label_->Layout({px + 132.0f, field_y + 6.0f, 16.0f, 20.0f},
                          tokens);
    height_input_->Layout({px + 152.0f, field_y, 96.0f, 32.0f}, tokens);

    const float custom_y = panel.y + 178.0f;
    fps_num_label_->Layout({px, custom_y + 5.0f, 30.0f, 18.0f}, tokens);
    fps_num_input_->Layout({px + 34.0f, custom_y, 72.0f, 30.0f}, tokens);
    fps_den_label_->Layout({px + 118.0f, custom_y + 5.0f, 30.0f, 18.0f},
                           tokens);
    fps_den_input_->Layout({px + 152.0f, custom_y, 72.0f, 30.0f},
                           tokens);

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
        const auto chips = FpsChipRects();
        for (int i = 0; i < kFpsPresetCount; ++i) {
            if (chips[i].Contains(event.position)) {
                SelectFpsPreset(i);
                return true;
            }
        }
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
