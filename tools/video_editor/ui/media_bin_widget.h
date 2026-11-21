// Media Bin panel: persistent SQLite catalog + one-click timeline
// placement. Kept deliberately small — the app shell owns catalog I/O
// and project mutation through TimelineHistory.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "media_catalog.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class ButtonGroup;
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
        // Double-click / context-menu "Open in Source Monitor" — the
        // app loads the media file into the source monitor surface.
        std::function<void(const std::string&)> on_open_source;
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
        // Bin->timeline drag (grid mode): a card's left-press drag
        // that releases OUTSIDE the bin reports the entry path + the
        // absolute-space release point — the shell hit-tests its own
        // drop targets. on_drag_hover tracks the cursor while a drag
        // is active (active=false once, at the end) so the shell can
        // light a drop-target affordance; both are absolute coords.
        std::function<void(const std::string&,
                           XER::UI::Layout::Point)>
            on_drop;
        std::function<void(XER::UI::Layout::Point, bool)>
            on_drag_hover;
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
    void DrawGrid(XER::UI::Rendering::Renderer& renderer,
                  const XER::UI::Theme::Tokens& tokens);
    // Kind filter strip (All/Video/Audio): SetFilter is the buttons'
    // OnClick target; SyncFilterButtons mirrors filter_ onto the
    // Primary/Secondary styles (lab_panel mode-strip convention).
    void SetFilter(int filter);
    void SyncFilterButtons();

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
    // Kind filter strip above the content ("All | Video | Audio") —
    // `visible_` maps a displayed row/card to its entries_ index.
    int filter_ = 0;
    std::vector<int> visible_;
    void RebuildVisible();
    // Double-click detection (there's no DoubleClick event type — two
    // Click events on the same entry within the threshold count).
    // Works for both the poster grid and the table's row-click hook.
    void MaybeOpenSource(int entry_index);
    int last_click_entry_ = -1;
    std::chrono::steady_clock::time_point last_click_at_{};
    // Grid card drag-out (bin->timeline drop): armed on a card's left
    // MouseDown, promoted once the cursor travels past the threshold —
    // the tree's left-button mouse capture keeps moves/releases routed
    // here even outside the bounds. drag_entry_ indexes entries_.
    int drag_entry_ = -1;
    bool drag_active_ = false;
    XER::UI::Layout::Point drag_press_{};

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Button> import_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> add_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> view_btn_;
    std::shared_ptr<XER::UI::Widgets::ButtonGroup> filter_group_;
    std::shared_ptr<XER::UI::Widgets::Button> filter_all_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> filter_video_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> filter_audio_btn_;
    std::shared_ptr<XER::UI::Widgets::Table> table_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
};

}  // namespace XER::VideoEditor::Ui
