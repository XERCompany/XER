// Media Bin panel: persistent SQLite catalog + one-click timeline
// placement. Kept deliberately small — the app shell owns catalog I/O
// and project mutation through TimelineHistory.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "media_catalog.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
class Table;
}

namespace XER::VideoEditor::Ui {

class MediaBinWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        std::function<void()> on_import;
        std::function<void(const std::string&)> on_add_to_timeline;
        std::function<void(const std::string&)> on_select;
        std::function<void(const std::string&, bool)> on_status;
        // AI actions on the media under the cursor (context menu).
        std::function<void(const std::string&)> on_detect_beats;
        std::function<void(const std::string&)> on_transcribe;
        // Cheap availability probe for the transcribe menu item —
        // false greys the item out instead of failing later.
        std::function<bool()> transcribe_available;
        // Audio-plugin bounce: media path + plugin id ("xer.af.echo");
        // the app processes PCM and imports the bounced file.
        std::function<void(const std::string&, const std::string&)>
            on_audio_fx;
        // Lists loaded audio-plugin ids for the context menu.
        std::function<std::vector<std::pair<std::string, std::string>>()>
            audio_fx_list;
    };

    explicit MediaBinWidget(void* parent_hwnd, Hooks hooks = {});

    void SetEntries(std::vector<CatalogEntry> entries);
    void SetPreviewActive(bool active);
    void RefreshRows();
    std::string SelectedPath() const;

    // Entry-level context-menu actions owned by the app (catalog row
    // removal, OS shell reveal). Unset = the menu items stay disabled.
    void SetOnRemoveEntry(std::function<void(const std::string& path)> cb);
    void SetOnRevealEntry(std::function<void(const std::string& path)> cb);

    // Right-click menu: row actions (Add to Timeline) + Import. Called
    // by the shell's tree context-menu handler. Returns true when shown.
    bool ShowContextMenu(XER::UI::Layout::Point position);

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
    // Grid-mode hit test: entry index under a widget-space point, -1
    // when the point is outside the thumbnail grid.
    int ItemAtPoint(const XER::UI::Layout::Point& p) const;
    XER::UI::Layout::Rect GridRect() const;
    XER::UI::Layout::Rect ChipRect(int index) const;
    void DrawGrid(XER::UI::Rendering::Renderer& renderer,
                  const XER::UI::Theme::Tokens& tokens);
    void DrawChips(XER::UI::Rendering::Renderer& renderer,
                   const XER::UI::Theme::Tokens& tokens);

    Hooks hooks_;
    std::function<void(const std::string&)> on_remove_entry_;
    std::function<void(const std::string&)> on_reveal_entry_;
    std::vector<CatalogEntry> entries_;
    int selected_row_ = -1;
    bool children_attached_ = false;
    // CapCut-style poster grid (default) vs. the original table list —
    // toggled by the small view button next to Add.
    bool grid_mode_ = true;
    float grid_scroll_ = 0.0f;
    int grid_hovered_ = -1;
    XER::UI::Layout::Rect grid_rect_{};
    // Kind filter chips above the content ("All | Video | Audio") —
    // `visible_` maps a displayed row/card to its entries_ index.
    int filter_ = 0;
    std::vector<int> visible_;
    int chip_hovered_ = -1;
    void RebuildVisible();

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Button> import_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> add_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> view_btn_;
    std::shared_ptr<XER::UI::Widgets::Table> table_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
};

}  // namespace XER::VideoEditor::Ui
