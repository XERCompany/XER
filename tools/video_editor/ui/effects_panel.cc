#include "effects_panel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <utility>

#include "app_runtime/icon_pack.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Theme::Color;

constexpr float kRowHeight = 38.0f;
constexpr float kRowIcon = 16.0f;
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(450);

Color WithAlpha(Color color, float alpha) {
    color.a *= alpha;
    return color;
}

// Theme float color → packed AARRGGBB tint for the icon pack.
std::uint32_t ToArgb(Color color) {
    const auto byte = [](float v) {
        return static_cast<std::uint32_t>(
            std::clamp(std::lround(v * 255.0f), 0L, 255L));
    };
    return (byte(color.a) << 24) | (byte(color.r) << 16) |
           (byte(color.g) << 8) | byte(color.b);
}

// Rasterized SVG icon from the shared pack, drawn centered in `box`.
// Returns false when the icon is missing or the renderer can't blit
// it — the caller then keeps the text-only fallback layout.
bool DrawIcon(XER::UI::Rendering::Renderer& renderer, const char* name,
              int px, Color tint, const XER::UI::Layout::Rect& box) {
    const auto* bmp =
        AppRuntime::SharedIconPack().Get(name, px, ToArgb(tint));
    if (bmp == nullptr) return false;
    const XER::UI::Layout::Rect dst{
        box.x + (box.width - bmp->width) * 0.5f,
        box.y + (box.height - bmp->height) * 0.5f,
        static_cast<float>(bmp->width),
        static_cast<float>(bmp->height)};
    return renderer.DrawFrameBgra(bmp->bgra.data(), bmp->width,
                                  bmp->height, dst, 1.0f);
}

// Logical icon name (assets/icons.yaml) per built-in effect type.
// Unmapped names fall back to "<name>.svg" — every file used here
// exists under assets/icons/.
const char* IconForEffectType(const std::string& type) {
    if (type.find("flip") != std::string::npos) {
        return "compare_arrows";
    }
    if (type.find("blur") != std::string::npos) return "shadow";
    if (type == "chroma_key") return "select_window";
    if (type == "vignette" || type == "pixelate") return "tile_medium";
    return "tab.effects";   // polymer.svg — generic effects glyph
}

std::string Lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

}  // namespace

const std::vector<EffectsPanelWidget::EffectInfo>&
EffectsPanelWidget::Catalog() {
    // type names + default params mirror the dispatch table and its
    // documented fallbacks in compositor.cc ApplyClipEffects; keep in
    // sync when new effects land there.
    static const std::vector<EffectInfo> catalog = {
        {"brightness_contrast", {0.0f, 1.0f},
         "brightness -255..255 · contrast 0..4"},
        {"grayscale", {}, "remove color"},
        {"box_blur", {4.0f}, "cheap blur · radius px"},
        {"gaussian_blur", {2.0f}, "soft blur · sigma"},
        {"flip_h", {}, "mirror horizontally"},
        {"flip_v", {}, "mirror vertically"},
        {"sharpen", {1.5f}, "unsharp mask · amount"},
        {"edge_detect", {}, "edge highlight (Skia)"},
        {"emboss", {}, "relief emboss (Skia)"},
        {"vignette", {0.5f, 0.7f}, "darken edges · strength, size (Skia)"},
        {"chromatic_aberration", {2.0f}, "RGB fringe · shift px (Skia)"},
        {"pixelate", {8.0f}, "mosaic · block px (Skia)"},
        {"oil_paint", {4.0f}, "painterly · radius (Skia)"},
        {"motion_blur", {0.0f, 10.0f}, "directional · angle deg, samples"},
        {"chroma_key", {0.0f, 255.0f, 0.0f, 120.0f, 40.0f},
         "key out color · key r,g,b 0-255, tolerance, softness"},
        {"color_balance", {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
         "lift+gain grade · lift r,g,b -1..1, gain r,g,b 0..4"},
        {"posterize", {4.0f}, "band colors · levels per channel 2-256"},
    };
    return catalog;
}

EffectsPanelWidget::EffectsPanelWidget(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);   // list scrolls inside the panel
    RecomputeFilter();
}

void EffectsPanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    title_ = std::make_shared<Widgets::Label>("EFFECTS");
    search_ = std::make_shared<Widgets::TextInput>("");
    search_->SetPlaceholder("Search effects...");
    search_->SetTooltip("Filter by name or description");
    search_->SetOnValueChanged([this](const std::string&) {
        RecomputeFilter();
        Invalidate();
    });
    add_btn_ = std::make_shared<Widgets::Button>(
        "Add", Widgets::ButtonStyle::Primary);
    add_btn_->SetTooltip("Apply the selected effect to the clip");
    add_btn_->SetOnClick([this] { AddSelected(); });
    status_ = std::make_shared<Widgets::Label>("Select a clip", true);

    AddChild(title_);
    AddChild(search_);
    AddChild(add_btn_);
    AddChild(status_);
    UpdateAddEnabled();
}

void EffectsPanelWidget::UpdateAddEnabled() {
    add_btn_->SetEnabled(!clip_id_.empty() && selected_ >= 0 &&
                         selected_ < static_cast<int>(filtered_.size()));
}

void EffectsPanelWidget::SetClip(const Clip* clip) {
    clip_id_ = clip == nullptr ? std::string() : clip->id;
    if (children_attached_) {
        UpdateAddEnabled();
        status_->SetText(clip == nullptr ? "Select a clip"
                                         : "Double-click or Add");
    }
    Invalidate();
}

void EffectsPanelWidget::RecomputeFilter() {
    const std::string needle =
        Lower(children_attached_ ? search_->GetValue() : std::string());
    filtered_.clear();
    const auto& catalog = Catalog();
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        if (needle.empty() ||
            Lower(catalog[i].type).find(needle) != std::string::npos ||
            Lower(catalog[i].description).find(needle) !=
                std::string::npos) {
            filtered_.push_back(static_cast<int>(i));
        }
    }
    content_height_ = kRowHeight * static_cast<float>(filtered_.size());
    // Clamp to the last page, not past it — Layout() uses the same
    // content_height - view formula.
    const float view_h = rows_bottom_ - rows_top_;
    scroll_y_ = std::clamp(
        scroll_y_, 0.0f,
        std::max(0.0f, content_height_ - std::max(view_h, 0.0f)));
    if (selected_ >= static_cast<int>(filtered_.size())) selected_ = -1;
    hovered_ = -1;
    if (children_attached_) UpdateAddEnabled();
}

int EffectsPanelWidget::RowAt(
    const XER::UI::Layout::Point& position) const {
    if (position.y < rows_top_ || position.y >= rows_bottom_) {
        return -1;
    }
    const int row = static_cast<int>(
        std::floor((position.y - rows_top_ + scroll_y_) / kRowHeight));
    return row >= 0 && row < static_cast<int>(filtered_.size()) ? row : -1;
}

void EffectsPanelWidget::AddSelected() {
    if (clip_id_.empty()) {
        SetStatus("Select a clip first", true);
        return;
    }
    if (selected_ < 0 || selected_ >= static_cast<int>(filtered_.size())) {
        SetStatus("Select an effect", true);
        return;
    }
    const EffectInfo& info =
        Catalog()[static_cast<std::size_t>(filtered_[static_cast<std::size_t>(selected_)])];
    if (hooks_.on_add_effect) {
        ClipEffect effect;
        effect.type = info.type;
        effect.params = info.default_params;
        hooks_.on_add_effect(clip_id_, effect);
    }
    SetStatus(std::string("added ") + info.type);
}

void EffectsPanelWidget::SetStatus(const std::string& text, bool is_error) {
    if (children_attached_) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void EffectsPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    if (filtered_.empty()) {
        renderer.DrawText(
            search_ != nullptr && !search_->GetValue().empty()
                ? "no matches"
                : "no effects",
            {bounds.x + 12.0f, rows_top_ + 6.0f, bounds.width - 24.0f,
             20.0f},
            XER::UI::Rendering::TextStyle{tokens.muted, tokens.font_small,
                                          400});
        return;
    }

    // Rows are drawn (not child widgets) so clicks land on the panel —
    // widget-tree Click events do not bubble up from Label children.
    if (rows_top_ <= 0.0f) return;   // not yet laid out
    const auto& catalog = Catalog();
    const float width = bounds.width - 24.0f;
    const float view_bottom = rows_bottom_;
    for (std::size_t i = 0; i < filtered_.size(); ++i) {
        const float y = rows_top_ + static_cast<float>(i) * kRowHeight -
                        scroll_y_;
        if (y + kRowHeight < rows_top_ || y > view_bottom) continue;
        const EffectInfo& info = catalog[static_cast<std::size_t>(filtered_[i])];
        const int idx = static_cast<int>(i);
        const XER::UI::Layout::Rect row{bounds.x + 8.0f, y, width,
                                        kRowHeight - 3.0f};
        if (idx == selected_) {
            renderer.FillRect(row, WithAlpha(tokens.accent, 0.16f),
                              tokens.radius_small);
            renderer.StrokeRect(row, WithAlpha(tokens.accent, 0.5f), 1.0f,
                                tokens.radius_small);
        } else if (idx == hovered_) {
            renderer.FillRect(row, tokens.hover, tokens.radius_small);
        }
        float text_x = row.x + 8.0f;
        if (DrawIcon(renderer, IconForEffectType(info.type),
                     static_cast<int>(kRowIcon), tokens.foreground,
                     {row.x + 6.0f,
                      row.y + (row.height - kRowIcon) * 0.5f,
                      kRowIcon, kRowIcon})) {
            text_x = row.x + 6.0f + kRowIcon + 8.0f;
        }
        renderer.DrawText(info.type,
                          {text_x, row.y + 3.0f,
                           row.x + row.width - text_x - 8.0f, 16.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.foreground, tokens.font_body, 500});
        renderer.DrawText(info.description,
                          {text_x, row.y + 20.0f,
                           row.x + row.width - text_x - 8.0f, 14.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});
    }
}

void EffectsPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;

    title_->Layout({bounds.x + pad, bounds.y + pad * 0.6f,
                    bounds.width - pad * 2.0f, 24.0f},
                   tokens);
    search_->Layout({bounds.x + pad, bounds.y + 34.0f,
                     bounds.width - pad * 2.0f, 30.0f},
                    tokens);
    rows_top_ = bounds.y + 34.0f + 30.0f + gap;
    const float footer_y = bounds.y + bounds.height - 30.0f;
    rows_bottom_ = footer_y - gap;
    add_btn_->Layout({bounds.x + pad, footer_y, 76.0f, 26.0f}, tokens);
    status_->Layout({bounds.x + pad + 84.0f, footer_y + 2.0f,
                     std::max(40.0f, bounds.width - pad * 2.0f - 84.0f),
                     22.0f},
                    tokens);
    // Keep the list viewport out of the footer.
    const float view_h = footer_y - rows_top_ - gap;
    scroll_y_ = std::clamp(
        scroll_y_, 0.0f,
        std::max(0.0f, content_height_ - std::max(view_h, 0.0f)));
}

bool EffectsPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    switch (event.type) {
        case XER::UI::Widgets::Event::Type::MouseWheel: {
            const float view_h = rows_bottom_ - rows_top_;
            const float range =
                std::max(0.0f, content_height_ - std::max(view_h, 0.0f));
            if (range <= 0.0f) return false;
            scroll_y_ = std::clamp(
                scroll_y_ - static_cast<float>(event.wheel_delta) * 0.5f,
                0.0f, range);
            Invalidate();
            return true;
        }
        case XER::UI::Widgets::Event::Type::MouseMove: {
            const int row = RowAt(event.position);
            if (row != hovered_) {
                hovered_ = row;
                Invalidate();
            }
            return false;
        }
        case XER::UI::Widgets::Event::Type::Click: {
            const int row = RowAt(event.position);
            if (row < 0) return false;
            const auto now = std::chrono::steady_clock::now();
            const bool double_click =
                row == last_click_row_ &&
                (now - last_click_time_) < kDoubleClickWindow;
            last_click_row_ = row;
            last_click_time_ = now;
            selected_ = row;
            UpdateAddEnabled();
            Invalidate();
            if (double_click) AddSelected();
            return true;
        }
        case XER::UI::Widgets::Event::Type::KeyDown: {
            if (event.key == XER::UI::Widgets::KeyCode::Enter) {
                AddSelected();
                return true;
            }
            if (event.key == XER::UI::Widgets::KeyCode::ArrowDown ||
                event.key == XER::UI::Widgets::KeyCode::ArrowUp) {
                if (!filtered_.empty()) {
                    const int count =
                        static_cast<int>(filtered_.size());
                    const int dir = event.key ==
                            XER::UI::Widgets::KeyCode::ArrowDown
                        ? 1
                        : -1;
                    selected_ = selected_ < 0
                        ? 0
                        : (selected_ + dir + count) % count;
                    // Keep the selection visible.
                    const float view_h = rows_bottom_ - rows_top_;
                    const float top =
                        static_cast<float>(selected_) * kRowHeight;
                    if (top < scroll_y_) scroll_y_ = top;
                    if (top + kRowHeight > scroll_y_ + view_h) {
                        scroll_y_ = top + kRowHeight - view_h;
                    }
                    UpdateAddEnabled();
                    Invalidate();
                }
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

void EffectsPanelWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
