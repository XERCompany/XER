#include "table.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

std::shared_ptr<Table> Table::Create() {
    return std::shared_ptr<Table>(new Table());
}

Table::Table() {
    SetClipsChildren(true);
}

void Table::SetColumns(std::vector<Column> columns) {
    columns_ = std::move(columns);
    RequestLayout();
    Invalidate();
}

void Table::SetRows(std::vector<std::vector<std::string>> rows) {
    rows_ = std::move(rows);
    selected_ = -1;
    scroll_ = 0.0f;
    RequestLayout();
    Invalidate();
}

void Table::SetHeader(bool show) {
    show_header_ = show;
    RequestLayout();
    Invalidate();
}

void Table::SetRowHeight(float px) {
    row_height_ = std::max(18.0f, px);
    RequestLayout();
}

void Table::SetSelectedRow(int index) {
    selected_ = (index >= 0 && index < static_cast<int>(rows_.size()))
                    ? index
                    : -1;
    Invalidate();
}

void Table::SetOnRowClick(std::function<void(int)> cb) {
    on_row_click_ = std::move(cb);
}

void Table::SetOnCellClick(std::function<void(int, int)> cb) {
    on_cell_click_ = std::move(cb);
}

void Table::ScrollTo(int row) {
    const float y = row_height_ * static_cast<float>(row);
    const auto b = GetBounds();
    const float view_h = b.height - (show_header_ ? row_height_ : 0.0f);
    scroll_ = std::clamp(y - view_h * 0.5f, 0.0f, max_scroll_);
    Invalidate();
}

int Table::RowAtY(float y) const {
    const auto b = GetBounds();
    const float header_h = show_header_ ? row_height_ : 0.0f;
    const int idx = static_cast<int>(std::floor((y - b.y - header_h + scroll_) /
                                              row_height_));
    return (idx >= 0 && idx < static_cast<int>(rows_.size())) ? idx : -1;
}

int Table::ColAtX(float x) const {
    const auto b = GetBounds();
    float cx = b.x;
    const float total = std::max(1.0f, b.width);
    float explicit_w = 0.0f;
    for (const auto& c : columns_) explicit_w += c.width;
    const float auto_w = columns_.empty()
                             ? 0.0f
                             : (total - explicit_w) /
                                   static_cast<float>(columns_.size());
    for (size_t i = 0; i < columns_.size(); ++i) {
        const float w = columns_[i].width > 0.0f ? columns_[i].width : auto_w;
        if (x >= cx && x < cx + w) return static_cast<int>(i);
        cx += w;
    }
    return -1;
}

Layout::Rect Table::CellRect(int row, int col,
                             const Layout::Rect& b) const {
    const float header_h = show_header_ ? row_height_ : 0.0f;
    float cx = b.x;
    const float total = std::max(1.0f, b.width);
    float explicit_w = 0.0f;
    for (const auto& c : columns_) explicit_w += c.width;
    const float auto_w = columns_.empty()
                             ? 0.0f
                             : (total - explicit_w) /
                                   static_cast<float>(columns_.size());
    for (int i = 0; i < col; ++i) {
        const float w = columns_[i].width > 0.0f ? columns_[i].width : auto_w;
        cx += w;
    }
    const float w =
        col >= 0 && col < static_cast<int>(columns_.size())
            ? (columns_[col].width > 0.0f ? columns_[col].width : auto_w)
            : 0.0f;
    return {cx, b.y + header_h + row_height_ * row - scroll_, w, row_height_};
}

void Table::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    const float header_h = show_header_ ? row_height_ : 0.0f;
    const float view_h = std::max(0.0f, bounds.height - header_h);
    const float content_h = row_height_ * static_cast<float>(rows_.size());
    max_scroll_ = std::max(0.0f, content_h - view_h);
    scroll_ = std::clamp(scroll_, 0.0f, max_scroll_);
    scrollbar_rect_ = {bounds.x + bounds.width - 6.0f, bounds.y + header_h,
                       6.0f, view_h};
}

void Table::Render(Rendering::Renderer& renderer,
                   const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return;
    const float header_h = show_header_ ? row_height_ : 0.0f;

    // Surface + border.
    renderer.FillRect(b, tokens.surface, tokens.radius_medium);
    renderer.StrokeRect(b, tokens.border, 1.0f, tokens.radius_medium);

    // Header.
    if (show_header_ && !columns_.empty()) {
        Layout::Rect hr{b.x, b.y, b.width, header_h};
        renderer.FillRect(hr, tokens.muted, tokens.radius_medium);
        float cx = b.x;
        const float total = std::max(1.0f, b.width);
        float explicit_w = 0.0f;
        for (const auto& c : columns_) explicit_w += c.width;
        const float auto_w = (total - explicit_w) /
                             static_cast<float>(columns_.size());
        const Rendering::TextStyle hs{tokens.foreground,
                                      tokens.font_small, 600};
        for (size_t i = 0; i < columns_.size(); ++i) {
            const float w = columns_[i].width > 0.0f
                                ? columns_[i].width
                                : auto_w;
            Layout::Rect tr{cx + 8.0f, b.y, w - 16.0f, header_h};
            renderer.DrawText(columns_[i].header, tr, hs);
            cx += w;
        }
    }

    // Rows (clipped to the body area).
    renderer.PushClip({b.x, b.y + header_h, b.width,
                       b.height - header_h});
    for (size_t r = 0; r < rows_.size(); ++r) {
        const float ry = b.y + header_h + row_height_ *
                                     static_cast<float>(r) - scroll_;
        if (ry + row_height_ < b.y + header_h || ry > b.y + b.height) continue;
        const bool sel = static_cast<int>(r) == selected_;
        const bool hov = static_cast<int>(r) == hovered_;
        if (sel) {
            renderer.FillRect({b.x, ry, b.width, row_height_},
                              tokens.accent, 0.0f);
        } else if (hov) {
            renderer.FillRect({b.x, ry, b.width, row_height_},
                              tokens.hover, 0.0f);
        }
        // Row separator.
        renderer.DrawLine({b.x, ry + row_height_},
                          {b.x + b.width, ry + row_height_},
                          tokens.border, 1.0f);
        // Cells.
        float cx = b.x;
        const float total = std::max(1.0f, b.width);
        float explicit_w = 0.0f;
        for (const auto& c : columns_) explicit_w += c.width;
        const float auto_w = columns_.empty()
                                 ? 0.0f
                                 : (total - explicit_w) /
                                       static_cast<float>(columns_.size());
        const Rendering::TextStyle cs{sel ? tokens.accent_foreground
                                          : tokens.foreground,
                                      tokens.font_small, 400};
        for (size_t c = 0; c < rows_[r].size() &&
                           c < columns_.size(); ++c) {
            const float w = columns_[c].width > 0.0f
                                ? columns_[c].width
                                : auto_w;
            Layout::Rect tr{cx + 8.0f, ry, w - 16.0f, row_height_};
            renderer.DrawText(rows_[r][c], tr, cs);
            cx += w;
        }
    }
    renderer.PopClip();

    // Scrollbar thumb.
    if (max_scroll_ > 0.0f && scrollbar_rect_.height > 0.0f) {
        const float track_h = scrollbar_rect_.height;
        const float content_h = row_height_ *
                                static_cast<float>(rows_.size());
        const float thumb_h = std::max(24.0f,
                                       track_h * track_h / content_h);
        const float thumb_y = scrollbar_rect_.y +
                              (track_h - thumb_h) * (scroll_ / max_scroll_);
        renderer.FillRect({scrollbar_rect_.x, thumb_y,
                           scrollbar_rect_.width, thumb_h},
                          tokens.muted, 3.0f);
    }
}

void Table::Tick(double) {}

bool Table::HandleEvent(const Event& event) {
    const auto& p = event.position;
    const auto b = GetBounds();
    const float header_h = show_header_ ? row_height_ : 0.0f;

    switch (event.type) {
        case Event::Type::MouseMove: {
            if (scrollbar_drag_) {
                const float track = std::max(1.0f, scrollbar_rect_.height);
                const float content_h = row_height_ *
                                        static_cast<float>(rows_.size());
                const float thumb_h = std::max(
                    24.0f, track * track / std::max(1.0f, content_h));
                const float travel = std::max(1.0f, track - thumb_h);
                const float thumb_top = p.y - scrollbar_drag_y_;
                const float rel = (thumb_top - scrollbar_rect_.y) / travel;
                scroll_ = std::clamp(rel * max_scroll_, 0.0f, max_scroll_);
                Invalidate();
                return true;
            }
            const int row = RowAtY(p.y);
            if (row != hovered_) {
                hovered_ = row;
                Invalidate();
            }
            return false;
        }
        case Event::Type::Click:
        case Event::Type::MouseDown: {
            // Scrollbar drag region.
            if (max_scroll_ > 0.0f && p.x >= scrollbar_rect_.x - 4.0f &&
                p.x <= scrollbar_rect_.x + scrollbar_rect_.width + 4.0f &&
                p.y >= scrollbar_rect_.y &&
                p.y <= scrollbar_rect_.y + scrollbar_rect_.height) {
                // Grab offset inside the thumb so the thumb doesn't jump.
                const float track = scrollbar_rect_.height;
                const float content_h = row_height_ *
                                        static_cast<float>(rows_.size());
                const float thumb_h = std::max(
                    24.0f, track * track / std::max(1.0f, content_h));
                const float thumb_top =
                    scrollbar_rect_.y +
                    (track - thumb_h) * (scroll_ / max_scroll_);
                scrollbar_drag_y_ = p.y - thumb_top;  // px inside thumb
                scrollbar_drag_ = true;
                return true;
            }
            if (event.type == Event::Type::Click) {
                const int row = RowAtY(p.y);
                const int col = ColAtX(p.x);
                if (row >= 0) {
                    selected_ = row;
                    if (on_row_click_) on_row_click_(row);
                    if (col >= 0 && on_cell_click_) on_cell_click_(row, col);
                    Invalidate();
                    return true;
                }
            }
            return false;
        }
        case Event::Type::MouseUp:
            if (scrollbar_drag_) {
                scrollbar_drag_ = false;
                return true;
            }
            return false;
        case Event::Type::MouseWheel:
            scroll_ = std::clamp(scroll_ - event.wheel_delta * row_height_,
                                 0.0f, max_scroll_);
            Invalidate();
            return true;
        default:
            return false;
    }
}

Layout::Size Table::GetMinSize() const {
    return {120.0f, 60.0f};
}

Layout::Size Table::GetPreferredSize() const {
    return {480.0f, 240.0f};
}

Layout::Size Table::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
