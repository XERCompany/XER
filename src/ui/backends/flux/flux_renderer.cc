#include "flux_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "flux/terminal/terminal_colors.h"
#include "flux/terminal/terminal_styles.h"

namespace XER::UI::Backends {
namespace {

struct Rgb {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

constexpr std::array<Rgb, 16> kTerminalPalette{{
    {0, 0, 0},
    {170, 0, 0},
    {0, 170, 0},
    {170, 85, 0},
    {0, 0, 170},
    {170, 0, 170},
    {0, 170, 170},
    {170, 170, 170},
    {85, 85, 85},
    {255, 85, 85},
    {85, 255, 85},
    {255, 255, 85},
    {85, 85, 255},
    {255, 85, 255},
    {85, 255, 255},
    {255, 255, 255},
}};

std::uint8_t ToByte(float value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

XER::Flux::Terminal::TerminalColor ToTerminalColor(Theme::Color color) {
    const int red = ToByte(color.r);
    const int green = ToByte(color.g);
    const int blue = ToByte(color.b);

    // Hue comes from the 8 base ANSI colors; a separate vivid/near-white check
    // upgrades the result to the matching bright variant (index + 8). Pure
    // primaries like {1,0,0} are mathematically closer to dark red than to
    // bright red in RGB space, so nearest-palette alone never selects brights.
    std::size_t hue = 0;
    int best_distance = std::numeric_limits<int>::max();
    for (std::size_t index = 0; index < 8; ++index) {
        const Rgb candidate = kTerminalPalette[index];
        const int red_delta = red - candidate.red;
        const int green_delta = green - candidate.green;
        const int blue_delta = blue - candidate.blue;
        const int distance =
            red_delta * red_delta + green_delta * green_delta + blue_delta * blue_delta;
        if (distance < best_distance) {
            best_distance = distance;
            hue = index;
        }
    }

    const int max_channel = std::max({red, green, blue});
    const int min_channel = std::min({red, green, blue});
    const bool vivid = max_channel >= 200 && (max_channel - min_channel) >= 140;
    const bool near_white = red >= 180 && green >= 180 && blue >= 180;
    const int index = static_cast<int>(hue) + ((vivid || near_white) ? 8 : 0);
    return static_cast<XER::Flux::Terminal::TerminalColor>(index);
}

struct CellRect {
    std::size_t left = 0;
    std::size_t top = 0;
    std::size_t right = 0;
    std::size_t bottom = 0;

    bool empty() const {
        return left >= right || top >= bottom;
    }
};

CellRect ToCellRect(const Layout::Rect& rect, std::size_t width, std::size_t height) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
        rect.width <= 0.0f || rect.height <= 0.0f) {
        return {};
    }

    const double left = std::floor(static_cast<double>(rect.x));
    const double top = std::floor(static_cast<double>(rect.y));
    const double right = std::ceil(static_cast<double>(rect.x) + rect.width);
    const double bottom = std::ceil(static_cast<double>(rect.y) + rect.height);
    const double clipped_left = std::clamp(left, 0.0, static_cast<double>(width));
    const double clipped_top = std::clamp(top, 0.0, static_cast<double>(height));
    const double clipped_right = std::clamp(right, 0.0, static_cast<double>(width));
    const double clipped_bottom = std::clamp(bottom, 0.0, static_cast<double>(height));

    return {
        static_cast<std::size_t>(clipped_left),
        static_cast<std::size_t>(clipped_top),
        static_cast<std::size_t>(clipped_right),
        static_cast<std::size_t>(clipped_bottom),
    };
}

std::size_t Utf8SequenceLength(std::string_view text, std::size_t index) {
    const auto byte = static_cast<unsigned char>(text[index]);
    std::size_t length = 0;
    if (byte >= 0xC2u && byte <= 0xDFu) {
        length = 2;
    } else if (byte >= 0xE0u && byte <= 0xEFu) {
        length = 3;
    } else if (byte >= 0xF0u && byte <= 0xF4u) {
        length = 4;
    } else {
        return 1;
    }

    if (index + length > text.size()) {
        return 1;
    }
    for (std::size_t offset = 1; offset < length; ++offset) {
        const auto continuation = static_cast<unsigned char>(text[index + offset]);
        if (continuation < 0x80u || continuation > 0xBFu) {
            return 1;
        }
    }

    const auto second = static_cast<unsigned char>(text[index + 1]);
    if ((byte == 0xE0u && second < 0xA0u) ||
        (byte == 0xEDu && second >= 0xA0u) ||
        (byte == 0xF0u && second < 0x90u) ||
        (byte == 0xF4u && second > 0x8Fu)) {
        return 1;
    }
    return length;
}

char AsciiFallback(std::string_view text, std::size_t index) {
    const auto byte = static_cast<unsigned char>(text[index]);
    return byte >= 0x20u && byte <= 0x7Eu ? static_cast<char>(byte) : '?';
}

std::size_t CellColumnCount(std::string_view text, std::size_t& line_count) {
    std::size_t column = 0;
    std::size_t maximum = 0;
    line_count = 1;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '\n') {
            maximum = std::max(maximum, column);
            column = 0;
            ++line_count;
            ++index;
            continue;
        }
        ++column;
        index += Utf8SequenceLength(text, index);
    }
    return std::max(maximum, column);
}

void SetForegroundStyle(XER::Flux::Terminal::TerminalStyle& terminal_style, Theme::Color color, int weight) {
    terminal_style.foreground = ToTerminalColor(color);
    terminal_style.bold = weight >= 600;
}

}  // namespace

FluxRenderer::FluxRenderer(XER::Flux::Rendering::RenderContext& context)
    : context_(context) {}

void FluxRenderer::BeginFrame(Layout::Rect) {
    context_.Clear();
}

void FluxRenderer::EndFrame() {}

void FluxRenderer::PushClip(Layout::Rect rect) {
    clip_stack_.push_back(rect);
}

void FluxRenderer::PopClip() {
    if (!clip_stack_.empty()) {
        clip_stack_.pop_back();
    }
}

std::optional<Layout::Rect> FluxRenderer::EffectiveClip() const {
    if (clip_stack_.empty()) {
        return std::nullopt;
    }
    auto clip = clip_stack_.front();
    for (std::size_t i = 1; i < clip_stack_.size(); ++i) {
        clip = clip.Intersect(clip_stack_[i]);
    }
    return clip;
}

Layout::Rect FluxRenderer::IntersectClip(Layout::Rect rect) const {
    const auto clip = EffectiveClip();
    if (!clip.has_value()) {
        return rect;
    }
    return rect.Intersect(*clip);
}

void FluxRenderer::FillRect(Layout::Rect rect, Theme::Color color, float) {
    rect = IntersectClip(rect);
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    XER::Flux::Terminal::TerminalStyle style;
    style.background = ToTerminalColor(color);
    const CellRect cells = ToCellRect(rect, context_.frame().width(), context_.frame().height());
    for (std::size_t y = cells.top; y < cells.bottom; ++y) {
        for (std::size_t x = cells.left; x < cells.right; ++x) {
            context_.frame().SetCell(x, y, ' ', style);
        }
    }
}

void FluxRenderer::StrokeRect(Layout::Rect rect, Theme::Color color, float, float) {
    rect = IntersectClip(rect);
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    const CellRect cells = ToCellRect(rect, context_.frame().width(), context_.frame().height());
    if (cells.empty()) {
        return;
    }

    XER::Flux::Terminal::TerminalStyle style;
    style.foreground = ToTerminalColor(color);
    const std::size_t right = cells.right - 1;
    const std::size_t bottom = cells.bottom - 1;
    for (std::size_t x = cells.left; x <= right; ++x) {
        context_.frame().SetCell(x, cells.top, x == cells.left || x == right ? '+' : '-', style);
        if (bottom != cells.top) {
            context_.frame().SetCell(x, bottom, x == cells.left || x == right ? '+' : '-', style);
        }
    }
    for (std::size_t y = cells.top + 1; y < bottom; ++y) {
        context_.frame().SetCell(cells.left, y, '|', style);
        if (right != cells.left) {
            context_.frame().SetCell(right, y, '|', style);
        }
    }
}

void FluxRenderer::DrawLine(
    Layout::Point start,
    Layout::Point end,
    Theme::Color color,
    float
) {
    const std::size_t width = context_.frame().width();
    const std::size_t height = context_.frame().height();
    if (width == 0 || height == 0 ||
        !std::isfinite(start.x) || !std::isfinite(start.y) ||
        !std::isfinite(end.x) || !std::isfinite(end.y)) {
        return;
    }

    const double maximum_x = static_cast<double>(
        std::min(width - 1, static_cast<std::size_t>(std::numeric_limits<int>::max()))
    );
    const double maximum_y = static_cast<double>(
        std::min(height - 1, static_cast<std::size_t>(std::numeric_limits<int>::max()))
    );
    double x0 = start.x;
    double y0 = start.y;
    double x1 = end.x;
    double y1 = end.y;
    const double segment_dx = x1 - x0;
    const double segment_dy = y1 - y0;
    double first = 0.0;
    double last = 1.0;

    const auto clip_test = [&first, &last](double p, double q) {
        if (p == 0.0) {
            return q >= 0.0;
        }
        const double ratio = q / p;
        if (p < 0.0) {
            if (ratio > last) {
                return false;
            }
            first = std::max(first, ratio);
        } else {
            if (ratio < first) {
                return false;
            }
            last = std::min(last, ratio);
        }
        return true;
    };

    if (!clip_test(-segment_dx, x0) ||
        !clip_test(segment_dx, maximum_x - x0) ||
        !clip_test(-segment_dy, y0) ||
        !clip_test(segment_dy, maximum_y - y0) ||
        first > last) {
        return;
    }

    const double clipped_x0 = x0 + first * segment_dx;
    const double clipped_y0 = y0 + first * segment_dy;
    const double clipped_x1 = x0 + last * segment_dx;
    const double clipped_y1 = y0 + last * segment_dy;
    const auto to_coordinate = [](double value, double maximum) {
        return static_cast<int>(std::clamp(std::round(value), 0.0, maximum));
    };
    int x = to_coordinate(clipped_x0, maximum_x);
    int y = to_coordinate(clipped_y0, maximum_y);
    const int end_x = to_coordinate(clipped_x1, maximum_x);
    const int end_y = to_coordinate(clipped_y1, maximum_y);
    const int origin_x = x;
    const int origin_y = y;
    const int delta_x = std::abs(end_x - x);
    const int step_x = x < end_x ? 1 : -1;
    const int delta_y = -std::abs(end_y - y);
    const int step_y = y < end_y ? 1 : -1;
    int error = delta_x + delta_y;

    XER::Flux::Terminal::TerminalStyle style;
    style.foreground = ToTerminalColor(color);
    const auto clip = EffectiveClip();
    while (true) {
        const int current_x = x;
        const int current_y = y;
        const bool in_clip = !clip.has_value() ||
            (current_x >= clip->x && current_x < clip->x + clip->width &&
             current_y >= clip->y && current_y < clip->y + clip->height);
        if (in_clip) {
            char glyph = '-';
            if (origin_x == end_x) {
                glyph = '|';
            } else if (origin_y != end_y) {
                const bool positive_slope =
                    (end_x > origin_x) == (end_y > origin_y);
                glyph = positive_slope ? '\\' : '/';
            }
            context_.frame().SetCell(
                static_cast<std::size_t>(current_x),
                static_cast<std::size_t>(current_y),
                glyph,
                style
            );
        }
        if (x == end_x && y == end_y) {
            break;
        }
        const std::int64_t doubled_error = 2 * static_cast<std::int64_t>(error);
        if (doubled_error >= delta_y) {
            error += delta_y;
            x += step_x;
        }
        if (doubled_error <= delta_x) {
            error += delta_x;
            y += step_y;
        }
    }
}

void FluxRenderer::DrawText(
    std::string_view text,
    Layout::Rect rect,
    Rendering::TextStyle style
) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
        rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }

    XER::Flux::Terminal::TerminalStyle terminal_style;
    SetForegroundStyle(terminal_style, style.color, style.weight);
    const double origin_x = std::floor(static_cast<double>(rect.x));
    const double origin_y = std::floor(static_cast<double>(rect.y));
    const double right = std::ceil(static_cast<double>(rect.x) + rect.width);
    const double bottom = std::ceil(static_cast<double>(rect.y) + rect.height);
    const std::size_t frame_width = context_.frame().width();
    const std::size_t frame_height = context_.frame().height();
    const auto clip = EffectiveClip();
    const double clip_right = clip.has_value() ? clip->x + clip->width
                                               : static_cast<double>(frame_width);
    const double clip_bottom = clip.has_value() ? clip->y + clip->height
                                                : static_cast<double>(frame_height);
    const double clip_left = clip.has_value() ? clip->x : 0.0;
    const double clip_top = clip.has_value() ? clip->y : 0.0;
    double row = 0.0;
    double column = 0.0;

    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '\n') {
            row += 1.0;
            column = 0.0;
            ++index;
            continue;
        }

        const double cell_x = origin_x + column;
        const double cell_y = origin_y + row;
        if (cell_x >= right || cell_y >= bottom ||
            cell_x >= clip_right || cell_y >= clip_bottom) {
            if (cell_y >= bottom || cell_y >= clip_bottom) {
                break;
            }
            ++column;
            index += Utf8SequenceLength(text, index);
            continue;
        }
        if (cell_x >= clip_left && cell_y >= clip_top &&
            cell_x >= 0.0 && cell_y >= 0.0 &&
            cell_x < static_cast<double>(frame_width) &&
            cell_y < static_cast<double>(frame_height)) {
            context_.frame().SetCell(
                static_cast<std::size_t>(cell_x),
                static_cast<std::size_t>(cell_y),
                AsciiFallback(text, index),
                terminal_style
            );
        }
        ++column;
        index += Utf8SequenceLength(text, index);
    }
}

Layout::Size FluxRenderer::MeasureText(std::string_view text, Rendering::TextStyle) const {
    std::size_t line_count = 0;
    const std::size_t columns = CellColumnCount(text, line_count);
    return {
        static_cast<float>(columns),
        static_cast<float>(line_count),
    };
}

}  // namespace XER::UI::Backends
