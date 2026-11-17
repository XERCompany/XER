// Plugins panel: catalog of every effect registered by the runtime
// plugin host (tools/video_editor/plugin_host.h —
// SharedPluginHost().Effects()). Rows are grouped by kind —
// "Video Effects" first, then "Audio Effects" — and each row shows
// the effect name plus a small vendor/file tag.
//
// Click selects a row; the "Add to Clip" button (Enter or a
// double-click does the same) emits Hooks::on_add_effect with the
// ClipEffect type string "plugin:<id>" — the app wraps the mutation
// in TimelineHistory and applies it to the current selection.
// Reload() re-reads the host list after a plugin rescan.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "plugin_host.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
}

namespace XER::VideoEditor::Ui {

class PluginsPanelWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Fired by "Add to Clip" with the plugin effect type string
        // "plugin:<id>" — the app adds a ClipEffect to the selection.
        std::function<void(const std::string& type)> on_add_effect;
    };

    explicit PluginsPanelWidget(Hooks hooks = {});

    // Re-reads SharedPluginHost().Effects() and rebuilds the list —
    // call after the host has (re)scanned <exe>/plugins.
    void Reload();

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
    // One line in the scrollable list — either a section header or a
    // selectable plugin row.
    struct Entry {
        bool header = false;
        std::string text;   // header label or effect name
        std::string tag;    // "vendor · file" (rows only)
        std::string id;     // plugin id (rows only)
    };

    void EnsureChildren();
    void UpdateAddEnabled();
    float EntryHeight(int index) const;
    float EntryTop(int index) const;   // content-space y of the line
    int RowAt(const XER::UI::Layout::Point& position) const;
    void AddSelected();
    void SetStatus(const std::string& text);

    Hooks hooks_;
    std::vector<Entry> entries_;    // headers interleaved with rows
    int selected_ = -1;             // index into entries_ (rows only)
    int hovered_ = -1;
    float scroll_y_ = 0.0f;
    float content_height_ = 0.0f;
    float rows_top_ = 0.0f;         // y where the list begins (layout)
    float rows_bottom_ = 0.0f;      // y where the list viewport ends
    bool children_attached_ = false;
    // Double-click detection: second click on the same row adds it.
    int last_click_row_ = -1;
    std::chrono::steady_clock::time_point last_click_time_{};

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Button> add_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
};

}  // namespace XER::VideoEditor::Ui
