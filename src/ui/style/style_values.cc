#include "style/style_values.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <string>

#include "content/css/computed_style.h"

namespace XER::UI::CSS {
namespace {

std::string_view Trim(std::string_view s) {
    while (!s.empty() &&
           std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() &&
           std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct NamedColor {
    std::string_view name;
    Theme::Color color;
};

constexpr NamedColor kNamed[] = {
    {"transparent", {0.0f, 0.0f, 0.0f, 0.0f}},
    {"black", {0.0f, 0.0f, 0.0f, 1.0f}},
    {"white", {1.0f, 1.0f, 1.0f, 1.0f}},
    {"red", {1.0f, 0.0f, 0.0f, 1.0f}},
    {"green", {0.0f, 0.5f, 0.0f, 1.0f}},
    {"lime", {0.0f, 1.0f, 0.0f, 1.0f}},
    {"blue", {0.0f, 0.0f, 1.0f, 1.0f}},
    {"yellow", {1.0f, 1.0f, 0.0f, 1.0f}},
    {"orange", {1.0f, 0.647f, 0.0f, 1.0f}},
    {"purple", {0.5f, 0.0f, 0.5f, 1.0f}},
    {"magenta", {1.0f, 0.0f, 1.0f, 1.0f}},
    {"cyan", {0.0f, 1.0f, 1.0f, 1.0f}},
    {"pink", {1.0f, 0.753f, 0.796f, 1.0f}},
    {"brown", {0.647f, 0.165f, 0.165f, 1.0f}},
    {"gray", {0.5f, 0.5f, 0.5f, 1.0f}},
    {"grey", {0.5f, 0.5f, 0.5f, 1.0f}},
    {"silver", {0.753f, 0.753f, 0.753f, 1.0f}},
    {"navy", {0.0f, 0.0f, 0.5f, 1.0f}},
    {"teal", {0.0f, 0.5f, 0.5f, 1.0f}},
    {"olive", {0.5f, 0.5f, 0.0f, 1.0f}},
    {"maroon", {0.5f, 0.0f, 0.0f, 1.0f}},
};

}  // namespace

std::optional<Theme::Color> ParseColor(std::string_view value) {
    value = Trim(value);
    if (value.empty()) return std::nullopt;

    if (value.front() == '#') {
        value.remove_prefix(1);
        auto hex = [&](std::size_t i) {
            return static_cast<float>(HexDigit(value[i]) * 16 +
                                      HexDigit(value[i + 1])) /
                   255.0f;
        };
        auto short_hex = [&](std::size_t i) {
            const int d = HexDigit(value[i]);
            return static_cast<float>(d * 16 + d) / 255.0f;
        };
        const auto n = value.size();
        for (char c : value)
            if (HexDigit(c) < 0) return std::nullopt;
        if (n == 6 || n == 8) {
            Theme::Color c{hex(0), hex(2), hex(4), 1.0f};
            if (n == 8) c.a = hex(6);
            return c;
        }
        if (n == 3 || n == 4) {
            Theme::Color c{short_hex(0), short_hex(1), short_hex(2),
                           1.0f};
            if (n == 4) c.a = short_hex(3);
            return c;
        }
        return std::nullopt;
    }

    // rgb(r, g, b[, a]) / rgba(r, g, b, a)
    if (value.starts_with("rgb")) {
        const auto open = value.find('(');
        const auto close = value.rfind(')');
        if (open == std::string_view::npos ||
            close == std::string_view::npos || close <= open)
            return std::nullopt;
        std::string inner{value.substr(open + 1, close - open - 1)};
        std::replace(inner.begin(), inner.end(), ',', ' ');
        float parts[4] = {0, 0, 0, 1.0f};
        int count = 0;
        const char* p = inner.c_str();
        char* end = nullptr;
        while (count < 4) {
            const float v = std::strtof(p, &end);
            if (end == p) break;
            parts[count++] = v;
            p = end;
        }
        if (count < 3) return std::nullopt;
        return Theme::Color{parts[0] / 255.0f, parts[1] / 255.0f,
                            parts[2] / 255.0f, parts[3]};
    }

    for (const auto& nc : kNamed)
        if (value == nc.name) return nc.color;
    return std::nullopt;
}

std::optional<float> ParseLength(std::string_view value,
                                 float em_base) {
    value = Trim(value);
    if (value.empty()) return std::nullopt;
    float scale = 1.0f;
    if (value.ends_with("px")) {
        value.remove_suffix(2);
    } else if (value.ends_with("pt")) {
        value.remove_suffix(2);
        scale = 96.0f / 72.0f;
    } else if (value.ends_with("rem")) {
        value.remove_suffix(3);
        scale = em_base;
    } else if (value.ends_with("em")) {
        value.remove_suffix(2);
        scale = em_base;
    }
    value = Trim(value);
    if (value.empty()) return std::nullopt;
    float out = 0.0f;
    const auto [ptr, ec] = std::from_chars(
        value.data(), value.data() + value.size(), out);
    if (ec != std::errc{} || ptr != value.data() + value.size())
        return std::nullopt;
    return out * scale;
}

void ApplyComputedToTokens(const Content::CSS::ComputedStyle& style,
                           Theme::Tokens* tokens) {
    if (!tokens) return;
    auto color = [&](std::string_view prop, Theme::Color* dst) {
        if (const std::string* v = style.Get(std::string(prop)))
            if (auto c = ParseColor(*v)) *dst = *c;
    };
    auto len = [&](std::string_view prop, float* dst) {
        if (const std::string* v = style.Get(std::string(prop)))
            if (auto l = ParseLength(*v, tokens->font_body))
                *dst = *l;
    };
    color("background-color", &tokens->background);
    color("color", &tokens->foreground);
    color("foreground", &tokens->foreground);
    color("surface", &tokens->surface);
    color("muted", &tokens->muted);
    color("panel", &tokens->panel);
    color("border-color", &tokens->border);
    color("accent-color", &tokens->accent);
    color("accent-foreground", &tokens->accent_foreground);
    color("focus-ring-color", &tokens->ring);
    color("ring", &tokens->ring);
    color("secondary", &tokens->secondary);
    color("hover", &tokens->hover);
    color("danger-color", &tokens->danger);
    color("danger-foreground", &tokens->danger_foreground);
    color("input", &tokens->input);
    color("pressed", &tokens->pressed);
    color("disabled", &tokens->disabled);
    color("selection", &tokens->selection);
    len("radius", &tokens->radius_medium);
    len("border-radius", &tokens->radius_medium);
    len("radius-small", &tokens->radius_small);
    len("radius-large", &tokens->radius_large);
    len("spacing-small", &tokens->spacing_small);
    len("spacing-medium", &tokens->spacing_medium);
    len("spacing-large", &tokens->spacing_large);
    len("font-size", &tokens->font_body);
    len("font-body", &tokens->font_body);
    len("font-small", &tokens->font_small);
    len("font-title", &tokens->font_title);
}

}  // namespace XER::UI::CSS
