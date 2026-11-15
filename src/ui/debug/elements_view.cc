#include "elements_view.h"

#include "debug_internal.h"
#include "../rendering/renderer.h"
#include "../theme/tokens.h"
#include "../layout/geometry.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace XER::UI::Debug {

namespace Widgets = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;
namespace Theme = XER::UI::Theme;

namespace {

constexpr float kIndent = 16.0f;
constexpr float kExpanderW = 18.0f;
constexpr float kPadX = 6.0f;

Theme::Color WithAlpha(Theme::Color c, float alpha) {
    c.a *= alpha;
    return c;
}

} // namespace

ElementsView::ElementsView() { SetClipsChildren(true); }

void ElementsView::SetRoot(std::shared_ptr<Widgets::Widget> root) {
    root_ = std::move(root);
    needs_rebuild_ = true;
    Invalidate();
}

bool ElementsView::IsExpanded(const Widgets::Widget& w, int depth) const {
    if (const auto it = expanded_.find(&w); it != expanded_.end()) {
        return it->second;
    }
    // DevTools default: shallow nodes open, deep nodes collapsed so a
    // large tree does not flood the panel on first attach.
    return depth < 3;
}

void ElementsView::ToggleExpanded(const Widgets::Widget& w) {
    // Find the row's depth from the flat model.
    for (const auto& row : rows_) {
        if (row.widget.get() == &w) {
            expanded_[&w] = !IsExpanded(w, row.depth);
            needs_rebuild_ = true;
            Invalidate();
            return;
        }
    }
}

void ElementsView::Collect(std::shared_ptr<Widgets::Widget> w, int depth) {
    Row row;
    row.widget = w;
    row.depth = depth;
    row.has_children = !w->GetChildren().empty();
    rows_.push_back(std::move(row));

    if (!IsExpanded(*w, depth)) return;
    for (const auto& child : w->GetChildren()) {
        if (child) Collect(child, depth + 1);
    }
}

void ElementsView::Rebuild() {
    rows_.clear();
    if (root_) Collect(root_, 0);
    needs_rebuild_ = false;
    // Keep selection valid: drop it when the widget left the tree.
    if (selected_ && std::none_of(rows_.begin(), rows_.end(),
                                  [this](const Row& r) {
                                      return r.widget == selected_;
                                  })) {
        selected_.reset();
    }
}

void ElementsView::Refresh() {
    if (needs_rebuild_ || root_) {
        // Always rebuild on explicit refresh — the tree may have changed
        // shape since the last pass.
        Rebuild();
    }
    Invalidate();
}

void ElementsView::Select(std::shared_ptr<Widgets::Widget> widget) {
    selected_ = std::move(widget);
    Invalidate();
    if (on_select_ && selected_) on_select_(selected_);
}

int ElementsView::RowAtY(float local_y) const {
    if (local_y < 0.0f) return -1;
    const int idx = static_cast<int>(local_y / row_height_);
    return idx < static_cast<int>(rows_.size()) ? idx : -1;
}

bool ElementsView::PointOnExpander(const Row& row,
                                  const Layout::Point& p) const {
    if (!row.has_children) return false;
    const Layout::Rect b = GetBounds();
    const float x0 = b.x + kPadX + row.depth * kIndent;
    return p.x >= x0 && p.x <= x0 + kExpanderW;
}

void ElementsView::ActivateRow(int index) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    const Row& row = rows_[static_cast<std::size_t>(index)];
    Select(row.widget);
}

void ElementsView::Render(Rendering::Renderer& renderer,
                          const Theme::Tokens& tokens) {
    if (needs_rebuild_) Rebuild();
    const Layout::Rect b = GetBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return;

    renderer.FillRect(b, tokens.surface, tokens.radius_small);

    // The ScrollArea parent gives this widget bounds covering the whole
    // scrollable content (height = rows*row_h, y already offset by
    // -scroll). GetVisibleRect() reports the actual viewport, so visible
    // rows are those intersecting it — cull the rest.
    const Layout::Rect view = GetVisibleRect();
    const float content_top = b.y;
    int i0 = static_cast<int>(
        std::floor((view.y - content_top) / row_height_)) - 1;
    int i1 = static_cast<int>(
        std::ceil((view.y + view.height - content_top) / row_height_)) + 1;
    const int count = static_cast<int>(rows_.size());
    i0 = std::max(0, i0);
    i1 = std::min(count, i1);

    Rendering::TextStyle type_style;
    type_style.size = tokens.font_small;
    type_style.weight = 500;
    Rendering::TextStyle meta_style = type_style;
    meta_style.color = tokens.muted;
    meta_style.weight = 400;

    for (int i = i0; i < i1; ++i) {
        const Row& row = rows_[static_cast<std::size_t>(i)];
        const Widgets::Widget& w = *row.widget;
        const float ry = b.y + static_cast<float>(i) * row_height_;
        const Layout::Rect row_rect{b.x, ry, b.width, row_height_};

        const bool selected = selected_ && selected_.get() == row.widget.get();
        const bool hovered = hovered_ == i;
        if (selected) {
            renderer.FillRect(row_rect, WithAlpha(tokens.accent, 0.18f), 0.0f);
        } else if (hovered) {
            renderer.FillRect(row_rect, tokens.hover, 0.0f);
        }
        if (selected) {
            renderer.FillRect(
                Layout::Rect{b.x, ry + 2.0f, 2.0f, row_height_ - 4.0f},
                tokens.accent, 1.0f);
        }

        // Expander triangle (drawn, not a glyph — font-independent).
        if (row.has_children) {
            const float ex = b.x + kPadX + row.depth * kIndent + 5.0f;
            const float ey = ry + row_height_ * 0.5f;
            const bool open = IsExpanded(w, row.depth);
            const Theme::Color tc =
                selected ? tokens.accent_foreground : tokens.muted;
            if (open) {
                renderer.DrawLine({ex - 3.5f, ey - 2.0f}, {ex + 3.5f, ey - 2.0f},
                                  tc, 1.2f);
                renderer.DrawLine({ex - 3.5f, ey - 2.0f}, {ex, ey + 2.5f}, tc,
                                  1.2f);
                renderer.DrawLine({ex + 3.5f, ey - 2.0f}, {ex, ey + 2.5f}, tc,
                                  1.2f);
            } else {
                renderer.DrawLine({ex - 2.5f, ey - 3.5f}, {ex + 2.5f, ey}, tc,
                                  1.2f);
                renderer.DrawLine({ex - 2.5f, ey + 3.5f}, {ex + 2.5f, ey}, tc,
                                  1.2f);
            }
        }

        const float tx = b.x + kPadX + row.depth * kIndent + kExpanderW;
        const std::string tag = internal::ElementTag(w);
        const bool usable = w.IsVisible() && w.IsEnabled();
        type_style.color =
            selected ? tokens.accent_foreground
                     : (usable ? tokens.foreground : tokens.muted);
        renderer.DrawText(tag, Layout::Rect{tx, ry, b.width - tx - b.x - 4.0f,
                                            row_height_},
                          type_style);

        // Right-aligned-ish meta: bounds + state markers.
        const Layout::Rect wb = w.GetBounds();
        std::string meta = std::format("{:.0f},{:.0f} {:.0f}x{:.0f}",
                                       wb.x, wb.y, wb.width, wb.height);
        if (!w.IsEffectivelyVisible()) meta += " hidden";
        if (!w.IsEnabled()) meta += " disabled";
        const auto ms = renderer.MeasureText(meta, meta_style);
        renderer.DrawText(meta,
                          Layout::Rect{b.x + b.width - ms.width - 8.0f, ry,
                                       ms.width + 4.0f, row_height_},
                          meta_style);
    }
}

void ElementsView::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool ElementsView::HandleEvent(const Widgets::Event& event) {
    const Layout::Rect b = GetBounds();
    const float ly = event.position.y - b.y;
    const int idx = RowAtY(ly);

    switch (event.type) {
        case Widgets::Event::Type::MouseMove: {
            const int h = idx;
            if (h != hovered_) {
                hovered_ = h;
                Invalidate();
            }
            return false;
        }
        case Widgets::Event::Type::Click:
        case Widgets::Event::Type::MouseDown: {
            if (idx < 0) return false;
            const Row& row = rows_[static_cast<std::size_t>(idx)];
            if (PointOnExpander(row, event.position)) {
                ToggleExpanded(*row.widget);
                return true;
            }
            if (event.type == Widgets::Event::Type::Click) {
                ActivateRow(idx);
                return true;
            }
            return true;  // swallow MouseDown so parents don't refocus
        }
        case Widgets::Event::Type::MouseUp:
            return idx >= 0;
        default:
            return false;  // MouseWheel bubbles to the ScrollArea
    }
}

Layout::Size ElementsView::GetMinSize() const { return {120.0f, 80.0f}; }
Layout::Size ElementsView::GetPreferredSize() const {
    return {320.0f, 400.0f};
}
Layout::Size ElementsView::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Debug
