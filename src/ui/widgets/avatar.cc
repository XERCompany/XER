#include "avatar.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

std::string InitialsOf(const std::string& name) {
    std::string initials;
    bool at_word_start = true;
    for (const unsigned char c : name) {
        if (std::isspace(c) || c == '_' || c == '-' || c == '.') {
            at_word_start = true;
            continue;
        }
        if (at_word_start && std::isalpha(c)) {
            initials.push_back(static_cast<char>(std::toupper(c)));
            at_word_start = false;
            if (initials.size() == 2) {
                break;
            }
        }
    }
    if (initials.empty() && !name.empty()) {
        initials.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))));
    }
    return initials;
}

Theme::Color AvatarColor(const std::string& name) {
    // Deterministic hue from name hash; keep it muted/shadcn-ish.
    std::uint32_t hash = 2166136261u;
    for (const unsigned char c : name) {
        hash = (hash ^ c) * 16777619u;
    }
    const float h = static_cast<float>(hash % 360u);
    // Simple HSV→RGB with moderate saturation/value.
    const float s = 0.45f, v = 0.55f;
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    auto chan = [m](float f) { return std::clamp(f + m, 0.0f, 1.0f); };
    return Theme::Color{chan(r), chan(g), chan(b), 1.0f};
}

} // namespace

Avatar::Avatar(std::string name, float size)
    : name_(std::move(name))
    , size_(size) {
}

void Avatar::SetName(std::string name) {
    if (name_ == name) {
        return;
    }
    name_ = std::move(name);
    Invalidate();
}

void Avatar::SetSize(float size) {
    if (size_ == size) {
        return;
    }
    size_ = size;
    Invalidate();
    RequestLayout();
}

void Avatar::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const float side = std::min(bounds.width, bounds.height);
    const Layout::Rect circle{
        bounds.x + (bounds.width - side) * 0.5f,
        bounds.y + (bounds.height - side) * 0.5f,
        side,
        side
    };

    renderer.FillRect(circle, AvatarColor(name_), side * 0.5f);
    if (!IsEnabled()) {
        renderer.FillRect(circle, tokens.muted, side * 0.5f);
    }

    const auto initials = InitialsOf(name_);
    if (!initials.empty()) {
        const Rendering::TextStyle style{tokens.surface, tokens.font_body, 600};
        renderer.DrawText(initials, circle, style);
    }
}

void Avatar::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Avatar::HandleEvent(const Event&) {
    return false;
}

Layout::Size Avatar::GetMinSize() const {
    return {size_, size_};
}

Layout::Size Avatar::GetPreferredSize() const {
    return {size_, size_};
}

Layout::Size Avatar::GetMaxSize() const {
    return {size_, size_};
}

} // namespace XER::UI::Widgets
