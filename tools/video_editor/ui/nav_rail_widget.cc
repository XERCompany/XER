#include "nav_rail_widget.h"

#include "app_runtime/icon_pack.h"
#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"

namespace XER::VideoEditor::Ui {
namespace {

constexpr float kCell = 38.0f;
constexpr float kGap = 6.0f;

// Logical icon name per rail glyph (assets/icons.yaml). The Trans slot
// hosts the markers panel in the shell, so it maps to nav.markers.
const char* IconNameFor(NavRailWidget::Glyph glyph) {
    using Glyph = NavRailWidget::Glyph;
    switch (glyph) {
        case Glyph::Media: return "nav.media";
        case Glyph::Audio: return "nav.audio";
        case Glyph::Text: return "nav.text";
        case Glyph::Fx: return "nav.fx";
        case Glyph::Trans: return "nav.markers";
        case Glyph::Settings: return "nav.settings";
    }
    return nullptr;
}

// Short text stand-in when the pack has no SVG for the slot — keeps
// the button legible instead of blank.
const char* FallbackLabelFor(NavRailWidget::Glyph glyph) {
    using Glyph = NavRailWidget::Glyph;
    switch (glyph) {
        case Glyph::Media: return "M";
        case Glyph::Audio: return "A";
        case Glyph::Text: return "T";
        case Glyph::Fx: return "FX";
        case Glyph::Trans: return "TR";
        case Glyph::Settings: return "S";
    }
    return "?";
}

}  // namespace

void NavRailWidget::SetItems(std::vector<Item> top,
                             std::vector<Item> bottom) {
    namespace Widgets = XER::UI::Widgets;
    // Rebuilds drop the previous slot buttons before re-seeding (a
    // stale child would keep hit-testing under the new geometry).
    for (auto& slot : top_) {
        if (slot.button) RemoveChild(slot.button);
    }
    for (auto& slot : bottom_) {
        if (slot.button) RemoveChild(slot.button);
    }
    top_.clear();
    bottom_.clear();
    top_.reserve(top.size());
    bottom_.reserve(bottom.size());
    // Icon-capable buttons resolve through the pack (idempotent).
    VeIcons::InstallButtonIconResolver();
    const auto make_slot = [this](Item&& item) {
        Slot slot;
        auto button = std::make_shared<Widgets::Button>(
            "", Widgets::ButtonStyle::Ghost);
        // The rail owns no keyboard focus — transport/menu shortcuts
        // stay global while its buttons take pointer input only.
        button->SetFocusable(false);
        const char* icon = IconNameFor(item.glyph);
        if (icon != nullptr &&
            AppRuntime::SharedIconPack().Has(icon)) {
            button->SetIcon(icon);
        } else {
            // No icon in the pack — keep the old text-label slot so
            // the rail never shows an empty button.
            button->SetText(FallbackLabelFor(item.glyph));
        }
        button->SetTooltip(item.tooltip);
        const auto action = item.action;
        button->SetOnClick([action] {
            if (action) action();
        });
        slot.item = std::move(item);
        slot.button = std::move(button);
        AddChild(slot.button);
        return slot;
    };
    for (auto& item : top) top_.push_back(make_slot(std::move(item)));
    for (auto& item : bottom) {
        bottom_.push_back(make_slot(std::move(item)));
    }
    Invalidate();
}

void NavRailWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    float y = bounds.y + 10.0f;
    const float x = bounds.x + (bounds.width - kCell) * 0.5f;
    for (auto& slot : top_) {
        slot.button->Layout({x, y, kCell, kCell}, tokens);
        y += kCell + kGap;
    }
    float by = bounds.y + bounds.height - 10.0f - kCell;
    for (int i = static_cast<int>(bottom_.size()) - 1; i >= 0; --i) {
        bottom_[static_cast<std::size_t>(i)].button->Layout(
            {x, by, kCell, kCell}, tokens);
        by -= kCell + kGap;
    }
}

bool NavRailWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    (void)event;
    // The slot Buttons own hover, press, tooltips and clicks — the
    // rail itself only draws chrome.
    return false;
}

void NavRailWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.panel, 0.0f);
    renderer.DrawLine({bounds.x + bounds.width, bounds.y},
                      {bounds.x + bounds.width,
                       bounds.y + bounds.height},
                      tokens.border, 1.0f);

    // Active slot = Primary (accent fill); the rest stay Ghost — the
    // activity-bar selection convention. The buttons paint themselves
    // as children after this chrome pass.
    const auto sync = [](const Slot& slot) {
        const bool active =
            slot.item.is_active && slot.item.is_active();
        slot.button->SetStyle(active
                                  ? XER::UI::Widgets::ButtonStyle::Primary
                                  : XER::UI::Widgets::ButtonStyle::Ghost);
    };
    for (const auto& slot : top_) sync(slot);
    for (const auto& slot : bottom_) sync(slot);
}

}  // namespace XER::VideoEditor::Ui
