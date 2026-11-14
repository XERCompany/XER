#include "message.h"

#include <algorithm>
#include <cmath>
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

} // namespace

Message::Message(std::string sender, std::string text, std::string time,
                 Bubble::Align align)
    : sender_(std::move(sender)),
      text_(std::move(text)),
      time_(std::move(time)),
      align_(align) {
    SetFocusable(false);
}

void Message::SetSender(std::string sender) {
    if (sender_ == sender) return;
    sender_ = std::move(sender);
    Invalidate();
    RequestLayout();
}

void Message::SetText(std::string text) {
    if (text_ == text) return;
    text_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Message::SetTime(std::string time) {
    if (time_ == time) return;
    time_ = std::move(time);
    Invalidate();
}

void Message::SetAlign(Bubble::Align align) {
    if (align_ == align) return;
    align_ = align;
    Invalidate();
}

void Message::Render(Rendering::Renderer& renderer,
                     const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    const bool own = align_ == Bubble::Align::Own;
    const float avatar_d = 28.0f;
    const float gap = 8.0f;

    // Avatar circle with sender initial (left side always — chat UIs keep
    // avatars on the left even for own messages in many designs; for Own
    // we mirror the whole row instead).
    Layout::Rect avatar;
    avatar.width = avatar_d;
    avatar.height = avatar_d;
    avatar.y = bounds.y;
    if (own) {
        avatar.x = bounds.x + bounds.width - avatar_d;
    } else {
        avatar.x = bounds.x;
    }
    renderer.FillRect(avatar, tokens.muted, avatar_d * 0.5f);
    if (!sender_.empty()) {
        // First UTF-8 codepoint (1-4 bytes) for the avatar initial.
        const unsigned char lead =
            static_cast<unsigned char>(sender_.front());
        const std::size_t len =
            lead < 0x80 ? 1 : (lead < 0xE0 ? 2 : (lead < 0xF0 ? 3 : 4));
        const std::string initial = sender_.substr(
            0, std::min(len, sender_.size()));
        const Rendering::TextStyle style{tokens.foreground,
                                         tokens.font_small, 600};
        const auto ts = renderer.MeasureText(initial, style);
        Layout::Rect tr;
        tr.x = avatar.x + (avatar_d - ts.width) * 0.5f;
        tr.y = avatar.y + (avatar_d - ts.height) * 0.5f;
        tr.width = ts.width;
        tr.height = ts.height;
        renderer.DrawText(initial, tr, style);
    }

    // Meta line: sender name + time, aligned to the bubble side.
    const float meta_h = tokens.font_small * 1.3f;
    const float text_x = own ? bounds.x + 8.0f : avatar.x + avatar_d + gap;
    const float text_w =
        (own ? avatar.x - gap : bounds.x + bounds.width) - text_x - 4.0f;

    if (!sender_.empty() || !time_.empty()) {
        const Rendering::TextStyle name_style{tokens.muted,
                                              tokens.font_small * 0.85f, 400};
        std::string meta;
        if (!sender_.empty()) meta += sender_;
        if (!time_.empty()) {
            if (!meta.empty()) meta += "  ·  ";
            meta += time_;
        }
        Layout::Rect meta_rect{text_x, bounds.y + avatar_d + 2.0f,
                               std::max(0.0f, text_w), meta_h};
        renderer.DrawText(meta, meta_rect, name_style);
    }

    // Bubble body below the meta line.
    const float bubble_y = bounds.y + avatar_d + 2.0f + meta_h + 4.0f;
    const float bubble_h = std::max(0.0f, bounds.y + bounds.height - bubble_y);
    const float bubble_max_w = std::max(0.0f, text_w);

    // Approximate bubble width from text length (~7px/char, capped).
    const float text_px =
        static_cast<float>(Utf8Length(text_)) * 7.0f + 24.0f;
    const float bubble_w = std::min(bubble_max_w, std::max(60.0f, text_px));

    Layout::Rect bubble;
    bubble.y = bubble_y;
    bubble.height = bubble_h;
    bubble.width = bubble_w;
    if (own) {
        bubble.x = text_x + std::max(0.0f, bubble_max_w - bubble_w);
    } else {
        bubble.x = text_x;
    }

    const bool own_bubble = own;
    const Theme::Color fill =
        own_bubble ? tokens.accent : tokens.surface;
    const Theme::Color stroke =
        own_bubble ? tokens.accent : tokens.border;
    const Theme::Color text_color =
        own_bubble ? tokens.accent_foreground : tokens.foreground;

    const float radius = tokens.radius_large;
    renderer.FillRect(bubble, fill, radius);
    renderer.StrokeRect(bubble, stroke, 1.0f, radius);

    const float pad_x = 12.0f;
    const float pad_y = 8.0f;
    const Rendering::TextStyle style{text_color, tokens.font_body, 400};
    Layout::Rect text_rect;
    text_rect.x = bubble.x + pad_x;
    text_rect.y = bubble.y + pad_y;
    text_rect.width = std::max(0.0f, bubble.width - pad_x * 2.0f);
    text_rect.height = std::max(0.0f, bubble.height - pad_y * 2.0f);
    renderer.DrawText(text_, text_rect, style);
}

void Message::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

Layout::Size Message::GetMinSize() const {
    return {200.0f, 64.0f};
}

Layout::Size Message::GetPreferredSize() const {
    const float text_w =
        static_cast<float>(Utf8Length(text_)) * 7.0f + 24.0f;
    const float w = std::min(360.0f, std::max(200.0f, text_w + 44.0f));
    const int lines = std::max(
        1, static_cast<int>(std::ceil(
               static_cast<float>(Utf8Length(text_)) * 7.0f /
               std::max(1.0f, w - 44.0f - 24.0f))));
    const float h = 28.0f + 18.0f + 8.0f +
                    static_cast<float>(lines) * 18.0f + 16.0f;
    return {w, h};
}

Layout::Size Message::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
