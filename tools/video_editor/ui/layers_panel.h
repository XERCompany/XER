// Layers panel: lightweight clip-list view for the video editor's
// right dock — the "LAYERS" counterpart to the visual timeline.
// Lists the clips on the ACTIVE track (the selected clip's track,
// else the first video track — the shell picks via SetActiveTrack)
// sorted by timeline start:
//
//   [kind chip] clip name                     HH:MM:SS.mmm
//
// Rows are disabled Widgets::Item children (title = clip label,
// description = kind + duration) so clicks land on the panel — a
// single click selects the clip (on_select_clip); a [Go] button
// seeks the playhead to the clip's start. The selected row gets the
// accent edge tick, mirroring MarkersPanelWidget's playhead marker.
//
// The panel mirrors Project::clips; call SetProject again after any
// clip mutation to refresh.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Item;
class Label;
}

namespace XER::VideoEditor::Ui {

class LayersPanelWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        std::function<void(const std::string&)> on_select_clip;
        std::function<void(double)> on_seek;
        std::function<void(const std::string&, bool)> on_status;
    };

    explicit LayersPanelWidget(Hooks hooks = {});

    // `project` may be nullptr (clears the list). Clip data is copied
    // — call again after edits to refresh.
    void SetProject(const Project* project);
    // Track the rows list (-1 clears). The shell derives it from the
    // selection: the selected clip's track, else the first video
    // track.
    void SetActiveTrack(int track_index);
    void SetSelectedClip(const std::string& clip_id);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {220.0f, 160.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {300.0f, 240.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    // Copied display state for one clip row (kept free of pointers
    // into Project so a rebuild can never dangle).
    struct LayerEntry {
        std::string clip_id;
        std::string label;
        std::string kind;       // MEDIA / MATTE / BARS / TEXT / ADJ
        double start = 0.0;
        double duration = 0.0;
    };
    struct Row {
        std::shared_ptr<XER::UI::Widgets::Item> item;
        std::shared_ptr<XER::UI::Widgets::Button> jump;
    };

    void EnsureChildren();
    // Re-derives entries_ from the last SetProject pointer and the
    // active track — called by both setters so a track switch alone
    // (clip selected on a different lane) refreshes the rows.
    void RefreshEntries();
    void RebuildRows();
    int RowAt(const XER::UI::Layout::Point& position) const;
    XER::UI::Layout::Rect RowRect(int row) const;
    float RowsViewportBottom() const;
    void SetStatus(const std::string& text, bool is_error = false);

    Hooks hooks_;
    const Project* project_ = nullptr;   // borrowed — app-owned
    std::vector<LayerEntry> entries_;   // active track, by start
    std::string track_name_;
    std::string selected_clip_id_;
    int active_track_ = -1;
    int hovered_ = -1;
    float scroll_y_ = 0.0f;
    float rows_top_ = 0.0f;
    float row_height_ = 40.0f;
    bool children_attached_ = false;

    std::vector<Row> rows_;
    // Mark-and-sweep graveyard (same pattern as MarkersPanelWidget):
    // a hook fired from inside a row button's OnClick can rebuild the
    // rows — retired children live one extra rebuild so a firing
    // widget is never freed mid-dispatch.
    std::vector<std::shared_ptr<XER::UI::Widgets::Widget>> retired_rows_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
};

}  // namespace XER::VideoEditor::Ui
