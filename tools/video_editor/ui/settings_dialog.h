// Modal project-settings overlay for the XER video editor (Phase 2).
// Styled like ExportDialog: dimmed backdrop, centered card, OK/Cancel.
// The dialog owns no project state — the shell pushes the current
// values via SetSettings()/SetProjectName() before showing it and reads
// the edited result back from the on_apply hook (or GetSettings()).
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
class Checkbox;
class Label;
class TextInput;
}

namespace XER::VideoEditor::Ui {

// Editable project settings surfaced by SettingsDialog.
struct ProjectSettings {
    int w = 1920;                       // canvas width, px
    int h = 1080;                       // canvas height, px
    int fps_num = 30;                   // frame rate numerator
    int fps_den = 1;                    // frame rate denominator
    std::array<std::uint8_t, 4> bg{0, 0, 0, 255};  // RGBA canvas clear
    // Proxy workflow (Project::use_proxies / proxy_ingest /
    // proxy_max_dim / proxy_crf — see timeline_model.h).
    bool use_proxies = true;            // decode video via ready proxies
    bool proxy_ingest = false;          // auto-build proxies on import
    int proxy_max_dim = 960;            // long-edge cap for new proxies
    int proxy_crf = 28;                 // h264 constant-quality target
};

class SettingsDialog final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Fired by OK with the validated settings. The host applies
        // them to the project and hides the overlay.
        std::function<void(const ProjectSettings&)> on_apply;
        // Fired by Cancel / Escape / backdrop click. Optional — the
        // host usually just hides the overlay.
        std::function<void()> on_cancel;
    };

    explicit SettingsDialog(Hooks hooks = {});

    // Display-only project name shown under the title.
    void SetProjectName(const std::string& name);
    // Fills every field from `settings` — call before showing.
    void SetSettings(const ProjectSettings& settings);
    // Reads the current field values (clamped/validated; never throws).
    ProjectSettings GetSettings() const;

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
    // Index into the fps preset table; kFpsPresetCount-1 is "Custom".
    int FpsPresetIndexFor(int fps_num, int fps_den) const;
    bool FpsCustomSelected() const;
    void SelectFpsPreset(int index);
    // Advances bg_ through the small preset palette on swatch click.
    void CycleBackground();

    XER::UI::Layout::Rect PanelRect() const;
    XER::UI::Layout::Rect BgSwatchRect() const;

    Hooks hooks_;
    bool children_attached_ = false;
    std::string project_name_;
    int fps_preset_ = 0;   // selected chip; last index = Custom
    std::array<std::uint8_t, 4> bg_{0, 0, 0, 255};

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Label> name_label_;
    std::shared_ptr<XER::UI::Widgets::TextInput> width_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> height_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> fps_num_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> fps_den_input_;
    std::shared_ptr<XER::UI::Widgets::Checkbox> proxies_check_;
    std::shared_ptr<XER::UI::Widgets::Checkbox> ingest_check_;
    std::shared_ptr<XER::UI::Widgets::TextInput> proxy_dim_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> proxy_crf_input_;
    std::shared_ptr<XER::UI::Widgets::Button> ok_button_;
    std::shared_ptr<XER::UI::Widgets::Button> cancel_button_;
};

}  // namespace XER::VideoEditor::Ui
