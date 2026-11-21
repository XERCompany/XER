// Media Bin panel: persistent SQLite catalog + one-click timeline
// placement. Kept deliberately small — the app shell owns catalog I/O
// and project mutation through TimelineHistory.
#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "media_catalog.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class ButtonGroup;
class Label;
class Select;
class Table;
class TextInput;
}

namespace XER::VideoEditor::Ui {

class MediaBinWidget final : public XER::UI::Widgets::Widget {
public:
    // Tri-state answer of the optional peaks hook — the same contract
    // as the timeline's MediaLookup (media_prep lookup-or-enqueue):
    // kPending means a worker is decoding and the tile keeps its
    // placeholder; kFailed is a permanent miss the widget
    // negative-caches. Values intentionally mirror MediaPrep::Lookup's
    // order so the app can static_cast straight through.
    enum class PeaksLookup { kReady, kPending, kFailed };

    struct Hooks {
        std::function<void()> on_import;
        // Context-menu "Import from URL…" — the shell owns the URL
        // dialog + download/import flow.
        std::function<void()> on_import_url;
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
        // Optional non-blocking audio-peaks lookup feeding the grid
        // tiles' mini-waveform strip — the shell serves media_prep's
        // warm cache (same lookup-or-enqueue contract as the
        // timeline's get_audio_peaks). Unset -> audio tiles keep the
        // drawn-bars placeholder forever.
        std::function<PeaksLookup(const std::string&, AudioPeaks*)>
            get_audio_peaks;
    };

    explicit MediaBinWidget(void* parent_hwnd, Hooks hooks = {});

    void SetEntries(std::vector<CatalogEntry> entries);
    void SetPreviewActive(bool active);
    void RefreshRows();
    std::string SelectedPath() const;
    // Media pipeline published fresh data (the app's media-prep
    // ConsumeRenderDirty drain): cached peaks MISSES are dropped so
    // the next paint re-probes — a waveform that was a miss before
    // the decode landed shows up instead of staying a placeholder.
    void NotifyMediaChanged();

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
    // Grid/List toggle — a two-button icon ButtonGroup; SetView is
    // the buttons' OnClick target, SyncViewButtons mirrors grid_mode_
    // onto Primary/Secondary like SyncFilterButtons does for kind.
    void SetView(bool grid);
    void SyncViewButtons();
    // Warm peaks lookup for an audio tile's wave strip: bounded LRU
    // for hits, a bounded negative cache for permanent misses, and a
    // per-frame re-ask while the worker reports kPending (the shell
    // hook itself enqueues — never blocks the UI thread).
    const AudioPeaks* PeaksFor(const std::string& path);
    // Min/max column strip drawn inside `strip` from a validated
    // AudioPeaks payload (all channels merged, ~3 px per bar).
    void DrawWaveStrip(XER::UI::Rendering::Renderer& renderer,
                       const AudioPeaks& peaks,
                       const XER::UI::Layout::Rect& strip,
                       XER::UI::Theme::Color color);

    Hooks hooks_;
    std::function<void(const std::string&)> on_remove_entry_;
    std::function<void(const std::string&)> on_reveal_entry_;
    std::vector<CatalogEntry> entries_;
    int selected_row_ = -1;
    bool children_attached_ = false;
    // CapCut-style poster grid (default) vs. the table list — toggled
    // by the grid/list icon group on the actions row.
    bool grid_mode_ = true;
    float grid_scroll_ = 0.0f;
    int grid_hovered_ = -1;
    XER::UI::Layout::Rect grid_rect_{};
    // Kind filter strip above the content ("All | Video | Audio") —
    // `visible_` maps a displayed row/card to its entries_ index,
    // already filtered by the search needle and ordered by sort_.
    int filter_ = 0;
    std::vector<int> visible_;
    void RebuildVisible();
    // Live substring filter over names+paths (search_ mirrors the
    // TextInput value) and the in-session sort selection — the
    // dropdown's option order is the index contract below.
    std::string search_;
    int sort_ = 0;   // 0 Name, 1 Duration, 2 Size, 3 Kind, 4 Imported
    // Peaks strip caches, bounded like the timeline's: ready payloads
    // survive repaints; permanent misses don't re-probe per frame.
    std::unordered_map<std::string, std::shared_ptr<const AudioPeaks>>
        peaks_cache_;
    std::deque<std::string> peaks_lru_;
    std::unordered_set<std::string> peaks_missing_;
    std::deque<std::string> peaks_missing_lru_;
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
    std::shared_ptr<XER::UI::Widgets::TextInput> search_input_;
    std::shared_ptr<XER::UI::Widgets::Button> import_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> add_btn_;
    std::shared_ptr<XER::UI::Widgets::ButtonGroup> view_group_;
    std::shared_ptr<XER::UI::Widgets::Button> grid_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> list_btn_;
    std::shared_ptr<XER::UI::Widgets::Select> sort_select_;
    std::shared_ptr<XER::UI::Widgets::ButtonGroup> filter_group_;
    std::shared_ptr<XER::UI::Widgets::Button> filter_all_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> filter_video_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> filter_audio_btn_;
    std::shared_ptr<XER::UI::Widgets::Table> table_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
    // Left-gutter box the search icon draws into (children paint
    // after the panel, so an icon inside the TextInput's own bounds
    // would be covered — the gutter sits outside its rect).
    XER::UI::Layout::Rect search_icon_rect_{};
};

}  // namespace XER::VideoEditor::Ui
