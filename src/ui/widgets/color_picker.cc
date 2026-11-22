// See color_picker.h — OKLCH-canonical color picker, self-drawn chrome
// with real TextInput children for the hex/channel fields.
#include "color_picker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/text_input.h"

namespace XER::UI::Widgets {
namespace {

namespace CS = XER::UI::ColorSpace;

constexpr float kPad = 10.0f;
constexpr float kGap = 8.0f;
constexpr float kTabsHeight = 26.0f;
constexpr float kChannelWidth = 18.0f;
constexpr float kAlphaHeight = 16.0f;
constexpr float kInputRowHeight = 24.0f;
constexpr float kSwatchRowHeight = 22.0f;
constexpr float kSwatchSize = 20.0f;
constexpr float kSwatchGap = 5.0f;
constexpr float kPreviewWidth = 40.0f;
constexpr float kHexWidth = 88.0f;
constexpr float kFieldMaxHeight = 180.0f;
constexpr float kOklchChromaMax = 0.4f;  // display range for the C axis
constexpr float kThumbRadius = 7.0f;
constexpr int kFieldRasterMax = 224;
constexpr int kChannelRasterH = 128;

const char* kModeLabels[] = {"HSV", "HSL", "RGB", "OKLCH"};

Theme::Color WithAlpha(Theme::Color c, float a) {
    c.a *= a;
    return c;
}

void ToBgra(Theme::Color c, std::uint8_t* px) {
    c = CS::ClampSrgb(c);
    px[0] = static_cast<std::uint8_t>(c.b * 255.0f + 0.5f);
    px[1] = static_cast<std::uint8_t>(c.g * 255.0f + 0.5f);
    px[2] = static_cast<std::uint8_t>(c.r * 255.0f + 0.5f);
    px[3] = static_cast<std::uint8_t>(c.a * 255.0f + 0.5f);
}

// Bakes `color` over the classic two-gray checkerboard keyed by (x,y).
void CheckerBgra(Theme::Color color, int x, int y, std::uint8_t* px) {
    const bool dark = ((x / 4) + (y / 4)) % 2 == 0;
    const float bg = dark ? 0.42f : 0.72f;
    color = CS::ClampSrgb(color);
    const float a = color.a;
    px[0] = static_cast<std::uint8_t>((color.b * a + bg * (1.0f - a)) * 255.0f + 0.5f);
    px[1] = static_cast<std::uint8_t>((color.g * a + bg * (1.0f - a)) * 255.0f + 0.5f);
    px[2] = static_cast<std::uint8_t>((color.r * a + bg * (1.0f - a)) * 255.0f + 0.5f);
    px[3] = 255;
}

void DrawChecker(Rendering::Renderer& renderer, const Layout::Rect& rect,
                 float cell = 5.0f) {
    renderer.PushClip(rect);
    const Theme::Color a{0.42f, 0.42f, 0.42f, 1.0f};
    const Theme::Color b{0.72f, 0.72f, 0.72f, 1.0f};
    int row = 0;
    for (float y = rect.y; y < rect.Bottom(); y += cell, ++row) {
        int col = row % 2;
        for (float x = rect.x; x < rect.Right(); x += cell, ++col) {
            renderer.FillRect({x, y,
                               std::min(cell, rect.Right() - x),
                               std::min(cell, rect.Bottom() - y)},
                              col % 2 ? b : a, 0.0f);
        }
    }
    renderer.PopClip();
}

bool ParseFloat(std::string_view text, float* out) {
    if (text.empty()) {
        return false;
    }
    char buf[32];
    const std::size_t n = std::min(text.size(), sizeof(buf) - 1);
    std::memcpy(buf, text.data(), n);
    buf[n] = '\0';
    char* end = nullptr;
    const float v = std::strtof(buf, &end);
    if (end == buf || *end != '\0') {
        return false;
    }
    *out = v;
    return true;
}

// Malformed hex must not read as opaque black while the user is typing.
bool IsCompleteHex(std::string_view text) {
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6 && text.size() != 8) {
        return false;
    }
    for (char c : text) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                         (c >= 'A' && c <= 'F');
        if (!hex) {
            return false;
        }
    }
    return true;
}

} // namespace

ColorPicker::ColorPicker() {
    SetFocusable(true);
}

void ColorPicker::SetColor(Theme::Color color) {
    SetColorOklch(CS::OklchFromSrgb(color));
}

Theme::Color ColorPicker::GetColor() const {
    return CS::ClampSrgb(CS::SrgbFromOklch(color_));
}

void ColorPicker::SetColorOklch(const ColorSpace::Oklch& color) {
    ColorSpace::Oklch c = color;
    c.l = std::clamp(c.l, 0.0f, 1.0f);
    c.c = std::max(c.c, 0.0f);
    c.h = CS::WrapHue(c.h);
    c.a = std::clamp(c.a, 0.0f, 1.0f);
    // Keep the last hue across achromatic edits — atan2(0,0) is undefined
    // and a wandering hue makes the field jump on gray colors.
    if (c.c < 1.0e-4f) {
        c.h = color_.h;
    }
    if (c.l == color_.l && c.c == color_.c && c.h == color_.h &&
        c.a == color_.a) {
        return;
    }
    color_ = c;
    MarkFieldDirty();
    SyncInputs();
    Invalidate();
}

ColorSpace::Oklch ColorPicker::GetColorOklch() const {
    return color_;
}

void ColorPicker::SetMode(Mode mode) {
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    MarkFieldDirty();
    SyncInputs();
    Invalidate();
    RequestLayout();
}

void ColorPicker::SetShowAlpha(bool show) {
    if (show_alpha_ == show) {
        return;
    }
    show_alpha_ = show;
    if (!show) {
        color_.a = 1.0f;
    }
    Invalidate();
    RequestLayout();
}

void ColorPicker::SetOnColorChange(std::function<void(Theme::Color)> callback) {
    on_change_ = std::move(callback);
}

void ColorPicker::SetOnColorCommitted(
    std::function<void(Theme::Color)> callback) {
    on_committed_ = std::move(callback);
}

void ColorPicker::SetPalette(std::vector<Theme::Color> colors) {
    palette_ = std::move(colors);
    Invalidate();
    RequestLayout();
}

void ColorPicker::SetRecent(std::vector<Theme::Color> colors) {
    recent_ = std::move(colors);
    if (static_cast<int>(recent_.size()) > max_recent_) {
        recent_.resize(static_cast<std::size_t>(max_recent_));
    }
    Invalidate();
    RequestLayout();
}

void ColorPicker::AddRecent(Theme::Color color) {
    const Theme::Color clamped = CS::ClampSrgb(color);
    auto it = std::find(recent_.begin(), recent_.end(), clamped);
    if (it != recent_.end()) {
        recent_.erase(it);
    }
    recent_.insert(recent_.begin(), clamped);
    if (static_cast<int>(recent_.size()) > max_recent_) {
        recent_.resize(static_cast<std::size_t>(max_recent_));
    }
    Invalidate();
}

void ColorPicker::SetMaxRecent(int max) {
    max_recent_ = std::max(1, max);
    if (static_cast<int>(recent_.size()) > max_recent_) {
        recent_.resize(static_cast<std::size_t>(max_recent_));
        Invalidate();
    }
}

// ── Mode mapping ──────────────────────────────────────────────────────────
// The field axes and the vertical channel slider mean different things per
// mode; these helpers centralize the mapping so rasterization and input
// handling share one truth.

Theme::Color ColorPicker::ColorForFieldPoint(float fx, float fy) const {
    fx = std::clamp(fx, 0.0f, 1.0f);
    fy = std::clamp(fy, 0.0f, 1.0f);
    const Theme::Color srgb = GetColor();
    switch (mode_) {
    case Mode::Hsv: {
        CS::Hsv hsv = CS::HsvFromSrgb(srgb);
        hsv.s = fx;
        hsv.v = 1.0f - fy;
        return CS::SrgbFromHsv(hsv);
    }
    case Mode::Hsl: {
        CS::Hsl hsl = CS::HslFromSrgb(srgb);
        hsl.s = fx;
        hsl.l = 1.0f - fy;
        return CS::SrgbFromHsl(hsl);
    }
    case Mode::Rgb: {
        Theme::Color c = srgb;
        c.g = fx;
        c.b = 1.0f - fy;
        return c;
    }
    case Mode::Oklch: {
        CS::Oklch c = color_;
        c.c = fx * kOklchChromaMax;
        c.l = 1.0f - fy;
        return CS::SrgbFromOklch(c);
    }
    }
    return srgb;
}

Layout::Point ColorPicker::FieldPointForColor() const {
    const Theme::Color srgb = GetColor();
    switch (mode_) {
    case Mode::Hsv: {
        const CS::Hsv hsv = CS::HsvFromSrgb(srgb);
        return {hsv.s, 1.0f - hsv.v};
    }
    case Mode::Hsl: {
        const CS::Hsl hsl = CS::HslFromSrgb(srgb);
        return {hsl.s, 1.0f - hsl.l};
    }
    case Mode::Rgb:
        return {srgb.g, 1.0f - srgb.b};
    case Mode::Oklch:
        return {std::clamp(color_.c / kOklchChromaMax, 0.0f, 1.0f),
                1.0f - std::clamp(color_.l, 0.0f, 1.0f)};
    }
    return {};
}

Theme::Color ColorPicker::ColorForChannelValue(float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    const Theme::Color srgb = GetColor();
    switch (mode_) {
    case Mode::Hsv: {
        CS::Hsv hsv = CS::HsvFromSrgb(srgb);
        hsv.h = t * 360.0f;
        return CS::SrgbFromHsv(hsv);
    }
    case Mode::Hsl: {
        CS::Hsl hsl = CS::HslFromSrgb(srgb);
        hsl.h = t * 360.0f;
        return CS::SrgbFromHsl(hsl);
    }
    case Mode::Rgb: {
        Theme::Color c = srgb;
        c.r = t;
        return c;
    }
    case Mode::Oklch: {
        CS::Oklch c = color_;
        c.h = t * 360.0f;
        return CS::SrgbFromOklch(c);
    }
    }
    return srgb;
}

float ColorPicker::ChannelValueForColor() const {
    const Theme::Color srgb = GetColor();
    switch (mode_) {
    case Mode::Hsv:
        return CS::HsvFromSrgb(srgb).h / 360.0f;
    case Mode::Hsl:
        return CS::HslFromSrgb(srgb).h / 360.0f;
    case Mode::Rgb:
        return srgb.r;
    case Mode::Oklch:
        return color_.h / 360.0f;
    }
    return 0.0f;
}

Theme::Color ColorPicker::ColorForAlphaValue(float t) const {
    Theme::Color c = GetColor();
    c.a = std::clamp(t, 0.0f, 1.0f);
    return c;
}

void ColorPicker::ChannelLabels(const char* (&labels)[3],
                                float (&ranges)[3]) const {
    static const char* kHsv[] = {"H", "S", "V"};
    static const char* kHsl[] = {"H", "S", "L"};
    static const char* kRgb[] = {"R", "G", "B"};
    static const char* kLch[] = {"L", "C", "H"};
    switch (mode_) {
    case Mode::Hsv:
        labels[0] = kHsv[0]; labels[1] = kHsv[1]; labels[2] = kHsv[2];
        ranges[0] = 360.0f; ranges[1] = 100.0f; ranges[2] = 100.0f;
        break;
    case Mode::Hsl:
        labels[0] = kHsl[0]; labels[1] = kHsl[1]; labels[2] = kHsl[2];
        ranges[0] = 360.0f; ranges[1] = 100.0f; ranges[2] = 100.0f;
        break;
    case Mode::Rgb:
        labels[0] = kRgb[0]; labels[1] = kRgb[1]; labels[2] = kRgb[2];
        ranges[0] = 255.0f; ranges[1] = 255.0f; ranges[2] = 255.0f;
        break;
    case Mode::Oklch:
        labels[0] = kLch[0]; labels[1] = kLch[1]; labels[2] = kLch[2];
        ranges[0] = 1.0f; ranges[1] = kOklchChromaMax; ranges[2] = 360.0f;
        break;
    }
}

void ColorPicker::ChannelValues(float (&values)[3]) const {
    const Theme::Color srgb = GetColor();
    switch (mode_) {
    case Mode::Hsv: {
        const CS::Hsv hsv = CS::HsvFromSrgb(srgb);
        values[0] = hsv.h; values[1] = hsv.s * 100.0f; values[2] = hsv.v * 100.0f;
        break;
    }
    case Mode::Hsl: {
        const CS::Hsl hsl = CS::HslFromSrgb(srgb);
        values[0] = hsl.h; values[1] = hsl.s * 100.0f; values[2] = hsl.l * 100.0f;
        break;
    }
    case Mode::Rgb:
        values[0] = srgb.r * 255.0f;
        values[1] = srgb.g * 255.0f;
        values[2] = srgb.b * 255.0f;
        break;
    case Mode::Oklch:
        values[0] = color_.l; values[1] = color_.c; values[2] = color_.h;
        break;
    }
}

Theme::Color ColorPicker::ColorForChannelValues(
    const float (&values)[3]) const {
    const Theme::Color srgb = GetColor();
    float ranges[3];
    const char* labels[3];
    ChannelLabels(labels, ranges);
    const float v0 = std::clamp(values[0], 0.0f, ranges[0]);
    const float v1 = std::clamp(values[1], 0.0f, ranges[1]);
    const float v2 = std::clamp(values[2], 0.0f, ranges[2]);
    switch (mode_) {
    case Mode::Hsv:
        return CS::SrgbFromHsv({v0, v1 / 100.0f, v2 / 100.0f, srgb.a});
    case Mode::Hsl:
        return CS::SrgbFromHsl({v0, v1 / 100.0f, v2 / 100.0f, srgb.a});
    case Mode::Rgb:
        return {v0 / 255.0f, v1 / 255.0f, v2 / 255.0f, srgb.a};
    case Mode::Oklch:
        return CS::SrgbFromOklch({v0, v1, v2, srgb.a});
    }
    return srgb;
}

// ── State application ─────────────────────────────────────────────────────

void ColorPicker::ApplyColor(Theme::Color color, bool live) {
    SetColorOklch(CS::OklchFromSrgb(color));
    if (live && on_change_) {
        on_change_(GetColor());
    }
}

void ColorPicker::Commit() {
    const Theme::Color c = GetColor();
    if (auto_recent_) {
        AddRecent(c);
    }
    if (on_committed_) {
        on_committed_(c);
    }
}

void ColorPicker::EnsureChildren() {
    if (inputs_ready_) {
        return;
    }
    // Children can only attach once shared ownership exists.
    if (weak_from_this().expired()) {
        return;
    }
    inputs_ready_ = true;

    hex_input_ = std::make_shared<TextInput>();
    hex_input_->SetPlaceholder("#RRGGBB");
    hex_input_->SetOnValueChanged([this](const std::string& text) {
        if (updating_inputs_ || !IsCompleteHex(text)) {
            return;
        }
        Theme::Color c = CS::ColorFromHex(text);
        if (!show_alpha_) {
            c.a = 1.0f;
        }
        ApplyColor(c, true);
    });
    hex_input_->SetOnSubmit([this] { Commit(); });
    AddChild(hex_input_);

    for (int i = 0; i < 3; ++i) {
        ch_input_[i] = std::make_shared<TextInput>();
        ch_input_[i]->SetOnValueChanged(
            [this, i](const std::string&) {
                if (updating_inputs_) {
                    return;
                }
                float values[3] = {0.0f, 0.0f, 0.0f};
                ChannelValues(values);
                for (int j = 0; j < 3; ++j) {
                    float parsed = 0.0f;
                    if (ch_input_[j] != nullptr &&
                        ParseFloat(ch_input_[j]->GetValue(), &parsed)) {
                        values[j] = parsed;
                    }
                }
                ApplyColor(ColorForChannelValues(values), true);
            });
        ch_input_[i]->SetOnSubmit([this] { Commit(); });
        AddChild(ch_input_[i]);
    }
    SyncInputs();
}

void ColorPicker::SyncInputs() {
    if (!inputs_ready_) {
        return;
    }
    updating_inputs_ = true;
    if (hex_input_ != nullptr && !hex_input_->IsFocused()) {
        hex_input_->SetValue(CS::HexFromColor(GetColor(), show_alpha_));
    }
    float values[3];
    ChannelValues(values);
    const bool decimals = (mode_ == Mode::Oklch);
    for (int i = 0; i < 3; ++i) {
        if (ch_input_[i] == nullptr || ch_input_[i]->IsFocused()) {
            continue;
        }
        char buf[24];
        if (decimals) {
            std::snprintf(buf, sizeof(buf), "%.2f",
                          static_cast<double>(values[i]));
        } else {
            std::snprintf(buf, sizeof(buf), "%.0f",
                          static_cast<double>(values[i]));
        }
        ch_input_[i]->SetValue(buf);
    }
    updating_inputs_ = false;
}

// ── Rasterization ─────────────────────────────────────────────────────────

void ColorPicker::RasterizeField(int w, int h,
                                 std::vector<std::uint8_t>* out) const {
    out->resize(static_cast<std::size_t>(w) * h * 4);
    std::uint8_t* px = out->data();
    for (int y = 0; y < h; ++y) {
        const float fy = (y + 0.5f) / h;
        for (int x = 0; x < w; ++x) {
            const float fx = (x + 0.5f) / w;
            Theme::Color c = ColorForFieldPoint(fx, fy);
            c.a = 1.0f;
            ToBgra(c, px + (static_cast<std::size_t>(y) * w + x) * 4);
        }
    }
}

void ColorPicker::RasterizeChannel(int w, int h,
                                   std::vector<std::uint8_t>* out) const {
    out->resize(static_cast<std::size_t>(w) * h * 4);
    std::uint8_t* px = out->data();
    for (int y = 0; y < h; ++y) {
        const float t = 1.0f - (y + 0.5f) / h;  // max at top
        Theme::Color c = ColorForChannelValue(t);
        c.a = 1.0f;
        for (int x = 0; x < w; ++x) {
            ToBgra(c, px + (static_cast<std::size_t>(y) * w + x) * 4);
        }
    }
}

void ColorPicker::RasterizeAlphaRamp(int w, int h,
                                     std::vector<std::uint8_t>* out) const {
    out->resize(static_cast<std::size_t>(w) * h * 4);
    std::uint8_t* px = out->data();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            Theme::Color c = GetColor();
            c.a = std::clamp((x + 0.5f) / w, 0.0f, 1.0f);
            CheckerBgra(c, x, y,
                        px + (static_cast<std::size_t>(y) * w + x) * 4);
        }
    }
}

// ── Layout ────────────────────────────────────────────────────────────────

ColorPicker::Chrome ColorPicker::GetChrome() const {
    Chrome c{};
    const auto b = GetBounds();
    float y = b.y + kPad;
    const float inner_x = b.x + kPad;
    const float inner_w = std::max(0.0f, b.width - 2.0f * kPad);

    c.tabs = {inner_x, y, inner_w, kTabsHeight};
    y += kTabsHeight + kGap;

    const float field_w = std::max(40.0f, inner_w - kChannelWidth - kGap);
    const float field_h = std::clamp(field_w * 0.62f, 70.0f, kFieldMaxHeight);
    c.field = {inner_x, y, field_w, field_h};
    c.channel = {inner_x + field_w + kGap, y, kChannelWidth, field_h};
    y += field_h + kGap;

    if (show_alpha_) {
        c.alpha = {inner_x, y, inner_w, kAlphaHeight};
        y += kAlphaHeight + kGap;
    }

    const float input_y = y;
    c.preview = {inner_x, input_y, kPreviewWidth, kInputRowHeight};
    c.hex_input = {inner_x + kPreviewWidth + kGap, input_y, kHexWidth,
                   kInputRowHeight};
    const float ch_x = c.hex_input.Right() + kGap;
    const float ch_w =
        std::max(0.0f, (inner_x + inner_w - ch_x - 2.0f * kGap) / 3.0f);
    for (int i = 0; i < 3; ++i) {
        c.ch_input[i] = {ch_x + i * (ch_w + kGap), input_y, ch_w,
                         kInputRowHeight};
    }
    y += kInputRowHeight;

    if (!palette_.empty()) {
        y += kGap;
        c.palette = {inner_x, y, inner_w, kSwatchRowHeight};
        y += kSwatchRowHeight;
    }
    if (!recent_.empty()) {
        y += kGap;
        c.recent = {inner_x, y, inner_w, kSwatchRowHeight};
        y += kSwatchRowHeight;
    }
    return c;
}

void ColorPicker::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    EnsureChildren();
    const Chrome c = GetChrome();
    if (hex_input_ != nullptr) {
        hex_input_->Layout(c.hex_input, CachedLayoutTokens());
    }
    for (int i = 0; i < 3; ++i) {
        if (ch_input_[i] != nullptr) {
            // Right half of each cell is the field; the left ~16px draws
            // the channel label in Render.
            const Layout::Rect cell = c.ch_input[i];
            const float label_w = 16.0f;
            ch_input_[i]->Layout({cell.x + label_w, cell.y,
                                  std::max(0.0f, cell.width - label_w),
                                  cell.height},
                                 CachedLayoutTokens());
        }
    }
}

int ColorPicker::HitTab(const Layout::Point& p) const {
    const Chrome c = GetChrome();
    if (!c.tabs.Contains(p)) {
        return -1;
    }
    const float cell_w = c.tabs.width / 4.0f;
    return std::clamp(static_cast<int>((p.x - c.tabs.x) / cell_w), 0, 3);
}

int ColorPicker::HitSwatch(const Layout::Point& p, const Layout::Rect& row,
                           std::size_t count) const {
    if (!row.Contains(p)) {
        return -1;
    }
    const int idx =
        static_cast<int>((p.x - row.x) / (kSwatchSize + kSwatchGap));
    const float local =
        std::fmod(p.x - row.x, kSwatchSize + kSwatchGap);
    if (local > kSwatchSize || idx < 0 ||
        idx >= static_cast<int>(count)) {
        return -1;
    }
    return idx;
}

// ── Rendering ─────────────────────────────────────────────────────────────

void ColorPicker::Render(Rendering::Renderer& renderer,
                         const Theme::Tokens& tokens) {
    const Chrome c = GetChrome();
    const Theme::Color srgb = GetColor();
    const Rendering::TextStyle label_style{tokens.muted, tokens.font_small, 500};
    const Rendering::TextStyle tab_active{tokens.foreground, tokens.font_small,
                                          600};
    const Rendering::TextStyle tab_idle{tokens.muted, tokens.font_small, 500};

    // ── Mode tabs ──
    {
        renderer.FillRect(c.tabs, tokens.secondary,
                          tokens.radius_medium);
        const float cell_w = c.tabs.width / 4.0f;
        for (int i = 0; i < 4; ++i) {
            const Layout::Rect cell{c.tabs.x + i * cell_w, c.tabs.y,
                                    cell_w, c.tabs.height};
            const bool active = static_cast<int>(mode_) == i;
            if (active) {
                renderer.FillRect({cell.x + 2.0f, cell.y + 2.0f,
                                   cell.width - 4.0f, cell.height - 4.0f},
                                  tokens.surface, tokens.radius_small);
            }
            const auto& style = active ? tab_active : tab_idle;
            const auto size =
                renderer.MeasureText(kModeLabels[i], style);
            renderer.DrawText(kModeLabels[i],
                              {cell.x + (cell.width - size.width) / 2.0f,
                               cell.y + (cell.height - size.height) / 2.0f,
                               size.width + 2.0f, size.height},
                              style);
        }
    }

    // ── 2D color field ──
    {
        const int rw = std::clamp(static_cast<int>(c.field.width), 8,
                                  kFieldRasterMax);
        const int rh = std::clamp(static_cast<int>(c.field.height), 8,
                                  kFieldRasterMax);
        if (field_dirty_ || rw != field_raster_w_ ||
            rh != field_raster_h_) {
            RasterizeField(rw, rh, &field_raster_);
            field_raster_w_ = rw;
            field_raster_h_ = rh;
            field_dirty_ = false;
        }
        renderer.PushClip(c.field);
        if (!renderer.DrawFrameBgra(field_raster_.data(), field_raster_w_,
                                    field_raster_h_, c.field)) {
            // Coarse fallback: 16x12 cells, same mapping.
            for (int y = 0; y < 12; ++y) {
                for (int x = 0; x < 16; ++x) {
                    Theme::Color cc = ColorForFieldPoint((x + 0.5f) / 16.0f,
                                                         (y + 0.5f) / 12.0f);
                    cc.a = 1.0f;
                    renderer.FillRect({c.field.x + x * c.field.width / 16.0f,
                                       c.field.y + y * c.field.height / 12.0f,
                                       c.field.width / 16.0f + 1.0f,
                                       c.field.height / 12.0f + 1.0f},
                                      cc, 0.0f);
                }
            }
        }
        renderer.PopClip();
        renderer.StrokeRect(c.field, tokens.border, 1.0f,
                            tokens.radius_small);

        // Field marker.
        const Layout::Point fp = FieldPointForColor();
        const float mx = c.field.x + fp.x * c.field.width;
        const float my = c.field.y + fp.y * c.field.height;
        renderer.FillRect({mx - kThumbRadius, my - kThumbRadius,
                           kThumbRadius * 2, kThumbRadius * 2},
                          {1.0f, 1.0f, 1.0f, 1.0f}, kThumbRadius);
        renderer.StrokeRect({mx - kThumbRadius, my - kThumbRadius,
                             kThumbRadius * 2, kThumbRadius * 2},
                            {0.0f, 0.0f, 0.0f, 0.7f}, 1.5f, kThumbRadius);
    }

    // ── Channel slider (vertical) ──
    {
        if (channel_dirty_ || channel_raster_h_ != kChannelRasterH) {
            RasterizeChannel(8, kChannelRasterH, &channel_raster_);
            channel_raster_h_ = kChannelRasterH;
            channel_dirty_ = false;
        }
        renderer.PushClip(c.channel);
        if (!renderer.DrawFrameBgra(channel_raster_.data(), 8,
                                    channel_raster_h_, c.channel)) {
            for (int i = 0; i < 12; ++i) {
                Theme::Color cc =
                    ColorForChannelValue(1.0f - (i + 0.5f) / 12.0f);
                cc.a = 1.0f;
                renderer.FillRect({c.channel.x,
                                   c.channel.y +
                                       i * c.channel.height / 12.0f,
                                   c.channel.width,
                                   c.channel.height / 12.0f + 1.0f},
                                  cc, 0.0f);
            }
        }
        renderer.PopClip();
        renderer.StrokeRect(c.channel, tokens.border, 1.0f,
                            tokens.radius_small);
        const float t = ChannelValueForColor();
        const float ty = c.channel.y + (1.0f - t) * c.channel.height;
        const Layout::Rect thumb{c.channel.x - 2.0f, ty - 4.0f,
                                 c.channel.width + 4.0f, 8.0f};
        renderer.FillRect(thumb, {1.0f, 1.0f, 1.0f, 1.0f}, 4.0f);
        renderer.StrokeRect(thumb, {0.0f, 0.0f, 0.0f, 0.7f}, 1.5f, 4.0f);
    }

    // ── Alpha slider ──
    if (show_alpha_ && c.alpha.height > 0.0f) {
        const int aw = std::clamp(static_cast<int>(c.alpha.width), 16, 256);
        std::vector<std::uint8_t> ramp;
        RasterizeAlphaRamp(aw, 8, &ramp);
        if (!renderer.DrawFrameBgra(ramp.data(), aw, 8, c.alpha)) {
            DrawChecker(renderer, c.alpha, 4.0f);
            const Theme::Color opaque{srgb.r, srgb.g, srgb.b, 1.0f};
            for (int i = 0; i < 24; ++i) {
                Theme::Color cc = opaque;
                cc.a = (i + 0.5f) / 24.0f;
                renderer.FillRect({c.alpha.x + i * c.alpha.width / 24.0f,
                                   c.alpha.y, c.alpha.width / 24.0f + 1.0f,
                                   c.alpha.height},
                                  cc, 0.0f);
            }
        }
        renderer.StrokeRect(c.alpha, tokens.border, 1.0f,
                            tokens.radius_small);
        const float ax = c.alpha.x + color_.a * c.alpha.width;
        const Layout::Rect thumb{ax - 4.0f, c.alpha.y - 2.0f, 8.0f,
                                 c.alpha.height + 4.0f};
        renderer.FillRect(thumb, {1.0f, 1.0f, 1.0f, 1.0f}, 4.0f);
        renderer.StrokeRect(thumb, {0.0f, 0.0f, 0.0f, 0.7f}, 1.5f, 4.0f);
    }

    // ── Preview swatch ──
    DrawChecker(renderer, c.preview, 4.0f);
    renderer.FillRect(c.preview, srgb, tokens.radius_small);
    renderer.StrokeRect(c.preview, tokens.border, 1.0f, tokens.radius_small);

    // ── Channel labels beside the numeric inputs ──
    {
        const char* labels[3];
        float ranges[3];
        ChannelLabels(labels, ranges);
        for (int i = 0; i < 3; ++i) {
            const Layout::Rect cell = c.ch_input[i];
            renderer.DrawText(labels[i],
                              {cell.x, cell.y + 4.0f, 16.0f,
                               cell.height - 8.0f},
                              label_style);
        }
    }

    // ── Swatch rows ──
    const auto draw_row = [&](const Layout::Rect& row,
                              const std::vector<Theme::Color>& colors) {
        if (row.height <= 0.0f) {
            return;
        }
        for (std::size_t i = 0; i < colors.size(); ++i) {
            const float x = row.x + i * (kSwatchSize + kSwatchGap);
            if (x + kSwatchSize > row.Right()) {
                break;
            }
            const Layout::Rect cell{x, row.y + 1.0f, kSwatchSize,
                                    kSwatchSize};
            if (colors[i].a < 1.0f) {
                DrawChecker(renderer, cell, 4.0f);
            }
            renderer.FillRect(cell, colors[i], tokens.radius_small);
            renderer.StrokeRect(cell, tokens.border, 1.0f,
                                tokens.radius_small);
        }
    };
    draw_row(c.palette, palette_);
    draw_row(c.recent, recent_);

    if (IsFocused()) {
        RenderFocusRing(renderer, tokens, tokens.radius_medium);
    }
}

// ── Events ────────────────────────────────────────────────────────────────

bool ColorPicker::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }
    const Chrome c = GetChrome();

    switch (event.type) {
    case Event::Type::MouseDown:
        if (event.mouse_button != MouseButton::Left) {
            return false;
        }
        if (const int tab = HitTab(event.position); tab >= 0) {
            SetMode(static_cast<Mode>(tab));
            return true;
        }
        if (c.field.Contains(event.position)) {
            drag_ = Drag::Field;
            ApplyColor(ColorForFieldPoint(
                           (event.position.x - c.field.x) / c.field.width,
                           (event.position.y - c.field.y) / c.field.height),
                       true);
            return true;
        }
        if (c.channel.Contains(event.position)) {
            drag_ = Drag::Channel;
            ApplyColor(ColorForChannelValue(
                           1.0f - (event.position.y - c.channel.y) /
                                      c.channel.height),
                       true);
            return true;
        }
        if (show_alpha_ && c.alpha.Contains(event.position)) {
            drag_ = Drag::Alpha;
            ApplyColor(ColorForAlphaValue(
                           (event.position.x - c.alpha.x) / c.alpha.width),
                       true);
            return true;
        }
        if (const int i = HitSwatch(event.position, c.palette,
                                    palette_.size());
            i >= 0) {
            SetColor(palette_[static_cast<std::size_t>(i)]);
            Commit();
            return true;
        }
        if (const int i = HitSwatch(event.position, c.recent,
                                    recent_.size());
            i >= 0) {
            SetColor(recent_[static_cast<std::size_t>(i)]);
            Commit();
            return true;
        }
        return false;

    case Event::Type::MouseMove:
        if (drag_ == Drag::Field) {
            ApplyColor(ColorForFieldPoint(
                           (event.position.x - c.field.x) / c.field.width,
                           (event.position.y - c.field.y) / c.field.height),
                       true);
            return true;
        }
        if (drag_ == Drag::Channel) {
            ApplyColor(ColorForChannelValue(
                           1.0f - (event.position.y - c.channel.y) /
                                      c.channel.height),
                       true);
            return true;
        }
        if (drag_ == Drag::Alpha) {
            ApplyColor(ColorForAlphaValue(
                           (event.position.x - c.alpha.x) / c.alpha.width),
                       true);
            return true;
        }
        return false;

    case Event::Type::MouseUp:
        if (event.mouse_button == MouseButton::Left &&
            drag_ != Drag::None) {
            drag_ = Drag::None;
            Commit();
            return true;
        }
        return false;

    case Event::Type::Blur:
        drag_ = Drag::None;
        return false;

    default:
        return false;
    }
}

Layout::Size ColorPicker::GetMinSize() const {
    return {232.0f, 240.0f};
}

Layout::Size ColorPicker::GetPreferredSize() const {
    const float w = 300.0f;
    const float inner = w - 2.0f * kPad;
    const float field_w = inner - kChannelWidth - kGap;
    const float field_h =
        std::clamp(field_w * 0.62f, 70.0f, kFieldMaxHeight);
    float h = 2.0f * kPad + kTabsHeight + kGap + field_h + kGap +
              kInputRowHeight;
    if (show_alpha_) {
        h += kAlphaHeight + kGap;
    }
    if (!palette_.empty()) {
        h += kSwatchRowHeight + kGap;
    }
    if (!recent_.empty()) {
        h += kSwatchRowHeight + kGap;
    }
    return {w, h};
}

Layout::Size ColorPicker::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
