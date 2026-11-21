#include "color_panel_widget.h"

#include <algorithm>
#include <utility>

#include "app_runtime/app_runtime.h"
#include "helper/path_utils.h"
#include "icon_helpers.h"
#include "lut_manager.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/item.h"
#include "widgets/label.h"
#include "widgets/scroll_area.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Ocio = XER::VideoEditor::Color;
namespace Path = XER::Helper::Path;

// Two-line catalog row: body-size name over a caption-size subtitle.
constexpr float kRowHeight = VeTheme::kFontBody * 1.4f +
                             VeTheme::kFontCaption * 1.4f +
                             VeTheme::kSpaceXs;
constexpr float kHeaderHeight = VeTheme::kRowHeight;
constexpr float kHeaderIcon = VeTheme::kFontBody;
constexpr float kRowIcon = VeTheme::kSpaceLg;
constexpr float kActionWidth = 100.0f;   // "Add to Clip" button

// Last path segment of `path` — color.yaml LUT paths may use either
// separator style regardless of platform.
std::string Basename(const std::string& path) {
    const std::string::size_type pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

// Row subtitle: the LUT file name for looks, or the effective
// colorspace chain — empty profile fields inherit the config's
// source/display/view defaults.
std::string TagFor(const Ocio::OcioProfile& profile,
                   const Ocio::OcioConfig& cfg) {
    if (!profile.lut_path.empty()) {
        std::string tag = Basename(profile.lut_path);
        // LUT-manager entries carry their parsed size — "file · 33³"
        // for a 3D LUT, "file · 1D" for a one-dimensional table.
        if (const Lut::Entry* entry =
                Lut::Manager::Shared().FindByPath(profile.lut_path)) {
            if (entry->size > 0) {
                tag += entry->is_3d
                           ? " \xc2\xb7 " + std::to_string(entry->size) +
                                 "\xc2\xb3"
                           : " \xc2\xb7 1D";
            }
        }
        return tag;
    }
    const std::string& src =
        profile.source.empty() ? cfg.source : profile.source;
    const std::string& dst =
        profile.display.empty() ? cfg.display : profile.display;
    const std::string& view =
        profile.view.empty() ? cfg.view : profile.view;
    return src + " → " + dst + "/" + view;
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
class ColorPanelWidget::RowList final : public XER::UI::Widgets::Widget {
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

std::shared_ptr<ColorPanelWidget> ColorPanelWidget::Create() {
    return std::shared_ptr<ColorPanelWidget>(new ColorPanelWidget());
}

ColorPanelWidget::~ColorPanelWidget() = default;

ColorPanelWidget::ColorPanelWidget() {
    SetFocusable(true);
    SetClipsChildren(true);   // the list scrolls inside the panel
    Refresh();
}

void ColorPanelWidget::SetOnAddEffect(
    std::function<void(const std::string&)> cb) {
    on_add_effect_ = std::move(cb);
}

void ColorPanelWidget::SetOnSelectionChanged(
    std::function<void(const std::string&)> cb) {
    on_selection_changed_ = std::move(cb);
}

void ColorPanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    title_ = std::make_shared<Widgets::Label>("COLOR");
    ocio_state_ = std::make_shared<Widgets::Label>(
        ocio_enabled_ ? "Display Transform: ON"
                      : "Display Transform: OFF",
        true);
    add_btn_ = std::make_shared<Widgets::Button>(
        "Add to Clip", Widgets::ButtonStyle::Primary);
    add_btn_->SetTooltip("Add the selected color profile to the clip");
    add_btn_->SetOnClick([this] { AddSelected(); });
    status_ = std::make_shared<Widgets::Label>(
        entries_.empty() ? "No profiles found" : "Select a profile",
        true);

    list_ = std::make_shared<RowList>();
    scroll_area_ = std::make_shared<Widgets::ScrollArea>(list_);

    AddChild(title_);
    AddChild(ocio_state_);
    AddChild(scroll_area_);
    AddChild(add_btn_);
    AddChild(status_);
    RebuildList();
    UpdateAddEnabled();
}

void ColorPanelWidget::Refresh() {
    // Keep the selection across a reload when its name still exists.
    const std::string keep =
        selected_ >= 0 && selected_ < static_cast<int>(entries_.size())
            ? entries_[static_cast<std::size_t>(selected_)].name
            : std::string();

    // Display-transform state + colorspace defaults for subtitles —
    // compiled defaults stand in when color.yaml is absent/failing.
    Ocio::OcioConfig cfg;
    const std::string& yaml =
        AppRuntime::SharedAssetsConfig().color_yaml;
    std::string error;
    ocio_enabled_ = !yaml.empty() &&
                    Ocio::LoadOcioConfig(Path::FromUtf8(yaml), &cfg,
                                         &error) &&
                    cfg.enabled;

    entries_.clear();
    int profile_count = 0;
    const auto& profiles = Ocio::SharedOcioProfiles();
    // Header entries stash their group icon name (assets/icons.yaml)
    // in `tag` — headers never render a tag line, and the icon names
    // double as the section icon for the rows that follow (same
    // convention as the plugins panel).
    const auto append_group = [&](bool looks, const char* label,
                                  const char* icon) {
        bool headed = false;
        for (const Ocio::OcioProfile& profile : profiles) {
            if (looks != !profile.lut_path.empty()) continue;
            if (!headed) {
                entries_.push_back({true, label, icon, {}});
                headed = true;
            }
            entries_.push_back({false, profile.name,
                                TagFor(profile, cfg), profile.name});
            ++profile_count;
        }
    };
    append_group(true, "LOOKS", "shadow");
    append_group(false, "TRANSFORMS", "compare_arrows");

    selected_ = -1;
    if (!keep.empty()) {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (!entries_[i].header && entries_[i].name == keep) {
                selected_ = static_cast<int>(i);
                break;
            }
        }
    }
    if (children_attached_) {
        RebuildList();
        UpdateAddEnabled();
        ocio_state_->SetText(ocio_enabled_
                                 ? "Display Transform: ON"
                                 : "Display Transform: OFF");
        status_->SetText(profile_count == 0
                             ? "No profiles found"
                             : std::to_string(profile_count) +
                                   " profile(s)");
    }
    Invalidate();
}

void ColorPanelWidget::RebuildList() {
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
        if (!section_icon.empty()) {
            item->AddChild(std::make_shared<RowIcon>(section_icon));
        }
        const int index = static_cast<int>(i);
        item->SetOnClick([this, index] { SelectRow(index); });
        item->SetOnDoubleClick([this] { AddSelected(); });
        lines.push_back({false, {}, {}, std::move(item)});
    }
    list_->SetLines(std::move(lines));
    list_->SetSelected(selected_);
}

void ColorPanelWidget::UpdateAddEnabled() {
    add_btn_->SetEnabled(
        selected_ >= 0 &&
        selected_ < static_cast<int>(entries_.size()) &&
        !entries_[static_cast<std::size_t>(selected_)].header);
}

void ColorPanelWidget::EmitSelection() {
    if (!on_selection_changed_) return;
    on_selection_changed_(
        selected_ >= 0 &&
                selected_ < static_cast<int>(entries_.size()) &&
                !entries_[static_cast<std::size_t>(selected_)].header
            ? entries_[static_cast<std::size_t>(selected_)].name
            : std::string());
}

float ColorPanelWidget::EntryHeight(int index) const {
    return entries_[static_cast<std::size_t>(index)].header
               ? kHeaderHeight
               : kRowHeight;
}

float ColorPanelWidget::EntryTop(int index) const {
    float y = 0.0f;
    for (int i = 0; i < index; ++i) y += EntryHeight(i);
    return y;
}

void ColorPanelWidget::SelectRow(int entry_index) {
    if (selected_ == entry_index) return;
    selected_ = entry_index;
    UpdateAddEnabled();
    EmitSelection();
    if (children_attached_) list_->SetSelected(selected_);
    Invalidate();
}

void ColorPanelWidget::EnsureSelectionVisible() {
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

void ColorPanelWidget::AddSelected() {
    if (selected_ < 0 ||
        selected_ >= static_cast<int>(entries_.size()) ||
        entries_[static_cast<std::size_t>(selected_)].header) {
        SetStatus("Select a profile");
        return;
    }
    const Entry& entry = entries_[static_cast<std::size_t>(selected_)];
    if (on_add_effect_) {
        on_add_effect_("ocio:" + entry.name);
    }
    SetStatus("added " + entry.text);
}

void ColorPanelWidget::SetStatus(const std::string& text) {
    if (children_attached_) status_->SetText(text);
}

void ColorPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    if (entries_.empty()) {
        const XER::UI::Rendering::TextStyle style{
            tokens.muted, tokens.font_small, 400};
        const char* hint = "No profiles — check assets/color.yaml";
        const auto size = renderer.MeasureText(hint, style);
        renderer.DrawText(
            hint,
            {bounds.x +
                 std::max(0.0f, (bounds.width - size.width) * 0.5f),
             bounds.y +
                 std::max(0.0f, (bounds.height - size.height) * 0.5f),
             size.width, size.height},
            style);
    }
    // Rows paint themselves — Item children inside the ScrollArea;
    // the RowList draws headers + selection/hover chrome.
}

void ColorPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
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
    ocio_state_->Layout({inner_x, y, inner_w, VeTheme::kSpaceLg}, tokens);
    y += VeTheme::kSpaceLg + gap;

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

bool ColorPanelWidget::HandleEvent(
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

void ColorPanelWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
