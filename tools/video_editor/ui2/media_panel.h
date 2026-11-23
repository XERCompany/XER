// MediaPanel — the M3 media bin of the ui2 rebuild (spec §6 "Media"
// dock tab). Widget-composed chrome around the BinGridSurface:
// search TextInput + sort/filter controls + import button + a
// grid/list view toggle whose list mode is a VirtualList (the
// rebuild-spec pairing "BinGridSurface or VirtualList for the list").
//
// Data flow mirrors the shell's ownership rules (surface_hooks.h):
// the panel holds a PRESENTATION copy of the catalog rows pushed via
// SetEntries (the shell owns the MediaCatalog), and every file action
// leaves through the Hooks bundle — the panel never touches
// MediaCatalog, the timeline or TimelineHistory itself.
//
// Ported behavior (ui/media_bin_widget.{h,cc} — legacy untouched):
//   * case-insensitive substring search over the whole path;
//   * kind filter All / Video / Audio;
//   * sort menu Name / Duration / Size / Kind / Imported — every key
//     tie-breaks on name (id on ties) so ordering is deterministic;
//   * selection survives re-feeds by path;
//   * double-click (grid) / Enter (list) -> on_open_source;
//   * context menu import / proxy / remove / reveal items;
//   * drag-out through the surface's begin/update/end hooks.
#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "bin_surface.h"
#include "layout/geometry.h"
#include "media_catalog.h"   // CatalogEntry — the pushed row type
#include "surface_hooks.h"
#include "widgets/widget.h"

namespace video {
class Frame;
}

namespace XER::UI::Widgets {
class Button;
class ButtonGroup;
class Label;
class Select;
class TextInput;
class VirtualList;
}

namespace XER::VideoEditor::Ui2 {

class MediaPanel final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<MediaPanel> Create();

    // Every shell-facing seam — all optional unless noted. The shell
    // implements them against the catalog / history / platform layer.
    struct Hooks {
        // Toolbar + context "Import…" / "Import from URL…".
        std::function<void()> on_import;
        std::function<void()> on_import_url;
        // Card double-click / list Enter / context "Open in Source".
        std::function<void(const std::string& path)> on_open_source;
        // Context "Add to Timeline" — inserts at the playhead.
        std::function<void(const std::string& path)> on_add_to_timeline;
        // Context "Remove from Bin" — the full selection's paths.
        std::function<void(const std::vector<std::string>& paths)>
            on_remove_entries;
        // Context "Reveal in Explorer".
        std::function<void(const std::string& path)> on_reveal_entry;
        // Context "Create Proxy" / "Cancel Proxy".
        std::function<void(const std::string& path)> on_create_proxy;
        std::function<void(const std::string& path)> on_cancel_proxy;
        // Context "AI Upscale 4x" (video entries) — the shell runs
        // the async x4v3 bounce (ai/upscale_x4v3.h + ai/ai_bounce.h)
        // and imports the produced <stem>.xer.upscaled.mp4 back into
        // the catalog. Unset -> the item hides.
        std::function<void(const std::string& path)> on_ai_upscale;
        // Selection fan-out (grid + list both feed this).
        std::function<void(const std::vector<std::string>& paths)>
            on_select;
        std::function<void(const std::string&, bool is_error)> on_status;
        // Tri-state async seams — forwarded to BinGridSurface (never
        // block paint; the shell answers from a warm worker pool).
        std::function<MediaLookup(const std::string&, double,
                                  video::Frame*)>
            get_poster;
        std::function<MediaLookup(const std::string&, AudioPeaks*)>
            get_audio_peaks;
        // Drag-out handoff — the shell owns Ui2DragSession.
        std::function<void(const std::vector<std::string>& paths,
                           XER::UI::Layout::Point)>
            on_begin_drag;
        std::function<void(XER::UI::Layout::Point)> on_drag_update;
        std::function<void(XER::UI::Layout::Point)> on_end_drag;
    };
    void SetHooks(Hooks hooks);
    const Hooks& GetHooks() const { return hooks_; }

    // ---- data (shell pushes catalog snapshots) ---------------------
    void SetEntries(std::vector<CatalogEntry> entries);
    int EntryCount() const { return static_cast<int>(entries_.size()); }
    // The shell's media-dirty drain: retries poster/peaks misses.
    void NotifyMediaChanged();
    void SetStatus(const std::string& text, bool is_error = false);

    // ---- view state --------------------------------------------------
    void SetGridMode(bool grid);
    bool IsGridMode() const { return grid_mode_; }
    void SetSearchText(std::string text);
    const std::string& SearchText() const { return search_; }
    enum class SortMode : int {
        Name = 0, Duration = 1, Size = 2, Kind = 3, Imported = 4,
    };
    void SetSortMode(SortMode mode);
    SortMode GetSortMode() const { return sort_; }
    enum class KindFilter : int { All = 0, Video = 1, Audio = 2 };
    void SetKindFilter(KindFilter filter);
    KindFilter GetKindFilter() const { return filter_; }

    // Filtered+sorted order — the panel-level truth (visible_ indexes
    // into entries_). The smoke asserts on these.
    std::vector<std::string> VisiblePaths() const;
    int VisibleCount() const { return static_cast<int>(visible_.size()); }
    std::vector<std::string> SelectedPaths() const;

    // Tree-level context-menu seam: right-clicks the list's internal
    // VirtualList left unconsumed bubble up to the shell's handler,
    // which forwards here. Returns true when a menu was shown.
    bool ShowContextMenuAt(XER::UI::Layout::Point point);

    // Escape hatch for the shell's drag/session plumbing and tests.
    BinGridSurface* Grid() const { return grid_.get(); }

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
        return {380.0f, 480.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit MediaPanel();

    void EnsureChildren();
    void RebuildVisible();
    void PushSelectionToViews();
    void PullSelectionFromGrid();
    void PullSelectionFromList();
    void SyncViewButtons();
    void SyncStatusText();
    void ShowCardMenu(int visible_index, XER::UI::Layout::Point point);
    int ListIndexAt(XER::UI::Layout::Point point) const;
    const CatalogEntry* EntryForVisible(int visible_index) const;

    std::vector<CatalogEntry> entries_;
    std::vector<int> visible_;           // display order -> entries_
    std::set<std::string> selected_paths_;
    std::string search_;
    SortMode sort_ = SortMode::Name;
    KindFilter filter_ = KindFilter::All;
    bool grid_mode_ = true;

    std::shared_ptr<XER::UI::Widgets::TextInput> search_input_;
    std::shared_ptr<XER::UI::Widgets::Button> import_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> sort_btn_;
    std::shared_ptr<XER::UI::Widgets::Select> filter_select_;
    std::shared_ptr<XER::UI::Widgets::ButtonGroup> view_group_;
    std::shared_ptr<XER::UI::Widgets::Button> grid_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> list_btn_;
    std::shared_ptr<BinGridSurface> grid_;
    std::shared_ptr<XER::UI::Widgets::VirtualList> list_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;

    bool children_attached_ = false;
    Hooks hooks_;
};

}  // namespace XER::VideoEditor::Ui2
