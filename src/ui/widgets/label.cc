#include "label.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr std::string_view kEllipsis = "\xE2\x80\xA6";  // U+2026

// The pre-measurement char-width estimate — kept ONLY as the fallback
// when neither an installed measure target nor a Render-populated
// cache exists (headless layouts before first paint).
constexpr float kEstimatedCharWidth = 7.7f;
constexpr float kEstimatedLineHeight = 18.0f;

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
    measured_valid_ = false;
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
    // Populate the measure cache so GetMinSize/GetPreferredSize report
    // real metrics even when no text-measure target is installed. A
    // changed measurement means the preferred size just moved — flag a
    // relayout so the box model sees the honest extents on the next pass.
    const Layout::Size measured = renderer.MeasureText(text_, style);
    if (!measured_valid_ || measured.width != measured_.width ||
        measured.height != measured_.height) {
        measured_ = measured;
        measured_valid_ = true;
        RequestLayout();
    }
    const Layout::Rect rect =
        CenteredTextRect(renderer, text_, style, 0.0f);
    // DrawText wraps word-broken text — a label squeezed under its
    // measured width would break mid-word onto a second (clipped)
    // line. Labels are single-line by contract: ellipsize instead.
    renderer.DrawText(
        EllipsizeText(renderer, text_, style, rect.width), rect,
        style);
}

void Label::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Label::HandleEvent(const Event&) {
    return false;
}

Rendering::TextStyle Label::MeasureStyle() const {
    // Same style Render() uses, minus the color (measurement is
    // color-independent). CachedLayoutTokens carries the theme's font
    // size once the widget has been through a themed Layout; before
    // that it is zero-initialized, so fall back to the stock 14px body
    // size (Theme::Provider's default tokens) rather than measure a
    // 0pt font.
    const float size = CachedLayoutTokens().font_body;
    return Rendering::TextStyle{
        Theme::Color{},
        size > 0.0f ? size : 14.0f,
        400
    };
}

Layout::Size Label::Measure(std::string_view text) const {
    if (const auto* target = GetTextMeasureTarget(); target != nullptr) {
        return target->MeasureText(text, MeasureStyle());
    }
    if (measured_valid_ && text == text_) {
        return measured_;
    }
    return {
        static_cast<float>(Utf8Length(text)) * kEstimatedCharWidth,
        kEstimatedLineHeight
    };
}

Layout::Size Label::GetMinSize() const {
    // A squeezed label draws "…" (see Render), so the honest minimum is
    // one ellipsis glyph wide by one line tall — never {0,0}, which lied
    // to flex layouts into assigning zero extent.
    const Layout::Size ellipsis = Measure(kEllipsis);
    const float height = std::max(
        ellipsis.height,
        text_.empty() ? 0.0f : Measure(text_).height);
    return {ellipsis.width, height};
}

Layout::Size Label::GetPreferredSize() const {
    return Measure(text_);
}

bool Label::IsDisplayTextClipped() const {
    // Half-pixel slack absorbs measurement rounding on boundary widths.
    return Measure(text_).width > GetBounds().width + 0.5f;
}

Layout::Size Label::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
