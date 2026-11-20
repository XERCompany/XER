// Markers panel: table of project timeline markers.
//
//   [swatch] HH:MM:SS.mmm   name            [Go] [x]
//
// Single-click a row seeks the playhead to the marker; [Go] does the
// same; [x] removes it; double-click a row opens an inline rename
// TextInput (Enter commits through on_rename_marker — the model has no
// dedicated rename op, so the app implements it as RemoveMarker +
// AddMarker under one history macro). "Add @ playhead" emits
// on_add_marker(playhead, "Marker").
//
// The panel mirrors Project::markers (already sorted by time); call
// SetProject again after any marker mutation to refresh.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
class TextInput;
}

namespace XER::VideoEditor::Ui {

class MarkersPanelWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        std::function<void(double)> on_seek;
        std::function<void(double, const std::string&)> on_add_marker;
        std::function<void(double)> on_remove_marker;
        // No model rename op — the app composes remove+add atomically.
        std::function<void(double, const std::string&)> on_rename_marker;
        std::function<void(const std::string&, bool)> on_status;
    };

    explicit MarkersPanelWidget(Hooks hooks = {});

    // `project` may be nullptr (clears the list). The marker list is
    // copied — call again after edits to refresh.
    void SetProject(const Project* project);
    void SetPlayhead(double timeline_seconds);
    double Playhead() const { return playhead_; }

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
    struct Row {
        std::shared_ptr<XER::UI::Widgets::Button> jump;
        std::shared_ptr<XER::UI::Widgets::Button> remove;
    };

    void EnsureChildren();
    void RebuildRows();
    int RowAt(const XER::UI::Layout::Point& position) const;
    XER::UI::Layout::Rect RowRect(int row) const;
    XER::UI::Layout::Rect NameRect(int row) const;
    void BeginRename(int row);
    void CommitRename();
    void CancelRename();
    void SetStatus(const std::string& text, bool is_error = false);

    Hooks hooks_;
    std::vector<Marker> markers_;   // copy of Project::markers
    double playhead_ = 0.0;
    int hovered_ = -1;
    int rename_row_ = -1;           // row being renamed (-1 = none)
    float scroll_y_ = 0.0f;
    float rows_top_ = 0.0f;         // layout: y where the list begins
    float row_height_ = 30.0f;
    bool children_attached_ = false;
    // Double-click detection (two Clicks on the same row < window).
    int last_click_row_ = -1;
    std::chrono::steady_clock::time_point last_click_time_{};

    std::vector<Row> rows_;
    // Mark-and-sweep graveyard: RebuildRows can fire from inside a row
    // button's own OnClick (remove -> hook -> SetProject -> rebuild),
    // which would destroy the very widget mid-dispatch. Retired
    // buttons are disabled + unparented but kept alive here until the
    // NEXT rebuild, so a firing button can never be freed under its
    // own event regardless of how dispatch holds refs.
    std::vector<std::shared_ptr<XER::UI::Widgets::Widget>> retired_rows_;
    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Button> add_btn_;
    std::shared_ptr<XER::UI::Widgets::TextInput> rename_input_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
};

}  // namespace XER::VideoEditor::Ui
