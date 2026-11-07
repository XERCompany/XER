#include "label.h"

#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

std::size_t Utf8Length(std::string_view text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

} // namespace

Label::Label(std::string text, bool muted)
    : text_(std::move(text))
    , muted_(muted) {
    SetFocusable(false);
}

void Label::SetText(std::string text) {
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    Invalidate();
    RequestLayout();
}

const std::string& Label::GetText() const {
    return text_;
}

void Label::SetMuted(bool muted) {
    if (muted_ == muted) {
        return;
    }
    muted_ = muted;
    Invalidate();
}

bool Label::IsMuted() const {
    return muted_;
}

void Label::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const Rendering::TextStyle style{
        !IsEnabled() || muted_ ? tokens.muted : tokens.foreground,
        tokens.font_body,
        400
    };
    renderer.DrawText(text_, CenteredTextRect(renderer, text_, style, 0.0f), style);
}

void Label::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Label::HandleEvent(const Event&) {
    return false;
}

Layout::Size Label::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size Label::GetPreferredSize() const {
    return {static_cast<float>(Utf8Length(text_)) * 7.7f, 18.0f};
}

Layout::Size Label::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
