#include "flex.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace XER::UI::Layout {

float Snap(float value, float scale) noexcept {
    if (!(scale > 0.0f) || !std::isfinite(value)) {
        return value;
    }
    return std::round(value * scale) / scale;
}

Rect SnapRect(const Rect& rect, float scale) noexcept {
    const float x0 = Snap(rect.x, scale);
    const float y0 = Snap(rect.y, scale);
    const float x1 = Snap(rect.Right(), scale);
    const float y1 = Snap(rect.Bottom(), scale);
    return Rect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

Rect CenteredRect(const Rect& container, const Size& size,
                  float h_padding, float v_padding) noexcept {
    const float avail_w = std::max(0.0f, container.width - 2.0f * h_padding);
    const float avail_h = std::max(0.0f, container.height - 2.0f * v_padding);
    const float w = std::min(size.width, avail_w);
    const float h = std::min(size.height, avail_h);
    const float x = container.x + h_padding + (avail_w - w) * 0.5f;
    const float y = container.y + v_padding + (avail_h - h) * 0.5f;
    return Rect{x, y, w, h};
}

Rect CenteredTextRect(Rendering::Renderer& renderer,
                      std::string_view text,
                      const Rendering::TextStyle& style,
                      const Rect& container,
                      float h_padding,
                      bool center_horizontal) {
    const float text_h = renderer.MeasureText(text, style).height;
    const float y = container.y +
        std::max(0.0f, (container.height - text_h) * 0.5f);
    if (center_horizontal && h_padding <= 0.0f) {
        const float text_w = renderer.MeasureText(text, style).width;
        const float x = container.x +
            std::max(0.0f, (container.width - text_w) * 0.5f);
        return Rect{x, y, text_w, text_h};
    }
    const float x = container.x + h_padding;
    const float w = std::max(0.0f, container.width - 2.0f * h_padding);
    return Rect{x, y, w, text_h};
}

Rect VerticallyCentered(const Rect& container, float element_height) noexcept {
    const float h = std::min(element_height, container.height);
    const float y = container.y + std::max(0.0f, (container.height - h) * 0.5f);
    return Rect{container.x, y, container.width, h};
}

std::vector<Rect> DistributeRow(
    const Rect& container,
    const std::vector<float>& base_widths,
    const std::vector<float>& grow_weights,
    float gap,
    FlexAlign align,
    float scale) {

    const std::size_t n = base_widths.size();
    if (n == 0 || container.width <= 0.0f) {
        return {};
    }

    const float container_x0 = Snap(container.x, scale);
    const float container_x1 = Snap(container.Right(), scale);
    const float container_w = std::max(0.0f, container_x1 - container_x0);
    const float y = Snap(container.y, scale);
    const float h = std::max(1.0f, Snap(container.height, scale));

    std::vector<float> widths(n, 0.0f);
    const float min_gap = std::max(0.0f, gap);
    const float total_gap = min_gap * static_cast<float>(n - 1);
    const float sum_base = std::accumulate(base_widths.begin(), base_widths.end(), 0.0f);

    if (sum_base + total_gap <= container_w + 0.01f) {
        // Wide container: distribute base widths + alignment slack.
        float extra = std::max(0.0f, container_w - total_gap - sum_base);
        float start_x = container_x0;

        if (align == FlexAlign::SpaceBetween && n > 1) {
            gap += extra / static_cast<float>(n - 1);
            extra = 0.0f;
        } else if (align == FlexAlign::SpaceAround) {
            const float side = extra / static_cast<float>(n);
            start_x += side;
            gap += 2.0f * side;
            extra = 0.0f;
        } else if (align == FlexAlign::SpaceEvenly) {
            const float step = extra / static_cast<float>(n + 1);
            start_x += step;
            gap += step;
            extra = 0.0f;
        } else if (align == FlexAlign::Center) {
            // Center the natural-size row (base widths + gaps).
            const float natural_span = sum_base + total_gap;
            start_x += std::max(0.0f, (container_w - natural_span) * 0.5f);
        } else if (align == FlexAlign::End) {
            const float natural_span = sum_base + total_gap;
            start_x += std::max(0.0f, container_w - natural_span);
        }

        // Extra space only grows items for Start alignment (fill the row).
        // Center/End keep natural widths so the offset computed above actually
        // centers/right-aligns the row; Space* modes consumed `extra` already.
        if (extra > 0.0f && align == FlexAlign::Start) {
            float weight_sum = 0.0f;
            for (std::size_t i = 0; i < n; ++i) {
                weight_sum += (i < grow_weights.size() ? grow_weights[i] : 1.0f);
            }
            if (weight_sum <= 0.0f) weight_sum = static_cast<float>(n);
            for (std::size_t i = 0; i < n; ++i) {
                const float w = (i < grow_weights.size() ? grow_weights[i] : 1.0f);
                widths[i] = base_widths[i] + extra * (w / weight_sum);
            }
        } else {
            widths = base_widths;
        }

        std::vector<Rect> result;
        result.reserve(n);
        float cur_x = start_x;
        for (std::size_t i = 0; i < n; ++i) {
            const float x0 = Snap(cur_x, scale);
            // Uniform right edge: width plus the inter-item gap (none after
            // the last item). Rows that don't fill the container (Center,
            // SpaceAround, ...) keep their natural trailing space.
            const float right = Snap(
                cur_x + widths[i] + (i + 1 == n ? 0.0f : gap), scale);
            const float item_w = std::max(0.0f, right - x0);
            result.push_back(Rect{x0, y, item_w, h});
            cur_x = cur_x + widths[i] + gap;
        }
        return result;
    }

    // Narrow container: shrink proportionally without overlap or negative widths.
    std::vector<float> min_widths(n);
    float min_sum = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        min_widths[i] = std::max(8.0f, base_widths[i] * 0.28f);
        min_sum += min_widths[i];
    }

    float eff_gap = min_gap;
    if (min_sum + eff_gap * static_cast<float>(n - 1) > container_w) {
        eff_gap = (n > 1)
            ? std::max(0.0f, (container_w - min_sum) / static_cast<float>(n - 1))
            : 0.0f;
    }

    const float avail_for_items = std::max(0.0f,
        container_w - eff_gap * static_cast<float>(n - 1));
    float assigned = 0.0f;
    std::vector<float> widths_shrunk(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        if (i + 1 == n) {
            widths_shrunk[i] = std::max(min_widths[i],
                avail_for_items - assigned);
        } else {
            const float frac = (sum_base > 0.0f)
                ? (base_widths[i] / sum_base)
                : (1.0f / static_cast<float>(n));
            widths_shrunk[i] = std::max(min_widths[i],
                avail_for_items * frac);
            assigned += widths_shrunk[i];
        }
    }

    // Normalize so sum(widths_shrunk) == avail_for_items exactly.
    float shrunk_sum = std::accumulate(widths_shrunk.begin(), widths_shrunk.end(), 0.0f);
    if (shrunk_sum > 0.0f && std::abs(shrunk_sum - avail_for_items) > 0.05f) {
        const float k = avail_for_items / shrunk_sum;
        for (std::size_t i = 0; i < n; ++i) {
            widths_shrunk[i] = std::max(min_widths[i], widths_shrunk[i] * k);
        }
    }

    std::vector<Rect> result;
    result.reserve(n);
    float cur_x = container_x0;
    for (std::size_t i = 0; i < n; ++i) {
        const float x0 = Snap(cur_x, scale);
        const float next_x = (i + 1 == n)
            ? container_x1
            : Snap(cur_x + widths_shrunk[i] + eff_gap, scale);
        const float w = std::max(0.0f, next_x - x0);
        result.push_back(Rect{x0, y, w, h});
        cur_x += widths_shrunk[i] + eff_gap;
    }
    return result;
}

} // namespace XER::UI::Layout
