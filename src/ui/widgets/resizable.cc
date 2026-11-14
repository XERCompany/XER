#include "resizable.h"

#include <algorithm>
#include <numeric>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

std::shared_ptr<Resizable> Resizable::Create(Direction dir) {
    return std::shared_ptr<Resizable>(new Resizable(dir));
}

Resizable::Resizable(Direction dir) : dir_(dir) {
    SetClipsChildren(true);
}

void Resizable::AddPanel(Panel panel) {
    if (panel.widget) AddChild(panel.widget);
    panels_.push_back(std::move(panel));
    sizes_.assign(panels_.size(), 0.0f);
    sizes_.back() = panels_.back().size;
    RequestLayout();
}

void Resizable::ClearPanels() {
    panels_.clear();
    sizes_.clear();
    handle_rects_.clear();
    RequestLayout();
}

void Resizable::SetDirection(Direction dir) {
    dir_ = dir;
    RequestLayout();
}

void Resizable::SetHandleSize(float px) {
    handle_size_ = std::max(2.0f, px);
    Invalidate();
}

void Resizable::SetOnLayout(
    std::function<void(const std::vector<float>&)> cb) {
    on_layout_ = std::move(cb);
}

void Resizable::SetCollapsed(int index, bool collapsed) {
    if (index < 0 || index >= static_cast<int>(panels_.size())) return;
    sizes_[index] = collapsed ? 0.0f : panels_[index].size;
    RequestLayout();
}

bool Resizable::IsCollapsed(int index) const {
    if (index < 0 || index >= static_cast<int>(sizes_.size())) return false;
    return sizes_[index] <= 0.001f;
}

void Resizable::NotifyLayout() {
    if (on_layout_) on_layout_(sizes_);
}

void Resizable::RecomputeSizes(const Layout::Rect& bounds) {
    const int n = static_cast<int>(panels_.size());
    if (n == 0) return;
    const float total =
        (dir_ == Direction::Horizontal ? bounds.width : bounds.height) -
        handle_size_ * static_cast<float>(n - 1);
    if (total <= 0.0f) {
        std::fill(sizes_.begin(), sizes_.end(), 0.0f);
        return;
    }

    // Sum of explicit fractions; panels without a size share the remainder.
    float explicit_sum = 0.0f;
    int auto_count = 0;
    for (int i = 0; i < n; ++i) {
        if (sizes_[i] > 0.0f) explicit_sum += sizes_[i];
        else ++auto_count;
    }
    const float auto_each =
        auto_count > 0 ? std::max(0.0f, (1.0f - explicit_sum) / auto_count)
                       : 0.0f;

    std::vector<float> px(n, 0.0f);
    float assigned = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float frac = sizes_[i] > 0.0f ? sizes_[i] : auto_each;
        const float min_frac =
            panels_[i].min_size > 0.0f
                ? panels_[i].min_size / total
                : 0.0f;
        px[i] = std::max(frac, min_frac);
        assigned += px[i];
    }
    // Normalize to fill (or shrink proportionally if over).
    if (assigned > 0.0001f) {
        for (auto& v : px) v /= assigned;
    } else {
        std::fill(px.begin(), px.end(), 1.0f / n);
    }
    sizes_ = px;
    NotifyLayout();
}

void Resizable::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    RecomputeSizes(bounds);
    handle_rects_.clear();

    const int n = static_cast<int>(panels_.size());
    if (n == 0) return;

    float cursor = dir_ == Direction::Horizontal ? bounds.x : bounds.y;
    const float span =
        dir_ == Direction::Horizontal ? bounds.width : bounds.height;
    const float usable = span - handle_size_ * static_cast<float>(n - 1);

    for (int i = 0; i < n; ++i) {
        const float px = usable * sizes_[i];
        Layout::Rect r;
        if (dir_ == Direction::Horizontal) {
            r = {cursor, bounds.y, px, bounds.height};
        } else {
            r = {bounds.x, cursor, bounds.width, px};
        }
        panels_[i].widget->Layout(r, CachedLayoutTokens());
        panels_[i].widget->SetBounds(r);
        cursor += px;
        if (i + 1 < n) {
            Layout::Rect hr;
            if (dir_ == Direction::Horizontal) {
                hr = {cursor, bounds.y, handle_size_, bounds.height};
            } else {
                hr = {bounds.x, cursor, bounds.width, handle_size_};
            }
            handle_rects_.push_back(hr);
            cursor += handle_size_;
        }
    }
    rendered_ = true;
}

void Resizable::Render(Rendering::Renderer& renderer,
                       const Theme::Tokens& tokens) {
    // Children render themselves via RenderSubtree; we only draw handles.
    for (const auto& hr : handle_rects_) {
        renderer.FillRect(hr, tokens.border, tokens.radius_small);
    }
    rendered_ = true;
}

void Resizable::Tick(double) {
    // Sizes are event-driven; nothing to animate.
}

float Resizable::HandleHit(const Layout::Rect& bounds,
                           const Layout::Point& p) const {
    (void)bounds;
    const float pad = 3.0f;
    for (size_t i = 0; i < handle_rects_.size(); ++i) {
        const auto& hr = handle_rects_[i];
        const float x0 = dir_ == Direction::Horizontal ? hr.x - pad : hr.x;
        const float y0 = dir_ == Direction::Horizontal ? hr.y : hr.y - pad;
        const float w = dir_ == Direction::Horizontal ? hr.width + pad * 2
                                                      : hr.width;
        const float h = dir_ == Direction::Horizontal ? hr.height
                                                      : hr.height + pad * 2;
        if (p.x >= x0 && p.x < x0 + w && p.y >= y0 && p.y < y0 + h) {
            return static_cast<float>(i);
        }
    }
    return -1.0f;
}

bool Resizable::HandleEvent(const Event& event) {
    const auto& p = event.position;
    switch (event.type) {
        case Event::Type::MouseDown:
            if (event.mouse_button == MouseButton::Left) {
                const float hit = HandleHit(GetBounds(), p);
                if (hit >= 0.0f) {
                    drag_handle_ = static_cast<int>(hit);
                    drag_start_ =
                        dir_ == Direction::Horizontal ? p.x : p.y;
                    drag_start_frac_ = sizes_[drag_handle_];
                    return true;
                }
            }
            return false;
        case Event::Type::MouseMove:
            if (drag_handle_ >= 0) {
                const float cur =
                    dir_ == Direction::Horizontal ? p.x : p.y;
                const float delta = cur - drag_start_;
                const float span =
                    dir_ == Direction::Horizontal
                        ? GetBounds().width
                        : GetBounds().height;
                const float dfrac = delta / std::max(1.0f, span);
                const int left = drag_handle_;
                const int right = drag_handle_ + 1;
                if (left >= 0 && right < static_cast<int>(sizes_.size())) {
                    // Steal/give fraction between the two adjacent panels,
                    // keeping every other panel's share intact.
                    const float others =
                        std::accumulate(sizes_.begin(), sizes_.end(), 0.0f) -
                        sizes_[left] - sizes_[right];
                    const float avail = 1.0f - others;
                    const float floor_l =
                        std::min(0.05f, panels_[left].min_size > 0.0f
                                              ? panels_[left].min_size /
                                                    std::max(1.0f, span)
                                              : 0.05f);
                    const float floor_r =
                        std::min(0.05f, panels_[right].min_size > 0.0f
                                               ? panels_[right].min_size /
                                                     std::max(1.0f, span)
                                               : 0.05f);
                    float nl = drag_start_frac_ + dfrac;
                    nl = std::clamp(nl, floor_l, avail - floor_r);
                    sizes_[left] = nl;
                    sizes_[right] = avail - nl;
                    RequestLayout();
                }
                return true;
            }
            return false;
        case Event::Type::MouseUp:
            if (drag_handle_ >= 0) {
                drag_handle_ = -1;
                NotifyLayout();
                return true;
            }
            return false;
        default:
            return false;
    }
}

Layout::Size Resizable::GetMinSize() const {
    return {80.0f, 60.0f};
}

Layout::Size Resizable::GetPreferredSize() const {
    return {480.0f, 320.0f};
}

Layout::Size Resizable::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
