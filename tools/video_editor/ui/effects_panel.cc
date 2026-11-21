#include "effects_panel.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "app_runtime/app_runtime.h"
#include "icon_helpers.h"
#include "js_fx.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/item.h"
#include "widgets/label.h"
#include "widgets/scroll_area.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor::Ui {
namespace {

// Two-line catalog row: a body-size title line over a caption-size
// subtitle — the height is the VeTheme type ramp + a tight inner gap.
constexpr float kRowHeight = VeTheme::kFontBody * 1.4f +
                             VeTheme::kFontCaption * 1.4f +
                             VeTheme::kSpaceXs;
constexpr float kRowIcon = VeTheme::kSpaceLg;
constexpr float kActionWidth = 76.0f;   // "Add" button

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

// Leading-slot glyph for a row Item: draws its logical pack icon
// centered in the slot. Disabled so hit-testing skips it (the same
// convention as MarkersPanelWidget::MarkerSwatch) — row clicks and
// hover land on the Item itself.
class RowIcon final : public XER::UI::Widgets::Widget {
public:
    explicit RowIcon(const char* name) : name_(name) {
        SetFocusable(false);
        SetEnabled(false);
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        VeIcons::DrawIcon(renderer, name_.c_str(),
                          static_cast<int>(kRowIcon), tokens.foreground,
                          GetBounds());
    }
    void Layout(const XER::UI::Layout::Rect& bounds) override {
        SetBounds(bounds);
    }
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }
    XER::UI::Layout::Size GetMinSize() const override {
        return {kRowIcon, kRowIcon};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {kRowIcon, kRowIcon};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {kRowIcon, kRowIcon};
    }

private:
    std::string name_;
};

}  // namespace

// The ScrollArea's content: stacks the filtered Item rows at
// kRowHeight and paints the selection/hover chrome behind them —
// children render after this widget's own pass, so the chrome reads
// as the rows' background exactly like the old drawn rows.
// Deliberately not focusable: click focus must climb to the panel so
// arrow-key navigation keeps working after a row click.
class EffectsPanelWidget::RowList final : public XER::UI::Widgets::Widget {
public:
    RowList() {
        SetFocusable(false);
    }

    // One Item per catalog entry, called once — the rows live as
    // children; filtering only decides which ones get laid out.
    void SetItems(
        std::vector<std::shared_ptr<XER::UI::Widgets::Item>> items) {
        items_ = std::move(items);
        for (const auto& item : items_) {
            AddChild(item);
        }
        RequestLayout();
        Invalidate();
    }

    // Display order → items_ index (the panel's filtered_ vector).
    void SetFiltered(std::vector<int> filtered) {
        filtered_ = std::move(filtered);
        RequestLayout();
        Invalidate();
    }

    // Selected position inside filtered_ (-1 = none).
    void SetSelected(int filtered_pos) {
        if (selected_ == filtered_pos) return;
        selected_ = filtered_pos;
        Invalidate();
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        for (std::size_t i = 0; i < filtered_.size(); ++i) {
            const auto& item = items_[static_cast<std::size_t>(filtered_[i])];
            const auto row = item->GetBounds();
            if (static_cast<int>(i) == selected_) {
                renderer.FillRect(row, tokens.selection,
                                  tokens.radius_small);
                renderer.StrokeRect(
                    row, VeTheme::WithAlpha(tokens.accent, 0.5f), 1.0f,
                    tokens.radius_small);
            } else if (item->IsHovered()) {
                renderer.FillRect(row, tokens.hover, tokens.radius_small);
            }
        }
    }

    void Layout(const XER::UI::Layout::Rect& bounds) override {
        SetBounds(bounds);
        const auto& tokens = CachedLayoutTokens();
        // Rows outside the filter keep their old bounds — hide them so
        // they neither paint nor hit-test.
        for (const auto& item : items_) {
            item->SetVisible(false);
        }
        float y = bounds.y;
        for (const int index : filtered_) {
            const auto& item = items_[static_cast<std::size_t>(index)];
            item->SetVisible(true);
            item->Layout(
                {bounds.x + VeTheme::kSpaceSm, y + VeTheme::kSpaceXxs,
                 std::max(0.0f, bounds.width - VeTheme::kSpaceSm * 2.0f),
                 kRowHeight - VeTheme::kSpaceXs},
                tokens);
            y += kRowHeight;
        }
    }
    using Widget::Layout;

    // Rows consume their own clicks (Item::SetOnClick); everything
    // else — gaps, edges — stays transparent so the tree's bubbling
    // reaches whatever wants it.
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }

    XER::UI::Layout::Size GetMinSize() const override {
        return {0.0f, 0.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {0.0f, GetContentHeight()};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }
    float GetContentHeight() const override {
        return kRowHeight * static_cast<float>(filtered_.size());
    }

private:
    std::vector<std::shared_ptr<XER::UI::Widgets::Item>> items_;
    std::vector<int> filtered_;
    int selected_ = -1;
};

EffectsPanelWidget::~EffectsPanelWidget() = default;

const std::vector<EffectsPanelWidget::EffectInfo>&
EffectsPanelWidget::Catalog() {
    // type names + default params mirror the dispatch table and its
    // documented fallbacks in compositor.cc ApplyClipEffects; keep in
    // sync when new effects land there. The JS fx catalog
    // (assets/scripts/fx/fx.json) is merged in once — LoadCatalog is
    // idempotent and this runs on the UI thread.
    static const std::vector<EffectInfo> catalog = [] {
        std::vector<EffectInfo> list = {
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
        {"lens_sphere", {0.45f, 1.4f, 3.0f, 0.35f, 0.6f},
         "crystal ball · radius, bend, chroma, edge, glint"},
        {"magnifier", {0.3f, 1.8f, 2.0f},
         "lens bulge · radius frac, zoom, chroma"},
        {"ribbed_glass", {24.0f, 6.0f, 0.0f, 0.5f},
         "fluted pane · period px, amp px, axis, softness"},
        {"bokeh_dof", {1.0f, 0.5f, 0.5f, 0.12f, 0.3f, 18.0f, 3.0f},
         "depth of field · mode, cx, cy, band, falloff, radius, gain"},
        {"bokeh_blur", {10.0f, 2.5f},
         "aperture discs · radius px, highlight boost"},
        };
        if (!JsFxRuntime::FxDir().empty() ||
            JsFxRuntime::LoadCatalog(
                (AppRuntime::SharedAssetsConfig().assets_dir /
                 "scripts" / "fx").string(),
                nullptr)) {
            for (const auto& d : JsFxRuntime::VideoEffects()) {
                EffectInfo info;
                info.type = "script:" + d.id;
                for (const auto& pd : d.params) {
                    info.default_params.push_back(pd.def);
                }
                info.description =
                    d.title + " · script (JS)";
                list.push_back(std::move(info));
            }
        }
        return list;
    }();
    return catalog;
}

EffectsPanelWidget::EffectsPanelWidget(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);   // the list scrolls inside the panel
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

    // The scrollable list: one Item per catalog entry with a leading
    // pack icon. Clicks select, double-clicks apply (Item hooks) —
    // the panel keeps keyboard nav through its focused state, which
    // click-bubbling restores to it after a row consumes the click.
    list_ = std::make_shared<RowList>();
    const auto& catalog = Catalog();
    items_.reserve(catalog.size());
    for (std::size_t c = 0; c < catalog.size(); ++c) {
        const EffectInfo& info = catalog[c];
        auto item = std::make_shared<Widgets::Item>();
        item->SetTitle(info.type);
        item->SetDescription(info.description);
        item->SetTooltip(info.description);
        item->AddChild(
            std::make_shared<RowIcon>(IconForEffectType(info.type)));
        const int index = static_cast<int>(c);
        item->SetOnClick([this, index] { SelectCatalogRow(index); });
        item->SetOnDoubleClick([this] { AddSelected(); });
        items_.push_back(std::move(item));
    }
    list_->SetItems(items_);
    list_->SetFiltered(filtered_);
    list_->SetSelected(selected_);
    scroll_area_ = std::make_shared<Widgets::ScrollArea>(list_);

    AddChild(title_);
    AddChild(search_);
    AddChild(scroll_area_);
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
    if (selected_ >= static_cast<int>(filtered_.size())) selected_ = -1;
    if (children_attached_) {
        list_->SetFiltered(filtered_);
        list_->SetSelected(selected_);
        UpdateAddEnabled();
        // The content height changed — the ScrollArea only re-reads it
        // (and re-clamps its offset) during layout.
        RequestLayout();
    }
    Invalidate();
}

void EffectsPanelWidget::SelectRow(int filtered_pos) {
    if (selected_ == filtered_pos) return;
    selected_ = filtered_pos;
    UpdateAddEnabled();
    if (children_attached_) list_->SetSelected(selected_);
    Invalidate();
}

void EffectsPanelWidget::SelectCatalogRow(int catalog_index) {
    for (std::size_t i = 0; i < filtered_.size(); ++i) {
        if (filtered_[i] == catalog_index) {
            SelectRow(static_cast<int>(i));
            return;
        }
    }
}

void EffectsPanelWidget::EnsureSelectionVisible() {
    if (scroll_area_ == nullptr || selected_ < 0) return;
    const float view_h = scroll_area_->GetBounds().height;
    const float top = static_cast<float>(selected_) * kRowHeight;
    const float scroll = scroll_area_->GetScrollY();
    if (top < scroll) {
        scroll_area_->ScrollTo(top);
    } else if (top + kRowHeight > scroll + view_h) {
        scroll_area_->ScrollTo(top + kRowHeight - view_h);
    }
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
        // The hint sits where the list would — the ScrollArea's rect
        // once laid out, the same offset arithmetic otherwise.
        const auto list_bounds =
            scroll_area_ != nullptr ? scroll_area_->GetBounds()
                                    : XER::UI::Layout::Rect{};
        const float hint_y = list_bounds.height > 0.0f
            ? list_bounds.y + VeTheme::kSpaceXs
            : bounds.y + VeTheme::kSpaceMd * 3.0f;
        renderer.DrawText(
            search_ != nullptr && !search_->GetValue().empty()
                ? "no matches"
                : "no effects",
            {bounds.x + VeTheme::kSpaceMd, hint_y,
             bounds.width - VeTheme::kSpaceMd * 2.0f,
             tokens.font_body * 1.4f},
            XER::UI::Rendering::TextStyle{tokens.muted, tokens.font_small,
                                          400});
    }
    // Rows paint themselves — Item children inside the ScrollArea;
    // the RowList draws selection/hover chrome behind them.
}

void EffectsPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;
    const float inner_x = bounds.x + pad;
    const float inner_w = bounds.width - pad * 2.0f;
    float y = bounds.y + pad * 0.6f;

    title_->Layout({inner_x, y, inner_w, VeTheme::kRowHeight}, tokens);
    y += VeTheme::kRowHeight + VeTheme::kSpaceXxs;
    search_->Layout({inner_x, y, inner_w, VeTheme::kControlHeight},
                    tokens);
    y += VeTheme::kControlHeight + gap;

    const float footer_h = VeTheme::kControlHeight;
    const float footer_y =
        bounds.y + bounds.height - footer_h - VeTheme::kSpaceXs;
    scroll_area_->Layout(
        {inner_x, y, inner_w, std::max(0.0f, footer_y - gap - y)},
        tokens);
    add_btn_->Layout({inner_x, footer_y, kActionWidth, footer_h}, tokens);
    status_->Layout(
        {inner_x + kActionWidth + gap, footer_y + VeTheme::kSpaceXxs,
         std::max(40.0f, inner_w - kActionWidth - gap),
         footer_h - VeTheme::kSpaceXs},
        tokens);
}

bool EffectsPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    // Row clicks are consumed by the Items themselves; this panel only
    // still owns keyboard navigation. Clicks that bubble up from dead
    // children land in `default` and stay unhandled — exactly the
    // behavior the drawn list had on chrome/gap clicks.
    if (event.type == XER::UI::Widgets::Event::Type::KeyDown) {
        if (event.key == XER::UI::Widgets::KeyCode::Enter) {
            AddSelected();
            return true;
        }
        if (event.key == XER::UI::Widgets::KeyCode::ArrowDown ||
            event.key == XER::UI::Widgets::KeyCode::ArrowUp) {
            if (!filtered_.empty()) {
                const int count = static_cast<int>(filtered_.size());
                const int dir =
                    event.key == XER::UI::Widgets::KeyCode::ArrowDown
                        ? 1
                        : -1;
                const int next = selected_ < 0
                    ? 0
                    : (selected_ + dir + count) % count;
                SelectRow(next);
                EnsureSelectionVisible();
            }
            return true;
        }
    }
    return false;
}

void EffectsPanelWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
