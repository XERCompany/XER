// font_manager.cc — see font_manager.h for the contract.
// List convention follows effects_panel: Item rows inside a RowList
// under a ScrollArea; the manager rebuilds rows on filter/selection
// changes instead of keeping a parallel index map.
#include "font_manager.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "icon_helpers.h"
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

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
namespace Widgets = XER::UI::Widgets;

constexpr float kPanelWidth = 680.0f;
constexpr float kPanelHeight = 480.0f;
constexpr float kPanelPad = 22.0f;
constexpr float kListWidth = 270.0f;
constexpr float kRowHeight = 34.0f;

std::string LowerAscii(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

// The ScrollArea content: lays out its Item children stacked at
// kRowHeight and paints the selection/hover chrome behind them (the
// effects_panel RowList pattern, minus the filtered-index indirection
// — this list is rebuilt whenever its contents change).
class FontManager::RowList final : public XER::UI::Widgets::Widget {
public:
    RowList() { SetFocusable(false); }

    bool HandleEvent(
        const XER::UI::Widgets::Event& event) override {
        (void)event;
        return false;
    }

    // `selected` is a child index (-1 = none).
    void SetSelected(int selected) {
        if (selected_ == selected) return;
        selected_ = selected;
        Invalidate();
    }

    void Render(Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        const auto& children = GetChildren();
        for (std::size_t i = 0; i < children.size(); ++i) {
            const auto row = children[i]->GetBounds();
            if (static_cast<int>(i) == selected_) {
                renderer.FillRect(row, tokens.selection,
                                  tokens.radius_small);
                renderer.StrokeRect(
                    row, VeTheme::WithAlpha(tokens.accent, 0.5f), 1.0f,
                    tokens.radius_small);
            } else if (children[i]->IsHovered()) {
                renderer.FillRect(row, tokens.hover,
                                  tokens.radius_small);
            }
        }
    }

    void Layout(const XER::UI::Layout::Rect& bounds) override {
        SetBounds(bounds);
        const auto& tokens = CachedLayoutTokens();
        float y = bounds.y;
        for (const auto& child : GetChildren()) {
            child->Layout(
                {bounds.x + VeTheme::kSpaceSm, y + VeTheme::kSpaceXxs,
                 std::max(0.0f, bounds.width - VeTheme::kSpaceSm * 2.0f),
                 kRowHeight - VeTheme::kSpaceXs},
                tokens);
            y += kRowHeight;
        }
    }

    XER::UI::Layout::Size GetMinSize() const override {
        return {0.0f, 0.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {kListWidth,
                static_cast<float>(GetChildren().size()) * kRowHeight};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }
    float GetContentHeight() const override {
        return static_cast<float>(GetChildren().size()) * kRowHeight;
    }

private:
    int selected_ = -1;
};

FontManager::FontManager(Hooks hooks) : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(false);
}

void FontManager::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    VeIcons::InstallButtonIconResolver();

    title_ = std::make_shared<Widgets::Label>("Fonts");
    search_ = std::make_shared<Widgets::TextInput>();
    search_->SetPlaceholder("Search fonts\u2026");
    search_->SetOnValueChanged(
        [this](const std::string&) { RecomputeFilter(); });
    family_rows_ = std::make_shared<RowList>();
    family_scroll_ =
        std::make_shared<Widgets::ScrollArea>(family_rows_);
    style_rows_ = std::make_shared<RowList>();
    style_scroll_ =
        std::make_shared<Widgets::ScrollArea>(style_rows_);
    status_ = std::make_shared<Widgets::Label>(
        FontCatalog::Available()
            ? ""
            : "No font catalog (Skia-free build)",
        true);
    apply_button_ = std::make_shared<Widgets::Button>(
        "Use for selected text", Widgets::ButtonStyle::Primary);
    apply_button_->SetOnClick([this] { Apply(); });
    cancel_button_ = std::make_shared<Widgets::Button>(
        "Close", Widgets::ButtonStyle::Secondary);
    cancel_button_->SetOnClick([this] { Cancel(); });

    AddChild(title_);
    AddChild(search_);
    AddChild(family_scroll_);
    AddChild(style_scroll_);
    AddChild(status_);
    AddChild(apply_button_);
    AddChild(cancel_button_);
    RecomputeFilter();
}

void FontManager::SetSelection(const std::string& family, int weight,
                               bool italic) {
    EnsureChildren();
    const auto& families = FontCatalog::Families();
    selected_family_ = -1;
    if (!family.empty()) {
        for (std::size_t i = 0; i < families.size(); ++i) {
            if (families[i].name == family) {
                selected_family_ = static_cast<int>(i);
                break;
            }
        }
        // Case-insensitive fallback — clip params may spell the family
        // differently than the OS reports it.
        if (selected_family_ < 0) {
            const std::string needle = LowerAscii(family);
            for (std::size_t i = 0; i < families.size(); ++i) {
                if (LowerAscii(families[i].name) == needle) {
                    selected_family_ = static_cast<int>(i);
                    break;
                }
            }
        }
    }
    if (selected_family_ < 0 && !filtered_.empty()) {
        selected_family_ = filtered_.front();
    }
    RebuildStyleRows();
    // Pre-select the style closest to (weight, italic).
    if (selected_family_ >= 0) {
        const auto& styles = families[static_cast<std::size_t>(
                                     selected_family_)]
                                 .styles;
        int best = -1, best_score = 1 << 30;
        for (std::size_t i = 0; i < styles.size(); ++i) {
            const int score =
                std::abs(styles[i].weight - weight) +
                (styles[i].italic == italic ? 0 : 400);
            if (score < best_score) {
                best_score = score;
                best = static_cast<int>(i);
            }
        }
        SelectStyle(best);
    }
    // Scroll the family list so the selection is on screen: the
    // ScrollArea reads content height on the next Layout pass.
    Invalidate();
}

void FontManager::RecomputeFilter() {
    filtered_.clear();
    const std::string needle =
        LowerAscii(search_ ? search_->GetValue() : std::string());
    const auto& families = FontCatalog::Families();
    for (std::size_t i = 0; i < families.size(); ++i) {
        if (needle.empty() ||
            LowerAscii(families[i].name).find(needle) !=
                std::string::npos) {
            filtered_.push_back(static_cast<int>(i));
        }
    }
    if (selected_family_ >= 0 &&
        std::find(filtered_.begin(), filtered_.end(),
                  selected_family_) == filtered_.end()) {
        selected_family_ = filtered_.empty() ? -1 : filtered_.front();
        RebuildStyleRows();
    }
    // Rebuild the rows — Items are cheap enough to recreate per
    // keystroke, and a rebuilt list needs no filtered-index map.
    const auto& children = family_rows_->GetChildren();
    for (const auto& child :
         std::vector<std::shared_ptr<Widgets::Widget>>(
             children.begin(), children.end())) {
        family_rows_->RemoveChild(child);
    }
    int selected_pos = -1;
    for (std::size_t pos = 0; pos < filtered_.size(); ++pos) {
        const int index = filtered_[pos];
        const FontFamilyInfo& family =
            families[static_cast<std::size_t>(index)];
        auto item = std::make_shared<Widgets::Item>();
        item->SetTitle(family.name);
        item->SetDescription(
            std::to_string(family.styles.size()) + " styles");
        item->SetTooltip(family.name);
        item->SetOnClick([this, pos] {
            SelectFamily(static_cast<int>(pos));
        });
        if (index == selected_family_) {
            selected_pos = static_cast<int>(pos);
        }
        family_rows_->AddChild(std::move(item));
    }
    family_rows_->SetSelected(selected_pos);
    UpdateApplyEnabled();
    preview_dirty_ = true;
    RequestLayout();
    Invalidate();
}

void FontManager::SelectFamily(int filtered_pos) {
    if (filtered_pos < 0 ||
        filtered_pos >= static_cast<int>(filtered_.size())) {
        return;
    }
    const int index = filtered_[static_cast<std::size_t>(filtered_pos)];
    if (index == selected_family_) return;
    selected_family_ = index;
    family_rows_->SetSelected(filtered_pos);
    RebuildStyleRows();
    UpdateApplyEnabled();
    preview_dirty_ = true;
    Invalidate();
}

void FontManager::RebuildStyleRows() {
    const auto& children = style_rows_->GetChildren();
    for (const auto& child :
         std::vector<std::shared_ptr<Widgets::Widget>>(
             children.begin(), children.end())) {
        style_rows_->RemoveChild(child);
    }
    selected_style_ = -1;
    const auto& families = FontCatalog::Families();
    if (selected_family_ >= 0 &&
        selected_family_ < static_cast<int>(families.size())) {
        const auto& styles =
            families[static_cast<std::size_t>(selected_family_)]
                .styles;
        for (std::size_t i = 0; i < styles.size(); ++i) {
            auto item = std::make_shared<Widgets::Item>();
            item->SetTitle(styles[i].name);
            item->SetDescription(
                std::to_string(styles[i].weight) +
                (styles[i].italic ? " italic" : ""));
            item->SetOnClick([this, i] {
                SelectStyle(static_cast<int>(i));
            });
            style_rows_->AddChild(std::move(item));
        }
        if (!styles.empty()) {
            // Default to the regular face when present.
            int regular = 0;
            for (std::size_t i = 0; i < styles.size(); ++i) {
                if (styles[i].weight == 400 && !styles[i].italic) {
                    regular = static_cast<int>(i);
                    break;
                }
            }
            SelectStyle(regular);
        }
    }
}

void FontManager::SelectStyle(int index) {
    if (index == selected_style_) return;
    selected_style_ = index;
    style_rows_->SetSelected(index);
    preview_dirty_ = true;
    Invalidate();
}

const FontStyleInfo* FontManager::CurrentStyle() const {
    if (selected_family_ < 0 || selected_style_ < 0) return nullptr;
    const auto& families = FontCatalog::Families();
    if (selected_family_ >= static_cast<int>(families.size())) {
        return nullptr;
    }
    const auto& styles =
        families[static_cast<std::size_t>(selected_family_)].styles;
    if (selected_style_ >= static_cast<int>(styles.size())) {
        return nullptr;
    }
    return &styles[static_cast<std::size_t>(selected_style_)];
}

void FontManager::UpdateApplyEnabled() {
    if (apply_button_) {
        apply_button_->SetEnabled(selected_family_ >= 0);
    }
}

void FontManager::RefreshPreview() {
    preview_pixels_.clear();
    preview_w_ = preview_h_ = 0;
    preview_label_.clear();
    const FontStyleInfo* style = CurrentStyle();
    if (style == nullptr) return;
    const std::string& family = FontCatalog::Families()
        [static_cast<std::size_t>(selected_family_)]
            .name;
    preview_label_ = family + " — " + style->name;
    FontCatalog::RenderPreview(
        family, style->weight, style->italic,
        "The quick brown fox 0123", 28.0f, &preview_pixels_,
        &preview_w_, &preview_h_);
}

void FontManager::Apply() {
    const FontStyleInfo* style = CurrentStyle();
    if (style == nullptr || !hooks_.on_apply) return;
    hooks_.on_apply(
        FontCatalog::Families()
            [static_cast<std::size_t>(selected_family_)]
                .name,
        style->weight, style->italic);
}

void FontManager::Cancel() {
    if (hooks_.on_cancel) hooks_.on_cancel();
}

Layout::Rect FontManager::PanelRect() const {
    const auto bounds = GetBounds();
    const float width = std::min(kPanelWidth, bounds.width - 40.0f);
    const float height =
        std::min(kPanelHeight, bounds.height - 40.0f);
    return {bounds.x + (bounds.width - width) * 0.5f,
            bounds.y + (bounds.height - height) * 0.5f, width, height};
}

Layout::Rect FontManager::PreviewRect() const {
    const auto panel = PanelRect();
    const float rx = panel.x + kPanelPad + kListWidth + 14.0f;
    return {rx, panel.y + 48.0f,
            panel.x + panel.width - kPanelPad - rx, 118.0f};
}

void FontManager::Render(Rendering::Renderer& renderer,
                         const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, VeTheme::kScrim, 0.0f);
    const auto panel = PanelRect();
    renderer.FillRect(panel, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(panel, tokens.border, 1.0f, tokens.radius_large);
    renderer.FillRect({panel.x, panel.y, 4.0f, panel.height},
                      tokens.accent, tokens.radius_large);

    // Live preview card: sunken field + the Skia-rendered sample line.
    const auto preview = PreviewRect();
    renderer.FillRect(preview, tokens.input, tokens.radius_small);
    renderer.StrokeRect(preview, tokens.border, 1.0f,
                        tokens.radius_small);
    if (preview_dirty_) {
        preview_dirty_ = false;
        RefreshPreview();
    }
    if (!preview_pixels_.empty() && preview_w_ > 0 && preview_h_ > 0) {
        const float scale =
            std::min(1.0f,
                     std::min((preview.width - 16.0f) / preview_w_,
                              (preview.height - 34.0f) / preview_h_));
        const float dw = preview_w_ * scale;
        const float dh = preview_h_ * scale;
        const Layout::Rect dst{
            preview.x + 8.0f +
                std::max(0.0f, (preview.width - 16.0f - dw) * 0.5f),
            preview.y + 8.0f +
                std::max(0.0f, (preview.height - 34.0f - dh) * 0.5f),
            dw, dh};
        renderer.DrawFrameBgra(preview_pixels_.data(), preview_w_,
                               preview_h_, dst);
    }
    if (!preview_label_.empty()) {
        renderer.DrawText(
            preview_label_,
            {preview.x + 8.0f,
             preview.y + preview.height - 24.0f,
             preview.width - 16.0f, 16.0f},
            {tokens.muted, tokens.font_small, 500});
    }
}

void FontManager::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const auto panel = PanelRect();
    const float px = panel.x + kPanelPad;
    const float pw = panel.width - kPanelPad * 2.0f;

    title_->Layout({px, panel.y + 14.0f, pw, 26.0f}, tokens);
    search_->Layout(
        {px, panel.y + 46.0f, kListWidth, 32.0f}, tokens);

    const float list_top = panel.y + 86.0f;
    const float list_bottom = panel.y + panel.height - 58.0f;
    family_scroll_->Layout(
        {px, list_top, kListWidth, list_bottom - list_top}, tokens);

    const float rx = px + kListWidth + 14.0f;
    const float rw = panel.x + panel.width - kPanelPad - rx;
    // Preview rect matches PreviewRect(); styles list below it.
    const float style_top = panel.y + 48.0f + 118.0f + 10.0f;
    style_scroll_->Layout(
        {rx, style_top, rw, list_bottom - style_top}, tokens);
    status_->Layout({rx, list_bottom + 2.0f, rw, 18.0f}, tokens);

    const float button_y = panel.y + panel.height - 48.0f;
    cancel_button_->Layout({panel.x + panel.width - kPanelPad - 94.0f,
                            button_y, 94.0f, 34.0f},
                           tokens);
    apply_button_->Layout({panel.x + panel.width - kPanelPad - 94.0f -
                               176.0f - 8.0f,
                           button_y, 176.0f, 34.0f},
                          tokens);
}

bool FontManager::HandleEvent(const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;
    if (event.type == Event::Type::KeyDown) {
        if (event.key == XER::UI::Widgets::KeyCode::Escape) {
            Cancel();
            return true;
        }
        if (event.key == XER::UI::Widgets::KeyCode::Enter) {
            Apply();
            return true;
        }
    }
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        if (!PanelRect().Contains(event.position)) {
            Cancel();
            return true;
        }
    }
    return event.type == Event::Type::MouseDown ||
           event.type == Event::Type::MouseUp ||
           event.type == Event::Type::Click;
}

void FontManager::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
