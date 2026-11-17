// Media Bin panel: persistent SQLite catalog + one-click timeline
// placement. Kept deliberately small — the app shell owns catalog I/O
// and project mutation through TimelineHistory.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "media_catalog.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
class Table;
class VideoPreviewWidget;
}

namespace XER::VideoEditor::Ui {

class MediaBinWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        std::function<void()> on_import;
        std::function<void(const std::string&)> on_add_to_timeline;
        std::function<void(const std::string&)> on_select;
        std::function<void(const std::string&, bool)> on_status;
    };

    explicit MediaBinWidget(void* parent_hwnd, Hooks hooks = {});

    void SetEntries(std::vector<CatalogEntry> entries);
    void SetPreviewActive(bool active);
    void RefreshRows();
    std::string SelectedPath() const;

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {220.0f, 240.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {300.0f, 480.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    void EnsureChildren();
    void SetStatus(const std::string& text, bool is_error = false);

    Hooks hooks_;
    void* parent_hwnd_ = nullptr;
    std::vector<CatalogEntry> entries_;
    int selected_row_ = -1;
    bool children_attached_ = false;

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Button> import_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> add_btn_;
    std::shared_ptr<XER::UI::Widgets::Table> table_;
    std::shared_ptr<XER::UI::Widgets::VideoPreviewWidget> source_preview_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
};

}  // namespace XER::VideoEditor::Ui
