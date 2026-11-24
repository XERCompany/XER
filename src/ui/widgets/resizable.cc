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
    collapsed_.push_back(false);
    // Grow the share vector without disturbing the fractions already
    // stored for earlier panels — assign(n, 0) used to wipe them and
    // every earlier panel silently fell into the auto pool.
    if (sizes_.size() < panels_.size()) {
        sizes_.resize(panels_.size(), 0.0f);
    }
    sizes_.back() = panels_.back().size;
    RequestLayout();
}

void Resizable::ClearPanels() {
    panels_.clear();
    sizes_.clear();
    collapsed_.clear();
    handle_rects_.clear();
    handle_panels_.clear();
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
    const auto i = std::size_t(index);
    // Already in the requested state: nothing to stash/restore. The
    // restore path below rescales the OTHER panels' shares — running
    // it on a never-collapsed panel corrupts the split (it used to
    // multiply siblings by (1-restored)/others on every shell sync).
    if (collapsed == collapsed_[i]) return;
    if (collapsed) {
        // Stash the resolved share so expanding restores the dragged size,
        // not just the originally configured fraction.
        panels_[i].size = sizes_[i];
        collapsed_[i] = true;
    } else {
        collapsed_[i] = false;
        // Give the panel its stashed share back and rescale the remaining
        // panels into the rest — the other panels were normalized to fill
        // the whole span while this one was collapsed.
        float others = 0.0f;
        for (std::size_t j = 0; j < sizes_.size(); ++j) {
            if (j != i && !collapsed_[j]) others += sizes_[j];
        }
        const float restored = panels_[i].size;
        if (others > 0.0001f) {
            const float scale = std::max(0.0f, 1.0f - restored) / others;
            for (std::size_t j = 0; j < sizes_.size(); ++j) {
                if (j != i && !collapsed_[j]) sizes_[j] *= scale;
            }
        }
        sizes_[i] = restored;
    }
    RequestLayout();
}

bool Resizable::IsCollapsed(int index) const {
    return index >= 0 && index < static_cast<int>(collapsed_.size()) &&
           collapsed_[static_cast<std::size_t>(index)];
}

void Resizable::NotifyLayout() {
    if (on_layout_) on_layout_(sizes_);
}

void Resizable::RecomputeSizes(const Layout::Rect& bounds) {
    const int n = static_cast<int>(panels_.size());
    if (n == 0) return;
    int active = 0;
    for (int i = 0; i < n; ++i) {
        if (!collapsed_[static_cast<std::size_t>(i)]) ++active;
    }
    const float total =
        (dir_ == Direction::Horizontal ? bounds.width : bounds.height) -
        handle_size_ * static_cast<float>(active > 0 ? active - 1 : 0);
    if (total <= 0.0f) {
        std::fill(sizes_.begin(), sizes_.end(), 0.0f);
        return;
    }

    // Sum of explicit fractions; panels without a size share the remainder.
    // Collapsed panels contribute nothing and never join the auto pool.
    float explicit_sum = 0.0f;
    int auto_count = 0;
    int active_count = 0;
    for (int i = 0; i < n; ++i) {
        if (collapsed_[static_cast<std::size_t>(i)]) continue;
        ++active_count;
        if (sizes_[i] > 0.0f) explicit_sum += sizes_[i];
        else ++auto_count;
    }
    const float auto_each =
        auto_count > 0 ? std::max(0.0f, (1.0f - explicit_sum) / auto_count)
                       : 0.0f;

    // Min floors are real guarantees, not hints: normalize only the
    // amount each panel holds ABOVE its min so rescaling can never push
    // a panel back under its configured floor. When the floors alone
    // oversubscribe the span they rescale among themselves — there is
    // no honest layout otherwise.
    std::vector<float> px(n, 0.0f);
    std::vector<float> floor_px(n, 0.0f);
    float floor_sum = 0.0f;
    for (int i = 0; i < n; ++i) {
        if (collapsed_[static_cast<std::size_t>(i)]) continue;
        const float frac = sizes_[i] > 0.0f ? sizes_[i] : auto_each;
        floor_px[i] =
            panels_[i].min_size > 0.0f
                ? std::min(1.0f, panels_[i].min_size / total)
                : 0.0f;
        px[i] = std::max(frac, floor_px[i]);
        floor_sum += floor_px[i];
    }
    if (floor_sum >= 1.0f && floor_sum > 0.0f) {
        for (int i = 0; i < n; ++i) {
            if (!collapsed_[static_cast<std::size_t>(i)]) {
                px[i] = floor_px[i] / floor_sum;
            }
        }
    } else {
        float excess_sum = 0.0f;
        for (int i = 0; i < n; ++i) {
            if (!collapsed_[static_cast<std::size_t>(i)]) {
                excess_sum += px[i] - floor_px[i];
            }
        }
        const float pool = 1.0f - floor_sum;
        if (excess_sum > 0.0001f) {
            const float scale = pool / excess_sum;
            for (int i = 0; i < n; ++i) {
                if (!collapsed_[static_cast<std::size_t>(i)]) {
                    px[i] = floor_px[i] +
                            (px[i] - floor_px[i]) * scale;
                }
            }
        } else if (active_count > 0) {
            // Every panel pinned at its floor — share the leftover
            // evenly so the strip still fills the span.
            const float rest = pool / static_cast<float>(active_count);
            for (int i = 0; i < n; ++i) {
                if (!collapsed_[static_cast<std::size_t>(i)]) {
                    px[i] = floor_px[i] + rest;
                }
            }
        }
    }
    sizes_ = px;
    NotifyLayout();
}

void Resizable::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    RecomputeSizes(bounds);
    handle_rects_.clear();
    handle_panels_.clear();

    // Children that are no longer registered panels — ClearPanels'd
    // and never re-added, or adopted without AddPanel — own no span.
    // Park them on an empty rect: without this an orphan keeps its
    // last bounds, keeps painting over the live panels (it's still a
    // child, so RenderSubtree draws it) and keeps winning hit-tests
    // (FindHitTarget walks children, not panels_). The ui2 shell hits
    // this on every workspace switch that swaps the center widget.
    for (const auto& child : GetChildren()) {
        bool is_panel = false;
        for (const auto& panel : panels_) {
            if (panel.widget.get() == child.get()) {
                is_panel = true;
                break;
            }
        }
        if (!is_panel) {
            child->Layout({}, CachedLayoutTokens());
            child->SetBounds({});
        }
    }

    const int n = static_cast<int>(panels_.size());
    if (n == 0) return;

    float cursor = dir_ == Direction::Horizontal ? bounds.x : bounds.y;
    const float span =
        dir_ == Direction::Horizontal ? bounds.width : bounds.height;
    int visible_count = 0;
    for (int i = 0; i < n; ++i) {
        if (!collapsed_[static_cast<std::size_t>(i)]) ++visible_count;
    }
    const float usable =
        std::max(0.0f, span - handle_size_ * static_cast<float>(
                                       visible_count > 0
                                           ? visible_count - 1
                                           : 0));

    for (int i = 0; i < n; ++i) {
        // Collapsed panels own no span — park them on an empty rect so
        // they neither paint over siblings nor keep a stale clickable
        // area (they used to lay out at their pre-collapse share, past
        // the end of the strip).
        if (collapsed_[static_cast<std::size_t>(i)]) {
            panels_[i].widget->Layout({}, CachedLayoutTokens());
            panels_[i].widget->SetBounds({});
            continue;
        }
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
        // A handle slot follows only when another VISIBLE panel still
        // comes after this one.
        bool more_visible = false;
        for (int j = i + 1; j < n; ++j) {
            if (!collapsed_[static_cast<std::size_t>(j)]) {
                more_visible = true;
                break;
            }
        }
        if (more_visible) {
            Layout::Rect hr;
            if (dir_ == Direction::Horizontal) {
                hr = {cursor, bounds.y, handle_size_, bounds.height};
            } else {
                hr = {bounds.x, cursor, bounds.width, handle_size_};
            }
            handle_rects_.push_back(hr);
            handle_panels_.push_back(i);
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
                if (hit >= 0.0f &&
                    hit < static_cast<float>(handle_panels_.size())) {
                    drag_handle_ = static_cast<int>(hit);
                    drag_start_ =
                        dir_ == Direction::Horizontal ? p.x : p.y;
                    drag_start_frac_ =
                        sizes_[handle_panels_[drag_handle_]];
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
                // Fractions apply to the usable span (minus handle strips);
                // dividing pixel deltas by the full span undershoots.
                const int n = static_cast<int>(sizes_.size());
                const float usable = std::max(
                    1.0f, span - handle_size_ * static_cast<float>(n - 1));
                const float dfrac = delta / usable;
                // The handle separates the panel it follows from the
                // next VISIBLE one — collapsed panels between them own
                // no span to redistribute.
                int left = -1;
                int right = -1;
                if (drag_handle_ >= 0 &&
                    drag_handle_ <
                        static_cast<int>(handle_panels_.size())) {
                    left = handle_panels_[drag_handle_];
                    for (int j = left + 1; j < n; ++j) {
                        if (!collapsed_[static_cast<std::size_t>(j)]) {
                            right = j;
                            break;
                        }
                    }
                }
                if (left >= 0 && right >= 0 && right < n) {
                    // Steal/give fraction between the two adjacent panels,
                    // keeping every other panel's share intact.
                    const float others =
                        std::accumulate(sizes_.begin(), sizes_.end(), 0.0f) -
                        sizes_[left] - sizes_[right];
                    const float avail = 1.0f - others;
                    // The floor IS the panel's configured min_size —
                    // the old 0.05 cap let drags crush docks down to 5%
                    // of the strip regardless of their content.
                    const float floor_l =
                        panels_[left].min_size > 0.0f
                            ? panels_[left].min_size / usable
                            : 0.05f;
                    const float floor_r =
                        panels_[right].min_size > 0.0f
                            ? panels_[right].min_size / usable
                            : 0.05f;
                    float nl = drag_start_frac_ + dfrac;
                    // Conflicting floors (mins oversubscribe `avail`)
                    // resolve to the left floor — never an inverted
                    // clamp range.
                    const float hi =
                        std::max(floor_l, avail - floor_r);
                    nl = std::clamp(nl, floor_l, hi);
                    sizes_[left] = nl;
                    sizes_[right] = avail - nl;
                    RequestLayout();
                }
                return true;
            }
            return false;
        case Event::Type::MouseUp:
            if (drag_handle_ >= 0 && event.mouse_button == MouseButton::Left) {
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
