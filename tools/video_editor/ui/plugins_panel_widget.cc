#include "plugins_panel_widget.h"

#include <algorithm>
#include <utility>

#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/item.h"
#include "widgets/label.h"
#include "widgets/scroll_area.h"

namespace XER::VideoEditor::Ui {
namespace {

// Two-line catalog row: body-size name over a caption-size vendor tag.
constexpr float kRowHeight = VeTheme::kFontBody * 1.4f +
                             VeTheme::kFontCaption * 1.4f +
                             VeTheme::kSpaceXs;
constexpr float kHeaderHeight = VeTheme::kRowHeight;
constexpr float kHeaderIcon = VeTheme::kFontBody;
constexpr float kRowIcon = VeTheme::kSpaceLg;
constexpr float kActionWidth = 100.0f;   // "Add to Clip" button

std::string TagFor(const PluginDesc& desc) {
    if (desc.vendor.empty()) return desc.file;
    if (desc.file.empty()) return desc.vendor;
    return desc.vendor + " · " + desc.file;
}

// Leading-slot glyph for a row Item: draws its section's pack icon
// centered in the slot. Disabled so hit-testing skips it (the same
// convention as MarkersPanelWidget::MarkerSwatch) — row clicks and
// hover land on the Item itself.
class RowIcon final : public XER::UI::Widgets::Widget {
public:
    explicit RowIcon(std::string name) : name_(std::move(name)) {
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

// The ScrollArea's content: drawn section headers interleaved with
// Item rows, plus the selection/hover chrome painted behind rows —
// children render after this widget's own pass, so the chrome reads
// as the rows' background exactly like the old drawn list.
// Deliberately not focusable: click focus must climb to the panel so
// arrow-key navigation keeps working after a row click.
class PluginsPanelWidget::RowList final : public XER::UI::Widgets::Widget {
public:
    RowList() {
        SetFocusable(false);
    }

    // One display line: either a drawn header (icon + muted caption)
    // or a row Item.
    struct Line {
        bool header = false;
        std::string header_text;
        std::string header_icon;
        std::shared_ptr<XER::UI::Widgets::Item> item;   // rows only
    };

    // Adopts the row Items as children and swaps the line model.
    void SetLines(std::vector<Line> lines) {
        // Retire the previous generation — disable first so a stale
        // mouse capture can't fire a detached row, then keep them
        // alive one rebuild (the MarkersPanelWidget pattern).
        retired_.clear();
        for (Line& line : lines_) {
            if (line.item == nullptr) continue;
            line.item->SetEnabled(false);
            RemoveChild(line.item);
            retired_.push_back(line.item);
        }
        lines_ = std::move(lines);
        for (const Line& line : lines_) {
            if (line.item != nullptr) AddChild(line.item);
        }
        RequestLayout();
        Invalidate();
    }

    // Selected index into lines_ (-1 = none); header indices are
    // simply never passed.
    void SetSelected(int index) {
        if (selected_ == index) return;
        selected_ = index;
        Invalidate();
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        const float sub_h = tokens.font_small * 1.4f;
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            const Line& line = lines_[i];
            const auto& r = line_rects_[i];
            if (line.header) {
                // Section header — the inspector's drawn-label
                // convention: muted, small, weight 600, centered.
                float label_x = r.x + VeTheme::kSpaceMd;
                if (!line.header_icon.empty() &&
                    VeIcons::DrawIcon(
                        renderer, line.header_icon.c_str(),
                        static_cast<int>(kHeaderIcon), tokens.muted,
                        {label_x,
                         r.y + (kHeaderHeight - kHeaderIcon) * 0.5f,
                         kHeaderIcon, kHeaderIcon})) {
                    label_x += kHeaderIcon + VeTheme::kSpaceXs;
                }
                renderer.DrawText(
                    line.header_text,
                    {label_x, r.y + (kHeaderHeight - sub_h) * 0.5f,
                     r.x + r.width - label_x - VeTheme::kSpaceMd, sub_h},
                    XER::UI::Rendering::TextStyle{
                        tokens.muted, tokens.font_small, 600});
                continue;
            }
            const auto row = line.item->GetBounds();
            if (static_cast<int>(i) == selected_) {
                renderer.FillRect(row, tokens.selection,
                                  tokens.radius_small);
                renderer.StrokeRect(
                    row, VeTheme::WithAlpha(tokens.accent, 0.5f), 1.0f,
                    tokens.radius_small);
            } else if (line.item->IsHovered()) {
                renderer.FillRect(row, tokens.hover, tokens.radius_small);
            }
        }
    }

    void Layout(const XER::UI::Layout::Rect& bounds) override {
        SetBounds(bounds);
        const auto& tokens = CachedLayoutTokens();
        line_rects_.clear();
        line_rects_.reserve(lines_.size());
        float y = bounds.y;
        for (Line& line : lines_) {
            const float h = line.header ? kHeaderHeight : kRowHeight;
            line_rects_.push_back({bounds.x, y, bounds.width, h});
            if (line.item != nullptr) {
                line.item->Layout(
                    {bounds.x + VeTheme::kSpaceSm,
                     y + VeTheme::kSpaceXxs,
                     std::max(0.0f,
                              bounds.width - VeTheme::kSpaceSm * 2.0f),
                     kRowHeight - VeTheme::kSpaceXs},
                    tokens);
            }
            y += h;
        }
    }
    using Widget::Layout;

    // Rows consume their own clicks (Item::SetOnClick); headers and
    // gaps stay transparent so clicks bubble like the drawn list's.
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
        float h = 0.0f;
        for (const Line& line : lines_) {
            h += line.header ? kHeaderHeight : kRowHeight;
        }
        return h;
    }

private:
    std::vector<Line> lines_;
    // Row Items removed by the last SetLines — kept alive one rebuild
    // in case an event was mid-dispatch through them.
    std::vector<std::shared_ptr<XER::UI::Widgets::Item>> retired_;
    std::vector<XER::UI::Layout::Rect> line_rects_;
    int selected_ = -1;
};

PluginsPanelWidget::~PluginsPanelWidget() = default;

PluginsPanelWidget::PluginsPanelWidget(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);   // the list scrolls inside the panel
    Reload();
}

void PluginsPanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    title_ = std::make_shared<Widgets::Label>("PLUGINS");
    add_btn_ = std::make_shared<Widgets::Button>(
        "Add to Clip", Widgets::ButtonStyle::Primary);
    add_btn_->SetTooltip("Add the selected plugin effect to the clip");
    add_btn_->SetOnClick([this] { AddSelected(); });
    status_ = std::make_shared<Widgets::Label>("Select a plugin", true);

    list_ = std::make_shared<RowList>();
    scroll_area_ = std::make_shared<Widgets::ScrollArea>(list_);

    AddChild(title_);
    AddChild(scroll_area_);
    AddChild(add_btn_);
    AddChild(status_);
    status_->SetText(entries_.empty() ? "No plugins found"
                                    : "Select a plugin");
    RebuildList();
    UpdateAddEnabled();
}

void PluginsPanelWidget::Reload() {
    // Keep the selection across a rescan when its id still exists.
    const std::string keep =
        selected_ >= 0 && selected_ < static_cast<int>(entries_.size())
            ? entries_[static_cast<std::size_t>(selected_)].id
            : std::string();

    entries_.clear();
    int effect_count = 0;
    const auto& effects = SharedPluginHost().Effects();
    // Header entries stash their group icon name (assets/icons.yaml)
    // in `tag` — headers never render a tag line, and the icon names
    // double as the section icon for the rows that follow.
    const auto append_kind = [&](XER_PluginKind kind, const char* label,
                                 const char* icon) {
        bool headed = false;
        for (const PluginDesc& desc : effects) {
            if (desc.kind != kind) continue;
            if (!headed) {
                entries_.push_back({true, label, icon, {}});
                headed = true;
            }
            entries_.push_back({false, desc.name, TagFor(desc), desc.id});
            ++effect_count;
        }
    };
    // Section labels uppercase — the inspector's drawn-header style.
    append_kind(XER_PLUGIN_VIDEO_FX, "VIDEO EFFECTS", "nav.fx");
    append_kind(XER_PLUGIN_AUDIO_FX, "AUDIO EFFECTS", "nav.audio");

    selected_ = -1;
    if (!keep.empty()) {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (!entries_[i].header && entries_[i].id == keep) {
                selected_ = static_cast<int>(i);
                break;
            }
        }
    }
    if (children_attached_) {
        RebuildList();
        UpdateAddEnabled();
        status_->SetText(effect_count == 0
                             ? "No plugins found"
                             : std::to_string(effect_count) + " effect(s)");
    }
    Invalidate();
}

void PluginsPanelWidget::RebuildList() {
    std::vector<RowList::Line> lines;
    lines.reserve(entries_.size());
    // The section icon travels from the header into each row's leading
    // slot — same sharing the drawn list had via section_icon.
    std::string section_icon;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& entry = entries_[i];
        if (entry.header) {
            section_icon = entry.tag;
            lines.push_back({true, entry.text, entry.tag, nullptr});
            continue;
        }
        auto item = std::make_shared<XER::UI::Widgets::Item>();
        item->SetTitle(entry.text);
        item->SetDescription(entry.tag);
        item->SetTooltip(entry.text);
        item->AddChild(std::make_shared<RowIcon>(
            section_icon.empty() ? "tab.plugins" : section_icon));
        const int index = static_cast<int>(i);
        item->SetOnClick([this, index] { SelectRow(index); });
        item->SetOnDoubleClick([this] { AddSelected(); });
        lines.push_back({false, {}, {}, std::move(item)});
    }
    list_->SetLines(std::move(lines));
    list_->SetSelected(selected_);
}

void PluginsPanelWidget::UpdateAddEnabled() {
    add_btn_->SetEnabled(
        selected_ >= 0 &&
        selected_ < static_cast<int>(entries_.size()) &&
        !entries_[static_cast<std::size_t>(selected_)].header);
}

float PluginsPanelWidget::EntryHeight(int index) const {
    return entries_[static_cast<std::size_t>(index)].header
               ? kHeaderHeight
               : kRowHeight;
}

float PluginsPanelWidget::EntryTop(int index) const {
    float y = 0.0f;
    for (int i = 0; i < index; ++i) y += EntryHeight(i);
    return y;
}

void PluginsPanelWidget::SelectRow(int entry_index) {
    if (selected_ == entry_index) return;
    selected_ = entry_index;
    UpdateAddEnabled();
    if (children_attached_) list_->SetSelected(selected_);
    Invalidate();
}

void PluginsPanelWidget::EnsureSelectionVisible() {
    if (scroll_area_ == nullptr || selected_ < 0) return;
    const float view_h = scroll_area_->GetBounds().height;
    const float top = EntryTop(selected_);
    const float bottom = top + EntryHeight(selected_);
    const float scroll = scroll_area_->GetScrollY();
    if (top < scroll) {
        scroll_area_->ScrollTo(top);
    } else if (bottom > scroll + view_h) {
        scroll_area_->ScrollTo(bottom - view_h);
    }
}

void PluginsPanelWidget::AddSelected() {
    if (selected_ < 0 ||
        selected_ >= static_cast<int>(entries_.size()) ||
        entries_[static_cast<std::size_t>(selected_)].header) {
        SetStatus("Select a plugin");
        return;
    }
    const Entry& entry = entries_[static_cast<std::size_t>(selected_)];
    // Audio plugins can't ride the clip effect chain — the compositor
    // video path silently no-ops them (PluginDesc::video_fn == nullptr).
    // The Media Bin bounce workflow is the supported path for audio fx.
    if (const PluginDesc* desc = SharedPluginHost().Find(entry.id);
        desc != nullptr && desc->kind == XER_PLUGIN_AUDIO_FX) {
        SetStatus("audio fx — use Media Bin bounce instead");
        return;
    }
    if (hooks_.on_add_effect) {
        hooks_.on_add_effect("plugin:" + entry.id);
    }
    SetStatus("added " + entry.text);
}

void PluginsPanelWidget::SetStatus(const std::string& text) {
    if (children_attached_) status_->SetText(text);
}

void PluginsPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    if (entries_.empty()) {
        const auto list_bounds =
            scroll_area_ != nullptr ? scroll_area_->GetBounds()
                                    : XER::UI::Layout::Rect{};
        const float hint_y = list_bounds.height > 0.0f
            ? list_bounds.y + VeTheme::kSpaceXs
            : bounds.y + VeTheme::kSpaceMd * 3.0f + VeTheme::kSpaceXs;
        renderer.DrawText(
            "No plugins found in <exe>/plugins",
            {bounds.x + VeTheme::kSpaceMd, hint_y,
             bounds.width - VeTheme::kSpaceMd * 2.0f,
             tokens.font_body * 1.4f},
            XER::UI::Rendering::TextStyle{tokens.muted, tokens.font_small,
                                          400});
    }
    // Rows paint themselves — Item children inside the ScrollArea;
    // the RowList draws headers + selection/hover chrome.
}

void PluginsPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;
    const float inner_x = bounds.x + pad;
    const float inner_w = bounds.width - pad * 2.0f;
    float y = bounds.y + pad * 0.6f;

    title_->Layout({inner_x, y, inner_w, VeTheme::kRowHeight}, tokens);
    y += VeTheme::kRowHeight + gap;

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

bool PluginsPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    // Row clicks are consumed by the Items themselves; this panel only
    // still owns keyboard navigation. Clicks bubbling up from dead
    // children land in `default` and stay unhandled — the same dead
    // zone the drawn list's gaps were.
    if (event.type == XER::UI::Widgets::Event::Type::KeyDown) {
        if (event.key == XER::UI::Widgets::KeyCode::Enter) {
            AddSelected();
            return true;
        }
        if (event.key == XER::UI::Widgets::KeyCode::ArrowDown ||
            event.key == XER::UI::Widgets::KeyCode::ArrowUp) {
            if (!entries_.empty()) {
                const int count = static_cast<int>(entries_.size());
                const int dir =
                    event.key == XER::UI::Widgets::KeyCode::ArrowDown
                        ? 1
                        : -1;
                // Step over section headers until a row is found.
                int i = selected_;
                for (int step = 0; step < count; ++step) {
                    i = i < 0 ? (dir > 0 ? 0 : count - 1)
                              : (i + dir + count) % count;
                    if (!entries_[static_cast<std::size_t>(i)].header) {
                        SelectRow(i);
                        break;
                    }
                }
                EnsureSelectionVisible();
            }
            return true;
        }
    }
    return false;
}

void PluginsPanelWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
