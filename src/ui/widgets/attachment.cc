#include "attachment.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

std::size_t Utf8Length(std::string_view text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0u) != 0x80u) ++count;
    }
    return count;
}

std::string ExtensionOf(const std::string& name) {
    const auto pos = name.find_last_of('.');
    if (pos == std::string::npos || pos + 1 >= name.size()) return {};
    std::string ext = name.substr(pos + 1);
    for (auto& c : ext) c = static_cast<char>(std::toupper(
        static_cast<unsigned char>(c)));
    return ext;
}

} // namespace

Attachment::Attachment(std::string filename, std::string meta)
    : filename_(std::move(filename)), meta_(std::move(meta)) {
    SetFocusable(false);
}

void Attachment::SetFilename(std::string filename) {
    if (filename_ == filename) return;
    filename_ = std::move(filename);
    Invalidate();
    RequestLayout();
}

void Attachment::SetMeta(std::string meta) {
    if (meta_ == meta) return;
    meta_ = std::move(meta);
    Invalidate();
    RequestLayout();
}

void Attachment::SetOnRemove(std::function<void()> callback) {
    on_remove_ = std::move(callback);
}

Layout::Rect Attachment::RemoveRect(const Layout::Rect& bounds) const {
    const float size = 18.0f;
    Layout::Rect r;
    r.x = bounds.x + bounds.width - size - 6.0f;
    r.y = bounds.y + (bounds.height - size) * 0.5f;
    r.width = size;
    r.height = size;
    return r;
}

void Attachment::Render(Rendering::Renderer& renderer,
                        const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    // Card.
    renderer.FillRect(bounds, tokens.surface, tokens.radius_medium);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_medium);

    const float pad = 6.0f;
    const float icon_size = bounds.height - pad * 2.0f;

    // Type tile with extension label.
    Layout::Rect tile;
    tile.x = bounds.x + pad;
    tile.y = bounds.y + pad;
    tile.width = icon_size;
    tile.height = icon_size;
    renderer.FillRect(tile, tokens.muted, tokens.radius_small);
    const std::string ext = ExtensionOf(filename_);
    if (!ext.empty()) {
        const Rendering::TextStyle style{tokens.foreground,
                                         tokens.font_small * 0.7f, 600};
        const auto ts = renderer.MeasureText(ext, style);
        Layout::Rect tr;
        tr.x = tile.x + (tile.width - ts.width) * 0.5f;
        tr.y = tile.y + (tile.height - ts.height) * 0.5f;
        tr.width = ts.width;
        tr.height = ts.height;
        renderer.DrawText(ext, tr, style);
    }

    // Filename (+ meta on second line when present).
    const float text_x = tile.x + tile.width + 8.0f;
    const float avail_w =
        (remove_rect_.width > 0.0f ? remove_rect_.x : bounds.x + bounds.width)
        - text_x - 6.0f;

    const Rendering::TextStyle name_style{tokens.foreground,
                                          tokens.font_small, 500};
    const Rendering::TextStyle meta_style{tokens.muted, tokens.font_small * 0.85f,
                                          400};

    float ty = bounds.y + pad;
    if (!meta_.empty()) {
        // Two-line layout: name on top, meta below.
        Layout::Rect name_rect{text_x, ty, avail_w, tokens.font_small * 1.4f};
        renderer.DrawText(filename_, name_rect, name_style);
        ty += tokens.font_small * 1.4f;
        Layout::Rect meta_rect{text_x, ty, avail_w,
                               tokens.font_small * 1.2f};
        renderer.DrawText(meta_, meta_rect, meta_style);
    } else {
        Layout::Rect name_rect{text_x,
                               bounds.y + (bounds.height - tokens.font_small * 1.4f) * 0.5f,
                               avail_w, tokens.font_small * 1.4f};
        renderer.DrawText(filename_, name_rect, name_style);
    }

    // Remove button.
    if (on_remove_) {
        const auto rr = remove_rect_;
        const Theme::Color bg =
            remove_hovered_ ? tokens.danger : tokens.muted;
        renderer.FillRect(rr, bg, tokens.radius_small);
        const float arm = 4.0f;
        const float cx = rr.x + rr.width * 0.5f;
        const float cy = rr.y + rr.height * 0.5f;
        const Theme::Color fg =
            remove_hovered_ ? tokens.danger_foreground : tokens.foreground;
        renderer.DrawLine({cx - arm, cy - arm}, {cx + arm, cy + arm}, fg, 1.2f);
        renderer.DrawLine({cx + arm, cy - arm}, {cx - arm, cy + arm}, fg, 1.2f);
    }
}

void Attachment::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    remove_rect_ = RemoveRect(bounds);
}

bool Attachment::HandleEvent(const Event& event) {
    if (!on_remove_ || remove_rect_.width <= 0.0f) return false;
    const auto& p = event.position;
    const bool inside = p.x >= remove_rect_.x && p.x < remove_rect_.x + remove_rect_.width &&
                        p.y >= remove_rect_.y && p.y < remove_rect_.y + remove_rect_.height;
    switch (event.type) {
        case Event::Type::MouseMove:
            if (inside != remove_hovered_) {
                remove_hovered_ = inside;
                Invalidate();
            }
            return inside;
        case Event::Type::Click:
            if (inside && event.mouse_button == MouseButton::Left) {
                on_remove_();
                return true;
            }
            return false;
        default:
            return false;
    }
}

Layout::Size Attachment::GetMinSize() const {
    return {120.0f, 44.0f};
}

Layout::Size Attachment::GetPreferredSize() const {
    const float name_w = static_cast<float>(Utf8Length(filename_)) * 7.0f;
    const float meta_w =
        meta_.empty() ? 0.0f
                      : static_cast<float>(Utf8Length(meta_)) * 6.0f;
    const float text_w = std::max(name_w, meta_w);
    const float w = 12.0f + 32.0f + 8.0f + text_w + 28.0f;
    const float h = meta_.empty() ? 44.0f : 56.0f;
    return {std::max(120.0f, w), h};
}

Layout::Size Attachment::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
