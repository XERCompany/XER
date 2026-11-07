#include "pagination.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kButtonSize = 32.0f;
constexpr float kGap = 4.0f;

} // namespace

Pagination::Pagination(int page_count, int current_page)
    : page_count_(std::max(1, page_count))
    , current_page_(std::clamp(current_page, 0, page_count_ - 1)) {
}

void Pagination::SetPageCount(int count) {
    page_count_ = std::max(1, count);
    current_page_ = std::clamp(current_page_, 0, page_count_ - 1);
    RequestLayout();
    Invalidate();
}

void Pagination::SetCurrentPage(int page) {
    GoTo(page);
}

void Pagination::SetOnPageChanged(std::function<void(int)> callback) {
    on_page_changed_ = std::move(callback);
}

std::vector<int> Pagination::VisiblePages() const {
    std::vector<int> pages;
    const int count = page_count_;
    if (count <= 7) {
        for (int i = 0; i < count; ++i) {
            pages.push_back(i);
        }
        return pages;
    }
    pages.push_back(0);
    if (current_page_ > 2) {
        pages.push_back(-1);
    }
    const int lo = std::max(1, current_page_ - 1);
    const int hi = std::min(count - 2, current_page_ + 1);
    for (int i = lo; i <= hi; ++i) {
        pages.push_back(i);
    }
    if (current_page_ < count - 3) {
        pages.push_back(-1);
    }
    pages.push_back(count - 1);
    return pages;
}

void Pagination::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    for (std::size_t i = 0; i < button_rects_.size(); ++i) {
        const auto& rect = button_rects_[i];
        const int page = button_pages_[i];
        const bool active = page == current_page_;
        const bool hovered = static_cast<int>(i) == hovered_button_ && IsEnabled();
        const bool disabled = (page == -2 && current_page_ == 0) ||
                              (page == -3 && current_page_ == page_count_ - 1) || page == -1;

        Theme::Color fill = active ? tokens.foreground : tokens.surface;
        Theme::Color stroke = active ? tokens.foreground : tokens.border;
        Theme::Color text_color = active ? tokens.surface : tokens.foreground;
        if (hovered && !disabled) {
            fill = active ? tokens.foreground : tokens.hover;
        }
        if (disabled) {
            fill = tokens.surface;
            stroke = tokens.border;
            text_color = tokens.muted;
        }

        renderer.FillRect(rect, fill, tokens.radius_medium);
        renderer.StrokeRect(rect, stroke, 1.0f, tokens.radius_medium);

        std::string label;
        if (page == -2) {
            label = "<";
        } else if (page == -3) {
            label = ">";
        } else if (page == -1) {
            label = "...";
        } else {
            label = std::to_string(page + 1);
        }
        const Rendering::TextStyle style{text_color, tokens.font_small, active ? 600 : 500};
        renderer.DrawText(label, rect, style);
    }
    (void)bounds;
}

void Pagination::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    button_rects_.clear();
    button_pages_.clear();

    std::vector<int> sequence;
    sequence.push_back(-2); // prev
    const auto pages = VisiblePages();
    sequence.insert(sequence.end(), pages.begin(), pages.end());
    sequence.push_back(-3); // next

    float x = bounds.x + (bounds.width - static_cast<float>(sequence.size()) * (kButtonSize + kGap) + kGap) * 0.5f;
    if (x < bounds.x) {
        x = bounds.x;
    }
    for (const int page : sequence) {
        button_rects_.push_back(Layout::Rect{x, bounds.y, kButtonSize, kButtonSize});
        button_pages_.push_back(page);
        x += kButtonSize + kGap;
    }
}

bool Pagination::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }
    switch (event.type) {
        case Event::Type::MouseMove: {
            const int hit = HitButton(event.position);
            if (hit != hovered_button_) {
                hovered_button_ = hit;
                Invalidate();
            }
            return hit >= 0;
        }
        case Event::Type::Click: {
            const int index = HitButton(event.position);
            if (index < 0) {
                return false;
            }
            const int page = button_pages_[static_cast<std::size_t>(index)];
            if (page == -2) {
                GoTo(current_page_ - 1);
            } else if (page == -3) {
                GoTo(current_page_ + 1);
            } else if (page >= 0) {
                GoTo(page);
            }
            return true;
        }
        default:
            return false;
    }
}

int Pagination::HitButton(const Layout::Point& position) const {
    for (std::size_t i = 0; i < button_rects_.size(); ++i) {
        if (button_rects_[i].Contains(position)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Pagination::GoTo(int page) {
    page = std::clamp(page, 0, page_count_ - 1);
    if (page == current_page_) {
        return;
    }
    current_page_ = page;
    Invalidate();
    RequestLayout();
    if (on_page_changed_) {
        on_page_changed_(page);
    }
}

Layout::Size Pagination::GetMinSize() const {
    return {0.0f, kButtonSize};
}

Layout::Size Pagination::GetPreferredSize() const {
    return {static_cast<float>(page_count_ + 2) * (kButtonSize + kGap), kButtonSize};
}

Layout::Size Pagination::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
