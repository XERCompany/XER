#ifndef XER_UI_WIDGETS_VIRTUAL_LIST_H
#define XER_UI_WIDGETS_VIRTUAL_LIST_H

#include <functional>
#include <memory>
#include <set>
#include <string>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// VirtualList — virtualized row list for large datasets (gap G4 substrate
// from docs/wiki/research/2026-11-22-registry-data-widgets.md).
//
// Render-callback model: rows are painted by ONE std::function invoked per
// visible row per frame — not by per-row child widgets — so a 10k+ item
// set costs O(visible + overscan) per frame. That is the point versus
// Table (which stores and walks every row).
//
// Item height is FIXED (SetItemHeight, default 26 DIP). Per-item heights
// are NOT supported — measuring 100k rows per layout would defeat the
// virtualization; callers needing variable heights should bucket items or
// pre-measure on their side and take the max.
//
// Scrolling is self-contained (own scroll offset + internal floating
// scrollbar styled like ScrollArea's 8px rail / 2px pad / 14px hit strip).
// Wrapping a ScrollArea was rejected: ScrollArea virtualizes nothing — it
// lays out a single child at full content height and would need a real
// widget per row to scroll past, which is exactly what this widget avoids.
//
// Selection is index-keyed (stable identity is the caller's problem —
// map indices to row ids on your side). Single mode: click selects,
// arrows move selection. Multiple mode: click selects-one, Ctrl+click
// toggles, Shift+click/arrow extends a range from the anchor, Space
// toggles the cursor row without disturbing the rest.
class VirtualList final : public Widget {
public:
    enum class SelectionMode { None, Single, Multiple };

    // Paints row `index` into `row` (absolute client coords). `selected`
    // is the list selection state; row backgrounds (hover/selection/cursor
    // chrome) are drawn by the list BEFORE this runs, so the callback only
    // needs to draw content. `row` may extend past the viewport — the list
    // holds a clip.
    using ItemRenderer = std::function<void(Rendering::Renderer& renderer,
                                            Layout::Rect row,
                                            int index,
                                            bool selected)>;

    static std::shared_ptr<VirtualList> Create();

    void SetItemCount(int count);
    int GetItemCount() const { return item_count_; }
    // Fixed row height in DIP, clamped to >= 18. SetRowHeight is the
    // table-flavored alias.
    void SetItemHeight(float px);
    float GetItemHeight() const { return item_height_; }
    void SetRowHeight(float px) { SetItemHeight(px); }
    void SetItemRenderer(ItemRenderer cb);
    // Extra rows painted past each viewport edge (default 4) so fast
    // wheel scrolls never flash empty space.
    void SetOverscan(int rows);
    int GetOverscan() const { return overscan_; }

    // ---- selection -----------------------------------------------------
    void SetSelectionMode(SelectionMode mode);
    SelectionMode GetSelectionMode() const { return selection_mode_; }
    void SetSelectedIndex(int index);  // -1 clears
    int GetSelectedIndex() const;
    void SetSelectedIndices(std::set<int> indices);
    const std::set<int>& GetSelectedIndices() const { return selected_; }
    bool IsRowSelected(int index) const { return selected_.count(index) != 0; }
    // Fired only for USER-driven selection changes (click/keys) — Set*
    // methods never fire it.
    void SetOnSelectionChanged(std::function<void()> cb);
    // Enter on the cursor row (and Space in non-Multiple modes).
    void SetOnItemActivated(std::function<void(int index)> cb);
    // Row click hook fired BEFORE the built-in selection update; return
    // true to consume the click and skip selection handling entirely
    // (TreeView uses this for chevron/checkbox sub-row hit zones).
    void SetOnItemClick(
        std::function<bool(int index, Layout::Point position)> cb);
    // Keys the list does not consume (ArrowLeft/Right, Escape, TextInput,
    // letters...) are offered to this hook last — TreeView's tree
    // navigation and typeahead live behind it.
    void SetKeyFallback(std::function<bool(const Event& event)> cb);

    // The keyboard/active row. In Single selection mode built-in key
    // navigation keeps selection on the cursor. SetCursorIndex only moves
    // the cursor (+ EnsureVisible); it never changes selection.
    int GetCursorIndex() const { return cursor_; }
    void SetCursorIndex(int index);

    // ---- scrolling -----------------------------------------------------
    void ScrollToIndex(int index);   // center the row in the viewport
    void EnsureVisible(int index);   // minimal scroll so the row is visible
    void ScrollTo(float y);
    float GetScrollY() const { return scroll_; }
    float GetScrollRange() const { return max_scroll_; }
    // Index range currently painted (INCLUDING overscan) — handy for
    // tests and for pre-warming data the renderer callback will want.
    int FirstPaintedIndex() const;
    int LastPaintedIndex() const;

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;
    float GetContentHeight() const override {
        return item_height_ * static_cast<float>(item_count_);
    }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit VirtualList();

    int RowAtY(float y) const;
    void MoveCursor(int delta, bool extend_selection);
    void SetCursorPage(int delta_pages, bool extend_selection);
    void ApplyCursorSelection();
    bool ScrollbarZoneContains(const Layout::Point& p) const;
    Layout::Rect ScrollbarThumbRect() const;
    void ScrollFromPointer(float y);
    int PaintedRangeEnd() const;

    int item_count_ = 0;
    float item_height_ = 26.0f;
    int overscan_ = 4;
    ItemRenderer item_renderer_;

    SelectionMode selection_mode_ = SelectionMode::Single;
    std::set<int> selected_;
    int cursor_ = -1;
    int anchor_ = -1;   // shift-range selection origin
    int hovered_ = -1;

    float scroll_ = 0.0f;
    float max_scroll_ = 0.0f;
    Layout::Rect scrollbar_rect_{};
    bool scrollbar_drag_ = false;
    float scrollbar_grab_ = 0.0f;   // px inside the thumb at drag start
    bool scrollbar_hovered_ = false;

    std::function<void()> on_selection_changed_;
    std::function<void(int)> on_item_activated_;
    std::function<bool(int, Layout::Point)> on_item_click_;
    std::function<bool(const Event&)> key_fallback_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_VIRTUAL_LIST_H
