// BinGridSurface — the S7 media-bin poster grid of the ui2 rebuild
// (spec §6). A single custom-paint Widget hosting a virtualized card
// grid: poster thumbnails, name + meta rows, kind/proxy/audio badges
// and selection chrome are painted in ONE Render pass — no child
// widget per card (the big-bin G4 requirement: thousands of assets
// must not cost thousands of widgets).
//
// Ported contract (mirrors ui/media_bin_widget.cc grid mode — the
// legacy implementation stays untouched):
//   * Fixed-width 16:9 thumbs, ~132px card pitch, 8px gutters, an
//     internal scroll offset + floating scrollbar (VirtualList idiom).
//   * Click / Ctrl+click / Shift+click selection, marquee selection
//     from empty space, double-click to open the source monitor
//     (surfaces synthesize their own double-clicks — spec §6 rule 6).
//   * Poster decode through the tri-state MediaLookup hook — kReady
//     draws the BGRA frame, kPending keeps the placeholder and re-asks
//     next frame, kFailed is negative-cached (bounded, like
//     TimelineSurface::PrimeMediaLookups' contract).
//   * Drag-out is a surface-level handoff (the kit has no drag
//     events): a left-drag armed on a card promotes past a threshold,
//     then fires on_begin_drag / on_drag_update / on_end_drag — the
//     shell owns the session (drag_session.h) and hit-tests the
//     timeline drop.
//   * Context menus, file actions and imports are emitted as hooks;
//     the surface never mutates document state (surface_hooks.h
//     rules 1+2).
#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "layout/geometry.h"
#include "surface_hooks.h"
#include "widgets/widget.h"

namespace video {
class Frame;
}

namespace XER::UI::Rendering {
class Renderer;
}

namespace XER::VideoEditor::Ui2 {

// One bin card's display data — populated by the owning MediaPanel
// from CatalogEntry rows (path is the stable identity for selection
// and async lookups).
struct BinItem {
    std::string path;          // canonical UTF-8 media path
    std::string name;          // display label (basename or override)
    double duration_seconds = 0.0;
    bool has_video = false;
    bool has_audio = false;
    bool is_still = false;     // still-image tile (kind "Image")
    // Proxy status mirrored from CatalogEntry::proxy_state:
    // 0 none, 1 pending, 2 ready, 3 failed.
    int proxy_state = 0;
    // Disk poster PNG from the catalog — drawn when the async BGRA
    // lookup is unwired or fails; the async frame wins when ready.
    std::string poster_path;
    std::int64_t file_size_bytes = 0;
    double fps = 0.0;
    int width = 0, height = 0;
};

class BinGridSurface final : public XER::UI::Widgets::Widget {
public:
    BinGridSurface();

    // ---- model ----------------------------------------------------
    void SetItems(std::vector<BinItem> items);
    int ItemCount() const { return static_cast<int>(items_.size()); }
    const BinItem* Item(int index) const;
    // Invalidates the card and clears cached poster/miss state so the
    // next paint re-asks the lookups (shell's ConsumeRenderDirty seam).
    void NotifyMediaChanged();

    // ---- selection -------------------------------------------------
    // Index-keyed, mirroring VirtualList's contract — path-stable
    // identity is the panel's problem (it remaps on resort/filter).
    void SetSelectedIndices(std::set<int> indices);
    const std::set<int>& SelectedIndices() const { return selected_; }
    std::vector<std::string> SelectedPaths() const;
    // Fired only for USER-driven selection changes.
    void SetOnSelectionChanged(std::function<void()> cb);
    // Double-click on a card — the panel maps it to the source monitor.
    void SetOnActivateItem(std::function<void(int index)> cb);
    // Right-click anywhere: index of the card under the cursor, or -1
    // on empty space. The panel builds the ContextMenu.
    void SetOnContextMenu(
        std::function<void(int index, XER::UI::Layout::Point)> cb);

    // ---- async media (tri-state, never blocks paint) ---------------
    std::function<MediaLookup(const std::string&, double,
                              video::Frame*)>
        get_poster;
    std::function<MediaLookup(const std::string&, AudioPeaks*)>
        get_audio_peaks;

    // ---- drag-out handoff ------------------------------------------
    // on_begin_drag fires once when an armed press crosses the drag
    // threshold, with the dragged paths (the press card plus the rest
    // of the selection when the press card was selected). Update fires
    // per MouseMove with the absolute point; end fires on release.
    void SetOnBeginDrag(
        std::function<void(const std::vector<std::string>& paths,
                           XER::UI::Layout::Point)> cb);
    void SetOnDragUpdate(std::function<void(XER::UI::Layout::Point)> cb);
    void SetOnEndDrag(std::function<void(XER::UI::Layout::Point)> cb);

    // ---- grid geometry (DIP space; smoke asserts on these) ---------
    int Columns() const;
    int Rows() const;
    XER::UI::Layout::Size CardSize() const;
    // Absolute client rect of card `index` (scroll-applied).
    XER::UI::Layout::Rect CardRect(int index) const;
    // Index of the card at an absolute point, -1 on gaps/empty space.
    int ItemAtPoint(XER::UI::Layout::Point point) const;
    // [first, last) indices whose cards intersect the viewport — the
    // virtualization contract the smoke checks.
    std::pair<int, int> VisibleIndexRange() const;
    float ScrollY() const { return scroll_; }
    float MaxScrollY() const;
    void ScrollTo(float y);
    void EnsureVisible(int index);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {180.0f, 120.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {460.0f, 320.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }
    float GetContentHeight() const override;

private:
    // Cached poster frame — shared_ptr so an eviction can never dangle
    // a frame the paint loop still reads.
    struct PosterEntry {
        std::shared_ptr<const video::Frame> frame;
    };

    const video::Frame* PosterFor(const BinItem& item);
    const AudioPeaks* PeaksFor(const BinItem& item);
    void DrawCard(XER::UI::Rendering::Renderer& renderer,
                  const XER::UI::Theme::Tokens& tokens, int index,
                  const XER::UI::Layout::Rect& card);
    void DrawScrollbar(XER::UI::Rendering::Renderer& renderer,
                       const XER::UI::Theme::Tokens& tokens);
    XER::UI::Layout::Rect ScrollbarThumbRect() const;
    bool ScrollbarZoneContains(XER::UI::Layout::Point point) const;
    void ScrollFromPointer(float y);
    void ApplyMarqueeSelection();
    std::vector<std::string> DragPaths() const;
    void CancelGesture();

    std::vector<BinItem> items_;
    std::set<int> selected_;
    int anchor_ = -1;             // shift-range selection origin
    int hovered_ = -1;

    float scroll_ = 0.0f;
    bool scrollbar_drag_ = false;
    float scrollbar_grab_ = 0.0f;

    // Marquee selection state — active while a press that started on
    // empty space is held.
    bool marquee_active_ = false;
    XER::UI::Layout::Point marquee_anchor_{};
    XER::UI::Layout::Point marquee_end_{};
    std::set<int> marquee_base_;  // selection snapshot for ctrl-marquee

    // Press/drag state — armed on a card press, promoted to an active
    // drag once the cursor travels past the threshold.
    int pressed_index_ = -1;
    // Snapshot taken at press: was the card already in the selection?
    // Ctrl+release only toggles OFF a card that was selected before the
    // press — a ctrl+press that ADDED the card must not undo itself.
    bool pressed_was_selected_ = false;
    XER::UI::Layout::Point press_point_{};
    bool drag_armed_ = false;
    bool drag_active_ = false;

    // Double-click synthesis (surfaces own it — spec §6 rule 6).
    int last_up_index_ = -1;
    std::chrono::steady_clock::time_point last_up_time_{};
    XER::UI::Layout::Point last_up_point_{};

    // Poster cache — bounded LRU by path; misses are negative-cached
    // so a permanently-undecodable path is never re-probed per frame.
    std::unordered_map<std::string, PosterEntry> posters_;
    std::deque<std::string> posters_lru_;
    std::unordered_set<std::string> poster_failed_;
    std::deque<std::string> poster_failed_lru_;
    std::unordered_map<std::string, std::shared_ptr<const AudioPeaks>>
        peaks_;
    std::deque<std::string> peaks_lru_;
    std::unordered_set<std::string> peaks_failed_;
    std::deque<std::string> peaks_failed_lru_;

    std::function<void()> on_selection_changed_;
    std::function<void(int)> on_activate_item_;
    std::function<void(int, XER::UI::Layout::Point)> on_context_menu_;
    std::function<void(const std::vector<std::string>&,
                       XER::UI::Layout::Point)>
        on_begin_drag_;
    std::function<void(XER::UI::Layout::Point)> on_drag_update_;
    std::function<void(XER::UI::Layout::Point)> on_end_drag_;
};

}  // namespace XER::VideoEditor::Ui2
