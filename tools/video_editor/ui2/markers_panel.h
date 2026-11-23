// MarkersPanel — the M3 wave-2 markers dock (spec §6 "Mark" tab):
// a virtualized list of timeline markers rebuilt on the shared
// widget kit (VirtualList + TextInput + Select + ContextMenu).
//
//   Search…                       [color ▾] [+ Add @ playhead]
//   ┌──────────────────────────────────────────────────┐
//   │ 00:00:05:12 ● Mark   Intro                       │
//   │ 00:00:18:03 ● Clip   bad take — needs review     │
//   └──────────────────────────────────────────────────┘
//   2 markers
//
// Row contract (spec: "timecode · color dot · name · duration ·
// comment"): every row carries the model Marker PLUS a presentation
// extension (duration/comment/scope). The timeline model's Marker
// only stores time + name + color — duration, comment and the
// clip-scope label are PANEL-level annotation data the shell may
// push once the model grows those fields (the edit dialog writes
// them into the panel's session annotation table today; that seam
// is documented in markers_panel.cc, not faked as model state).
//
// Ownership rules (surface_hooks.h §2): the panel holds a
// presentation copy pushed via SetMarkers; every mutation leaves
// through Hooks — add/remove/rename/recolor all reach
// TimelineHistory on the shell side (recolor + rename land as one
// history macro each).
#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "timeline_model.h"   // Marker
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class ContextMenu;
class Dialog;
class Label;
class Select;
class TextInput;
class VirtualList;
}

namespace XER::VideoEditor::Ui2 {

// One row's display data. `marker` is the model truth; the extra
// fields are presentation data the shell fills when available —
// `scope` distinguishes "Project" rows from clip-anchored markers
// (the model has no clip markers yet; the column exists for the
// feed, not as fake data).
struct MarkerRow {
    Marker marker;
    double duration_seconds = 0.0;   // 0 = point marker
    std::string comment;
    std::string scope = "Project";   // "Project" or a clip label
};

class MarkersPanel final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<MarkersPanel> Create();

    // Every document action leaves through here — all optional
    // unless noted. The shell implements them against TimelineHistory.
    struct Hooks {
        // Row double-click / context "Go to" — TIMELINE seconds
        // (VeApp::Seek on the shell side).
        std::function<void(double)> on_seek;
        // "+ Add @ playhead": full Marker so the toolbar color
        // Select travels with the request (history_->AddMarker).
        std::function<void(const Marker&)> on_add_marker;
        // Context "Delete".
        std::function<void(double)> on_remove_marker;
        // Atomic replace (rename / recolor / retime): the shell
        // removes `old_time` and adds `updated` inside one history
        // macro so undo restores both halves.
        std::function<void(double old_time, const Marker& updated)>
            on_update_marker;
        // Current playhead for the add button (shell-owned).
        std::function<double()> playhead_seconds;
        // HH:MM:SS:FF at project fps for the row's timecode column
        // (falls back to a fixed 30 fps formatter when unset).
        std::function<std::string(double)> format_timecode;
        std::function<void(const std::string&, bool)> on_status;
    };
    void SetHooks(Hooks hooks);
    const Hooks& GetHooks() const { return hooks_; }

    // ---- data (shell pushes marker snapshots) ------------------
    void SetMarkers(std::vector<MarkerRow> rows);
    // Convenience for the common "project markers only" feed.
    void SetMarkers(std::vector<Marker> markers);
    int MarkerCount() const { return static_cast<int>(rows_.size()); }
    const MarkerRow* RowAt(int visible_index) const;
    // Display order after search filtering (source times — the
    // marker identity the model keys on). The smoke asserts on this.
    std::vector<double> VisibleTimes() const;
    int VisibleCount() const { return static_cast<int>(visible_.size()); }

    // ---- view state ---------------------------------------------
    void SetPlayhead(double seconds) { playhead_ = seconds; }
    double Playhead() const { return playhead_; }
    void SetSearchText(std::string text);
    const std::string& SearchText() const { return search_; }
    // The toolbar color choice applied to newly-added markers.
    int ColorChoice() const { return color_choice_; }
    void SetColorChoice(int index);
    // Session annotation table (duration/comment — see the header
    // comment). Keyed by marker time so it survives re-feeds.
    bool AnnotationFor(double time, double* duration,
                       std::string* comment) const;

    // ---- verbs (buttons / context menu / smoke drive these) -----
    void AddAtPlayhead();
    void SeekToMarker(int visible_index);      // single path for dbl-click
    void DeleteMarkerAt(int visible_index);
    // Rename without opening the dialog (context "Rename…" uses the
    // dialog; the smoke drives this directly).
    void RenameMarkerAt(int visible_index, const std::string& name);
    void SetMarkerColorAt(int visible_index,
                          const std::array<std::uint8_t, 4>& color);
    // Annotation write path (session data — survives SetMarkers).
    void SetMarkerAnnotation(int visible_index, double duration,
                             const std::string& comment);

    // Edit dialog + context menu — the shell's tree-level context
    // handler forwards right-clicks here.
    void OpenEditDialog(int visible_index);
    bool ShowContextMenuAt(XER::UI::Layout::Point point);
    int ListIndexAt(XER::UI::Layout::Point point) const;

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {220.0f, 160.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {340.0f, 320.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit MarkersPanel();

    void EnsureChildren();
    void RebuildVisible();
    void SyncStatusText();
    void CommitEditDialog();
    std::string TimecodeFor(double seconds) const;

    std::vector<MarkerRow> rows_;
    std::vector<int> visible_;          // display order -> rows_
    std::string search_;
    double playhead_ = 0.0;
    int color_choice_ = 0;              // palette index for new markers

    // Session annotation store keyed by marker.time (the model
    // carries no duration/comment — see the header comment).
    struct Annotation {
        double duration = 0.0;
        std::string comment;
    };
    std::map<double, Annotation> annotations_;

    // Double-click synthesis (VirtualList reports clicks; the
    // Click event enum has no double-click variant — two clicks on
    // the same row inside 450 ms = seek).
    int last_click_row_ = -1;
    std::chrono::steady_clock::time_point last_click_time_{};

    std::shared_ptr<XER::UI::Widgets::TextInput> search_input_;
    std::shared_ptr<XER::UI::Widgets::Select> color_select_;
    std::shared_ptr<XER::UI::Widgets::Button> add_btn_;
    std::shared_ptr<XER::UI::Widgets::VirtualList> list_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
    // Edit dialog state — kept so CommitEditDialog can find the row.
    std::shared_ptr<XER::UI::Widgets::Dialog> edit_dialog_;
    std::shared_ptr<XER::UI::Widgets::TextInput> edit_name_;
    std::shared_ptr<XER::UI::Widgets::TextInput> edit_comment_;
    std::shared_ptr<XER::UI::Widgets::TextInput> edit_duration_;
    std::shared_ptr<XER::UI::Widgets::Select> edit_color_;
    int edit_row_ = -1;

    bool children_attached_ = false;
    Hooks hooks_;
};

}  // namespace XER::VideoEditor::Ui2
