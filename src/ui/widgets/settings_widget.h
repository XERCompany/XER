#ifndef XER_UI_WIDGETS_SETTINGS_WIDGET_H
#define XER_UI_WIDGETS_SETTINGS_WIDGET_H

#include <future>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "theme/theme.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Label;
class Select;
class Switch;

// Application settings panel: appearance, graph autosave, audio cache
// maintenance and a keyboard-shortcut reference. Designed to live inside a
// ScrollArea — reports its full content height via GetContentHeight().
class SettingsWidget final : public Widget {
public:
    struct Hooks {
        std::function<void(Theme::Variant)> on_theme_changed;
        std::function<bool()> autosave_get;
        std::function<void(bool)> autosave_set;
        std::function<void()> on_new_graph;
        std::function<void()> on_save_graph;
        std::function<void()> on_load_graph;
        std::function<std::string()> graph_file_get;
    };

    SettingsWidget(void* parent_hwnd, Hooks hooks = {});
    ~SettingsWidget() override;

    // Recomputes dynamic values (cache size, graph file path). Called by the
    // owner when the settings view becomes visible.
    void RefreshDynamic();

    float GetContentHeight() const override { return content_height_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override { (void)event; return false; }

    Layout::Size GetMinSize() const override { return {300.0f, 160.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {560.0f, content_height_ > 0.0f ? content_height_ : 640.0f};
    }
    Layout::Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }

private:
    struct Entry {
        std::shared_ptr<Widget> label;    // left column (may be null)
        std::shared_ptr<Widget> desc;     // optional muted sub-line under label
        std::vector<std::shared_ptr<Widget>> controls;  // right column
        float height = 0.0f;
        float top = 0.0f, bottom = 0.0f;  // filled during Layout
    };

    // One card on the page: title + optional description + a run of rows.
    struct Section {
        std::string title;
        std::string desc;
        std::size_t first = 0;   // entry range [first, last)
        std::size_t last = 0;
        // Geometry filled during Layout (DIP, relative to widget bounds).
        Layout::Rect card{};
        Layout::Rect title_rect{};
        Layout::Rect desc_rect{};
        float inner_x = 0.0f;
        float inner_w = 0.0f;
    };

    void Build();
    void BeginSection(const std::string& title, const std::string& desc = {});
    void AddRow(std::shared_ptr<Widget> label,
                std::vector<std::shared_ptr<Widget>> controls,
                float height);
    void AddDescribedRow(const std::string& title, const std::string& desc,
                         std::vector<std::shared_ptr<Widget>> controls,
                         float height);
    void AddInfo(const std::string& text, bool muted = true);
    void Persist(const std::string& key, const std::string& value);
    std::string LoadSetting(const std::string& key) const;

    void RefreshCacheInfo();
    void RefreshSystemInfo();
    void ApplySystemSnapshot();
    void OpenCacheFolder();
    void ClearCache();

    void* parent_hwnd_ = nullptr;
    Hooks hooks_;

    // Hardware snapshot plumbing (async_io/hardware_abstraction).
    struct SysProvider;
    std::unique_ptr<SysProvider> sys_provider_;

    bool built_ = false;
    bool children_attached_ = false;
    std::vector<Entry> entries_;
    std::vector<Section> sections_;
    float content_height_ = 0.0f;
    Theme::Tokens last_tokens_{};

    // Dynamic labels refreshed by RefreshDynamic().
    Label* cache_path_label_ = nullptr;
    Label* cache_size_label_ = nullptr;
    Label* cache_status_label_ = nullptr;
    Label* graph_file_label_ = nullptr;
    // System section labels (fixed order, filled by ApplySystemSnapshot).
    std::vector<Label*> sys_labels_;
    bool sys_pending_ = false;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_SETTINGS_WIDGET_H
