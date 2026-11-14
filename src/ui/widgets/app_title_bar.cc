#include "app_title_bar.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "layout/flex.h"
#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/context_menu.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kBarHPad = 8.0f;
constexpr float kControlH = 28.0f;
constexpr float kControlW = 28.0f;
constexpr float kWindowBtnW = 46.0f;
constexpr float kCenterBarH = 28.0f;
constexpr float kCenterBarMaxW = 480.0f;
constexpr float kAvatarSize = 24.0f;

std::string ToUpperAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
    }
    return text;
}

std::string DeriveInitials(const std::string& name) {
    std::string clean;
    clean.reserve(name.size());
    bool word_start = true;
    for (unsigned char ch : name) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9')) {
            if (word_start && clean.size() < 2) {
                clean.push_back(static_cast<char>(
                    (ch >= 'a' && ch <= 'z') ? (ch - ('a' - 'A')) : ch));
            }
            word_start = false;
        } else {
            word_start = true;
        }
    }
    if (clean.empty()) clean = "U";
    return clean;
}

} // namespace

AppTitleBar::AppTitleBar(std::string app_label)
    : app_label_(std::move(app_label))
    , workspace_label_(app_label_) {
    hover_anim_.Set(0.0f);
}

void AppTitleBar::SetWorkspaceLabel(std::string label) {
    if (workspace_label_ == label) return;
    workspace_label_ = std::move(label);
    Invalidate();
    RequestLayout();
}

void AppTitleBar::SetWorkspaceItems(std::vector<MenuItem> items) {
    workspace_items_ = std::move(items);
}

void AppTitleBar::SetNavActions(bool can_go_back, bool can_go_forward,
                                std::function<void()> on_back,
                                std::function<void()> on_forward) {
    show_nav_ = true;
    can_go_back_ = can_go_back;
    can_go_forward_ = can_go_forward;
    on_back_ = std::move(on_back);
    on_forward_ = std::move(on_forward);
    RequestLayout();
    Invalidate();
}

void AppTitleBar::SetMenus(std::vector<Menu> menus) {
    std::vector<MenuEntry> next;
    next.reserve(menus.size());
    for (std::size_t i = 0; i < menus.size(); ++i) {
        MenuEntry entry;
        entry.label = std::move(menus[i].label);
        entry.mnemonic = menus[i].mnemonic;
        entry.items = std::move(menus[i].items);
        if (i < menus_.size() && menus_[i].label == entry.label) {
            entry.rect = menus_[i].rect;
            entry.width = menus_[i].width;
        }
        next.push_back(std::move(entry));
    }
    menus_ = std::move(next);
    RequestLayout();
    Invalidate();
}

void AppTitleBar::ClearMenus() {
    menus_.clear();
    RequestLayout();
    Invalidate();
}

void AppTitleBar::SetCenterCommand(std::string placeholder,
                                   std::function<void()> on_activate,
                                   bool show_shortcuts) {
    center_placeholder_ = std::move(placeholder);
    on_center_activate_ = std::move(on_activate);
    show_center_shortcuts_ = show_shortcuts;
    RequestLayout();
    Invalidate();
}

void AppTitleBar::SetCenterCommandValue(std::string value) {
    if (center_value_ == value) return;
    center_value_ = std::move(value);
    Invalidate();
}

void AppTitleBar::SetLeftPanelToggle(bool visible, bool active,
                                     std::function<void()> on_toggle) {
    show_left_panel_ = visible;
    left_panel_active_ = active;
    on_left_panel_toggle_ = std::move(on_toggle);
    RequestLayout();
    Invalidate();
}

void AppTitleBar::SetRightPanelToggle(bool visible, bool active,
                                      std::function<void()> on_toggle) {
    show_right_panel_ = visible;
    right_panel_active_ = active;
    on_right_panel_toggle_ = std::move(on_toggle);
    RequestLayout();
    Invalidate();
}

void AppTitleBar::SetThemeToggle(bool is_dark,
                                 std::function<void(bool)> on_theme_changed) {
    show_theme_toggle_ = true;
    is_dark_theme_ = is_dark;
    on_theme_changed_ = std::move(on_theme_changed);
    RequestLayout();
    Invalidate();
}

void AppTitleBar::SetAvatar(std::string display_name,
                            std::vector<MenuItem> menu_items) {
    avatar_name_ = std::move(display_name);
    avatar_initials_ = DeriveInitials(avatar_name_);
    avatar_items_ = std::move(menu_items);
    RequestLayout();
    Invalidate();
}

void AppTitleBar::SetActionPill(std::string text,
                                std::function<void()> on_click,
                                bool active) {
    action_pill_text_ = std::move(text);
    on_action_pill_ = std::move(on_click);
    action_pill_active_ = active;
    RequestLayout();
    Invalidate();
}

void AppTitleBar::ClearActionPill() {
    action_pill_text_.clear();
    on_action_pill_ = nullptr;
    RequestLayout();
    Invalidate();
}

void AppTitleBar::SetOnWindowCommand(
    std::function<void(WindowCommand)> callback) {
    on_window_command_ = std::move(callback);
}

bool AppTitleBar::IsInteractiveAt(Layout::Point position) const {
    return HasInteractiveAt(position);
}

bool AppTitleBar::HasInteractiveAt(Layout::Point position) const {
    return HitTest(position) != HitZone::None;
}

AppTitleBar::HitZone AppTitleBar::HitTest(Layout::Point p) const {
    auto in_rect = [&](const Layout::Rect& r) {
        return r.width > 0.0f && r.height > 0.0f && r.Contains(p);
    };

    if (in_rect(close_rect_)) return HitZone::Close;
    if (in_rect(maximize_rect_)) return HitZone::Maximize;
    if (in_rect(minimize_rect_)) return HitZone::Minimize;
    if (!action_pill_text_.empty() && in_rect(action_pill_rect_))
        return HitZone::ActionPill;
    if (in_rect(avatar_rect_)) return HitZone::Avatar;
    if (show_theme_toggle_ && in_rect(theme_toggle_rect_))
        return HitZone::ThemeToggle;
    if (show_right_panel_ && in_rect(right_panel_rect_))
        return HitZone::RightPanel;
    if (show_left_panel_ && in_rect(left_panel_rect_))
        return HitZone::LeftPanel;
    if (in_rect(center_bar_rect_)) return HitZone::CenterCommand;
    if (show_nav_ && in_rect(nav_back_rect_)) return HitZone::NavBack;
    if (show_nav_ && in_rect(nav_fwd_rect_)) return HitZone::NavForward;
    for (std::size_t i = 0; i < menus_.size(); ++i) {
        if (in_rect(menus_[i].rect)) return HitZone::Menu;
    }
    if (in_rect(workspace_rect_)) return HitZone::Workspace;
    return HitZone::None;
}

void AppTitleBar::OpenMenuOverlay(int menu_index,
                                  Layout::Point anchor) {
    if (menu_index < 0 || menu_index >= static_cast<int>(menus_.size())) {
        return;
    }
    const auto& m = menus_[menu_index];
    if (m.items.empty()) return;

    std::vector<ContextMenu::Item> items;
    items.reserve(m.items.size());
    for (const auto& mi : m.items) {
        ContextMenu::Item ci;
        ci.label = mi.label;
        ci.shortcut = mi.shortcut;
        ci.enabled = mi.enabled;
        ci.checked = mi.checked;
        ci.danger = mi.danger;
        ci.separator_before = mi.separator_before;
        ci.action = mi.action;
        items.push_back(std::move(ci));
    }

    auto menu = ContextMenu::Create(std::move(items));
    menu->SetAnchor(anchor);
    ShowOverlay(menu);
}

void AppTitleBar::ExecuteWindowCommand(WindowCommand cmd) {
    if (on_window_command_) {
        on_window_command_(cmd);
    }
}

void AppTitleBar::InvalidateInteractive(Layout::Point p) {
    const HitZone z = HitTest(p);
    if (z != hovered_zone_) {
        hovered_zone_ = z;
        Invalidate();
    }
}

void AppTitleBar::DrawVectorIcon(Rendering::Renderer& r,
                                 const Theme::Tokens& tokens,
                                 const char* icon_name,
                                 Layout::Rect box,
                                 Theme::Color color,
                                 bool active) const {
    const float cx = box.x + box.width * 0.5f;
    const float cy = box.y + box.height * 0.5f;
    const float s = std::min(box.width, box.height) * 0.22f;
    const Theme::Color c = active ? tokens.accent : color;

    if (std::string_view(icon_name) == "search") {
        // Magnifying glass: circle + handle.
        r.StrokeRect({cx - s * 1.4f, cy - s * 1.4f, s * 1.8f, s * 1.8f},
                     c, 1.6f, s * 0.9f);
        r.DrawLine({cx + s * 0.25f, cy + s * 0.25f},
                   {cx + s * 1.35f, cy + s * 1.35f}, c, 1.8f);
    } else if (std::string_view(icon_name) == "chevron_down") {
        r.DrawLine({cx - s, cy - s * 0.35f}, {cx, cy + s * 0.45f}, c, 1.8f);
        r.DrawLine({cx, cy + s * 0.45f}, {cx + s, cy - s * 0.35f}, c, 1.8f);
    } else if (std::string_view(icon_name) == "chevron_right") {
        r.DrawLine({cx - s * 0.45f, cy - s}, {cx + s * 0.55f, cy}, c, 1.8f);
        r.DrawLine({cx + s * 0.55f, cy}, {cx - s * 0.45f, cy + s}, c, 1.8f);
    } else if (std::string_view(icon_name) == "arrow_left") {
        r.DrawLine({cx + s * 0.8f, cy}, {cx - s * 0.7f, cy}, c, 1.8f);
        r.DrawLine({cx - s * 0.2f, cy - s * 0.7f}, {cx - s * 0.7f, cy}, c, 1.8f);
        r.DrawLine({cx - s * 0.2f, cy + s * 0.7f}, {cx - s * 0.7f, cy}, c, 1.8f);
    } else if (std::string_view(icon_name) == "arrow_right") {
        r.DrawLine({cx - s * 0.8f, cy}, {cx + s * 0.7f, cy}, c, 1.8f);
        r.DrawLine({cx + s * 0.2f, cy - s * 0.7f}, {cx + s * 0.7f, cy}, c, 1.8f);
        r.DrawLine({cx + s * 0.2f, cy + s * 0.7f}, {cx + s * 0.7f, cy}, c, 1.8f);
    } else if (std::string_view(icon_name) == "panel_left") {
        // Sidebar toggle: vertical bar on left + outline rectangle.
        r.FillRect({cx - s * 1.2f, cy - s * 1.2f, s * 0.45f, s * 2.4f}, c, 1.0f);
        r.StrokeRect({cx - s * 0.55f, cy - s * 1.2f, s * 2.1f, s * 2.4f},
                     c, 1.5f, 2.0f);
    } else if (std::string_view(icon_name) == "panel_right") {
        r.StrokeRect({cx - s * 1.1f, cy - s * 1.2f, s * 2.2f, s * 2.4f},
                     c, 1.5f, 2.0f);
        r.FillRect({cx + s * 0.65f, cy - s * 1.2f, s * 0.45f, s * 2.4f}, c, 1.0f);
    } else if (std::string_view(icon_name) == "layout_grid") {
        r.StrokeRect({cx - s * 1.2f, cy - s * 1.2f, s * 2.4f, s * 2.4f},
                     c, 1.4f, 2.0f);
        r.DrawLine({cx, cy - s * 1.2f}, {cx, cy + s * 1.2f}, c, 1.2f);
        r.DrawLine({cx - s * 1.2f, cy}, {cx + s * 1.2f, cy}, c, 1.2f);
    } else if (std::string_view(icon_name) == "sun") {
        r.StrokeRect({cx - s, cy - s, s * 2.0f, s * 2.0f}, c, 1.5f, s);
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * 0.785398f;
            r.DrawLine(
                {cx + std::cos(a) * s * 1.35f, cy + std::sin(a) * s * 1.35f},
                {cx + std::cos(a) * s * 1.75f, cy + std::sin(a) * s * 1.75f},
                c, 1.4f);
        }
    } else if (std::string_view(icon_name) == "moon") {
        // Filled circle reads as a moon glyph at titlebar scale and avoids
        // carve-out artifacts over hover backgrounds.
        r.FillRect({cx - s * 0.9f, cy - s * 0.9f, s * 1.8f, s * 1.8f},
                   c, s * 0.9f);
    } else if (std::string_view(icon_name) == "minimize") {
        r.DrawLine({cx - s, cy}, {cx + s, cy}, c, 1.8f);
    } else if (std::string_view(icon_name) == "maximize") {
        r.StrokeRect({cx - s * 0.85f, cy - s * 0.85f, s * 1.7f, s * 1.7f},
                     c, 1.5f, 1.0f);
    } else if (std::string_view(icon_name) == "restore") {
        r.StrokeRect({cx - s * 0.95f, cy - s * 0.65f, s * 1.5f, s * 1.3f},
                     c, 1.4f, 1.0f);
        r.StrokeRect({cx - s * 0.55f, cy - s * 1.05f, s * 1.5f, s * 1.3f},
                     c, 1.4f, 1.0f);
    } else if (std::string_view(icon_name) == "close") {
        r.DrawLine({cx - s * 0.75f, cy - s * 0.75f},
                   {cx + s * 0.75f, cy + s * 0.75f}, c, 1.8f);
        r.DrawLine({cx + s * 0.75f, cy - s * 0.75f},
                   {cx - s * 0.75f, cy + s * 0.75f}, c, 1.8f);
    }
}

void AppTitleBar::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) return;
    const bool is_hover = hovered_zone_ != HitZone::None ||
                          pressed_zone_ != HitZone::None;
    const float target = is_hover ? 1.0f : 0.0f;
    if (hover_anim_.to != target) {
        hover_anim_.Set(target, 0.12f);
    }
    hover_anim_.Update(static_cast<float>(dt_seconds));
    if (hover_anim_.Active()) {
        Invalidate();
    }
}

void AppTitleBar::Render(Rendering::Renderer& r,
                         const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    bounds_ = b;
    const float cy = b.y + b.height * 0.5f;
    const float item_y = cy - kControlH * 0.5f;
    const bool ws_hot = hovered_zone_ == HitZone::Workspace ||
                        pressed_zone_ == HitZone::Workspace;
    const bool nav_b_hot = hovered_zone_ == HitZone::NavBack ||
                           pressed_zone_ == HitZone::NavBack;
    const bool nav_f_hot = hovered_zone_ == HitZone::NavForward ||
                           pressed_zone_ == HitZone::NavForward;
    const bool lp_hot = hovered_zone_ == HitZone::LeftPanel ||
                        pressed_zone_ == HitZone::LeftPanel;
    const bool rp_hot = hovered_zone_ == HitZone::RightPanel ||
                        pressed_zone_ == HitZone::RightPanel;
    const bool tt_hot = hovered_zone_ == HitZone::ThemeToggle ||
                        pressed_zone_ == HitZone::ThemeToggle;
    const bool av_hot = hovered_zone_ == HitZone::Avatar ||
                        pressed_zone_ == HitZone::Avatar;
    const bool ap_hot = hovered_zone_ == HitZone::ActionPill ||
                        pressed_zone_ == HitZone::ActionPill;
    const bool cc_hot = hovered_zone_ == HitZone::CenterCommand ||
                        pressed_zone_ == HitZone::CenterCommand;

    // ---- Pass 1: geometry (left cluster walk, right cluster walk, center) --
    float x = b.x + kBarHPad;

    // Workspace pill: [glyph] Label chevron
    {
        const Rendering::TextStyle label_style{tokens.foreground, 12.5f, 600};
        const float label_w = r.MeasureText(workspace_label_, label_style).width;
        const float w_width = 5.0f + 20.0f + 7.0f + label_w + 6.0f + 12.0f + 6.0f;
        workspace_rect_ = Layout::Rect{x, item_y, w_width, kControlH};
        x += w_width + 6.0f;
    }

    if (show_nav_) {
        nav_back_rect_ = Layout::Rect{x, item_y, 26.0f, kControlH};
        nav_fwd_rect_ = Layout::Rect{x + 28.0f, item_y, 26.0f, kControlH};
        x += 58.0f;
    } else {
        nav_back_rect_ = Layout::Rect{};
        nav_fwd_rect_ = Layout::Rect{};
    }

    if (show_left_panel_) {
        left_panel_rect_ = Layout::Rect{x, item_y, kControlW, kControlH};
        x += kControlW + 6.0f;
    } else {
        left_panel_rect_ = Layout::Rect{};
    }

    // Menu rects
    for (auto& m : menus_) {
        const Rendering::TextStyle m_style{tokens.foreground, 12.0f, 450};
        const float tw = r.MeasureText(m.label, m_style).width;
        const float mw = tw + 20.0f;
        m.rect = Layout::Rect{x, item_y, mw, kControlH};
        m.width = mw;
        x += mw + 2.0f;
    }

    // Right cluster walk (flush Windows 11 caption buttons on the right edge)
    float right_cursor = b.Right();
    close_rect_ = Layout::Rect{right_cursor - kWindowBtnW, b.y,
                               kWindowBtnW, b.height};
    right_cursor -= kWindowBtnW;
    maximize_rect_ = Layout::Rect{right_cursor - kWindowBtnW, b.y,
                                  kWindowBtnW, b.height};
    right_cursor -= kWindowBtnW;
    minimize_rect_ = Layout::Rect{right_cursor - kWindowBtnW, b.y,
                                  kWindowBtnW, b.height};
    right_cursor -= kWindowBtnW + 8.0f;

    if (!action_pill_text_.empty() && on_action_pill_) {
        const Rendering::TextStyle pill_text_style{tokens.accent_foreground,
                                                   12.0f, 600};
        const float tw =
            r.MeasureText(action_pill_text_, pill_text_style).width;
        const float pw = tw + 40.0f;
        action_pill_rect_ = Layout::Rect{right_cursor - pw, item_y, pw,
                                         kControlH};
        right_cursor -= pw + 8.0f;
    } else {
        action_pill_rect_ = Layout::Rect{};
    }

    avatar_rect_ = Layout::Rect{right_cursor - (kAvatarSize + 20.0f), item_y,
                                kAvatarSize + 20.0f, kControlH};
    right_cursor -= kAvatarSize + 20.0f + 6.0f;

    if (show_theme_toggle_) {
        theme_toggle_rect_ = Layout::Rect{right_cursor - kControlW, item_y,
                                          kControlW, kControlH};
        right_cursor -= kControlW + 4.0f;
    } else {
        theme_toggle_rect_ = Layout::Rect{};
    }

    if (show_right_panel_) {
        right_panel_rect_ = Layout::Rect{right_cursor - kControlW, item_y,
                                         kControlW, kControlH};
        right_cursor -= kControlW + 4.0f;
    } else {
        right_panel_rect_ = Layout::Rect{};
    }

    const float center_left = x + 8.0f;
    const float center_right = right_cursor - 8.0f;
    const float avail_center_w = std::max(120.0f, center_right - center_left);
    const float pill_w = std::min(kCenterBarMaxW, avail_center_w);
    const float pill_x = center_left + (avail_center_w - pill_w) * 0.5f;
    center_bar_rect_ = Layout::Rect{pill_x, cy - kCenterBarH * 0.5f,
                                    pill_w, kCenterBarH};

    // ---- Pass 2: backgrounds (hover / active fills under all content) -----
    if (ws_hot) {
        r.FillRect(workspace_rect_, tokens.hover, tokens.radius_medium);
    }
    if (nav_b_hot && show_nav_) {
        r.FillRect(nav_back_rect_, tokens.hover, tokens.radius_small);
    }
    if (nav_f_hot && show_nav_) {
        r.FillRect(nav_fwd_rect_, tokens.hover, tokens.radius_small);
    }
    if (show_left_panel_ && (lp_hot || left_panel_active_)) {
        r.FillRect(left_panel_rect_, tokens.hover, tokens.radius_small);
    }
    for (std::size_t i = 0; i < menus_.size(); ++i) {
        if (hovered_menu_index_ == static_cast<int>(i)) {
            r.FillRect(menus_[i].rect, tokens.hover, tokens.radius_small);
        }
    }

    // Center command pill background
    r.FillRect(center_bar_rect_, tokens.secondary, tokens.radius_large);
    r.StrokeRect(center_bar_rect_, tokens.border, 1.0f, tokens.radius_large);

    if (show_right_panel_ && (rp_hot || right_panel_active_)) {
        r.FillRect(right_panel_rect_, tokens.hover, tokens.radius_small);
    }
    if (tt_hot) {
        r.FillRect(theme_toggle_rect_, tokens.hover, tokens.radius_small);
    }
    if (av_hot) {
        r.FillRect(avatar_rect_, tokens.hover, tokens.radius_medium);
    }

    // Window button hover backgrounds (close gets danger fill)
    auto win_btn_bg = [&](const Layout::Rect& rect, HitZone zone,
                          bool is_close) {
        const bool wh = hovered_zone_ == zone || pressed_zone_ == zone;
        if (!wh) return;
        r.FillRect(rect,
                   is_close ? tokens.danger : tokens.hover, 0.0f);
    };
    win_btn_bg(minimize_rect_, HitZone::Minimize, false);
    win_btn_bg(maximize_rect_, HitZone::Maximize, false);
    win_btn_bg(close_rect_, HitZone::Close, true);

    // ---- Pass 3: content (icons + text) -----------------------------------
    // Workspace glyph + label + chevron
    {
        const Layout::Rect glyph_box{workspace_rect_.x + 5.0f,
                                     item_y + (kControlH - 20.0f) * 0.5f,
                                     20.0f, 20.0f};
        r.FillRect(glyph_box, tokens.accent, 5.0f);
        const std::string glyph_letter =
            ToUpperAscii(workspace_label_).substr(0, 1);
        const Rendering::TextStyle glyph_style{tokens.accent_foreground,
                                               11.5f, 700};
        const auto glyph_tr = Layout::CenteredTextRect(
            r, glyph_letter, glyph_style, glyph_box, 0.0f, true);
        r.DrawText(glyph_letter, glyph_tr, glyph_style);

        const Rendering::TextStyle label_style{tokens.foreground, 12.5f, 600};
        const float label_w =
            r.MeasureText(workspace_label_, label_style).width;
        const auto label_rect = Layout::CenteredTextRect(
            r, workspace_label_, label_style,
            Layout::Rect{workspace_rect_.x + 32.0f, item_y,
                         label_w + 4.0f, kControlH},
            0.0f, false);
        r.DrawText(workspace_label_, label_rect, label_style);
        DrawVectorIcon(r, tokens, "chevron_down",
                       {workspace_rect_.x + 38.0f + label_w,
                        item_y + (kControlH - 12.0f) * 0.5f,
                        12.0f, 12.0f},
                       tokens.muted, ws_hot);
    }

    // Nav arrows
    if (show_nav_) {
        DrawVectorIcon(r, tokens, "arrow_left", nav_back_rect_,
                       can_go_back_ ? tokens.foreground : tokens.muted,
                       nav_b_hot);
        DrawVectorIcon(r, tokens, "arrow_right", nav_fwd_rect_,
                       can_go_forward_ ? tokens.foreground : tokens.muted,
                       nav_f_hot);
    }

    // Left panel toggle icon
    if (show_left_panel_) {
        DrawVectorIcon(r, tokens, "panel_left", left_panel_rect_,
                       left_panel_active_ ? tokens.accent : tokens.foreground,
                       lp_hot);
    }

    // Menu labels
    for (const auto& m : menus_) {
        if (m.rect.width <= 0.0f) continue;
        const Rendering::TextStyle m_style{tokens.foreground, 12.0f, 450};
        const auto tr = Layout::CenteredTextRect(
            r, m.label, m_style, m.rect, 10.0f, false);
        r.DrawText(m.label, tr, m_style);
    }

    // Center pill: search icon + text + shortcut keycaps
    DrawVectorIcon(r, tokens, "search",
                   {center_bar_rect_.x + 10.0f,
                    center_bar_rect_.y + (kCenterBarH - 18.0f) * 0.5f,
                    18.0f, 18.0f},
                   tokens.muted, cc_hot);
    {
        const Rendering::TextStyle center_style{
            center_value_.empty() ? tokens.muted : tokens.foreground,
            12.5f, 450};
        const std::string& shown_text =
            center_value_.empty() ? center_placeholder_ : center_value_;
        const float text_pad_l = 36.0f;
        const float text_pad_r = show_center_shortcuts_ ? 78.0f : 14.0f;
        const Layout::Rect text_rect{
            center_bar_rect_.x + text_pad_l,
            center_bar_rect_.y,
            std::max(10.0f,
                     center_bar_rect_.width - text_pad_l - text_pad_r),
            kCenterBarH};
        const auto tr = Layout::CenteredTextRect(
            r, shown_text, center_style, text_rect, 0.0f, false);
        r.DrawText(shown_text, tr, center_style);
    }
    if (show_center_shortcuts_ && center_bar_rect_.width >= 240.0f) {
        const Rendering::TextStyle key_style{tokens.muted, 10.0f, 600};
        const char* k1 = "Ctrl";
        const char* k2 = "K";
        const float k1_w = r.MeasureText(k1, key_style).width + 10.0f;
        const float k2_w = r.MeasureText(k2, key_style).width + 10.0f;
        const float keys_total = k1_w + k2_w + 4.0f;
        float kx = center_bar_rect_.Right() - 8.0f - keys_total;
        const float ky = center_bar_rect_.y + (kCenterBarH - 18.0f) * 0.5f;
        auto draw_key = [&](const char* label, float kw) {
            r.FillRect({kx, ky, kw, 18.0f}, tokens.background, 4.0f);
            r.StrokeRect({kx, ky, kw, 18.0f}, tokens.border, 0.8f, 4.0f);
            const auto tr = Layout::CenteredTextRect(
                r, label, key_style, Layout::Rect{kx, ky, kw, 18.0f},
                0.0f, true);
            r.DrawText(label, tr, key_style);
            kx += kw + 4.0f;
        };
        draw_key(k1, k1_w);
        draw_key(k2, k2_w);
    }

    // Right panel toggle icon
    if (show_right_panel_) {
        DrawVectorIcon(r, tokens, "panel_right", right_panel_rect_,
                       right_panel_active_ ? tokens.accent : tokens.foreground,
                       rp_hot);
    }

    // Theme toggle icon
    if (show_theme_toggle_) {
        DrawVectorIcon(r, tokens, is_dark_theme_ ? "sun" : "moon",
                       theme_toggle_rect_, tokens.foreground, tt_hot);
    }

    // Avatar circle + initials + chevron
    {
        const float av_d = kAvatarSize;
        const float av_x = avatar_rect_.x + 3.0f;
        const float av_y = avatar_rect_.y + (kControlH - av_d) * 0.5f;
        r.FillRect({av_x, av_y, av_d, av_d}, tokens.accent, av_d * 0.5f);
        const Rendering::TextStyle init_style{tokens.accent_foreground,
                                              10.5f, 700};
        const auto init_rect = Layout::CenteredTextRect(
            r, avatar_initials_, init_style,
            Layout::Rect{av_x, av_y, av_d, av_d}, 0.0f, true);
        r.DrawText(avatar_initials_, init_rect, init_style);
        DrawVectorIcon(r, tokens, "chevron_down",
                       {av_x + av_d + 3.0f,
                        avatar_rect_.y + (kControlH - 12.0f) * 0.5f,
                        12.0f, 12.0f},
                       av_hot ? tokens.foreground : tokens.muted, av_hot);
    }

    // Action pill
    if (!action_pill_text_.empty() && on_action_pill_) {
        const Theme::Color pill_bg = action_pill_active_
            ? Motion::LerpColor(tokens.accent, tokens.foreground, 0.12f)
            : tokens.accent;
        const Theme::Color pill_bg_hover = ap_hot
            ? Motion::LerpColor(pill_bg, tokens.foreground, 0.10f)
            : pill_bg;
        r.FillRect(action_pill_rect_, pill_bg_hover, kControlH * 0.5f);
        const Rendering::TextStyle pill_style{tokens.accent_foreground,
                                              12.0f, 600};
        const float tw =
            r.MeasureText(action_pill_text_, pill_style).width;
        const auto tr = Layout::CenteredTextRect(
            r, action_pill_text_, pill_style,
            Layout::Rect{action_pill_rect_.x + 14.0f, action_pill_rect_.y,
                         tw + 4.0f, kControlH},
            0.0f, false);
        r.DrawText(action_pill_text_, tr, pill_style);
        DrawVectorIcon(r, tokens, "chevron_right",
                       {action_pill_rect_.Right() - 20.0f,
                        action_pill_rect_.y + (kControlH - 12.0f) * 0.5f,
                        12.0f, 12.0f},
                       tokens.accent_foreground, false);
    }

    // Window control buttons
    auto draw_win_btn = [&](const Layout::Rect& rect, const char* icon,
                            HitZone zone, bool is_close) {
        const bool wh = hovered_zone_ == zone || pressed_zone_ == zone;
        Theme::Color ic = tokens.foreground;
        if (is_close && wh) ic = tokens.danger_foreground;
        DrawVectorIcon(r, tokens, icon, rect, ic, false);
    };
    draw_win_btn(minimize_rect_, "minimize", HitZone::Minimize, false);
    draw_win_btn(maximize_rect_,
                 GetMaximized() ? "restore" : "maximize",
                 HitZone::Maximize, false);
    draw_win_btn(close_rect_, "close", HitZone::Close, true);

    // Subtle bottom hairline separating titlebar chrome from content below.
    r.DrawLine({b.x, b.Bottom() - 0.5f}, {b.Right(), b.Bottom() - 0.5f},
               tokens.border, 1.0f);
}

void AppTitleBar::Layout(const Layout::Rect& bounds) {
    Layout(bounds, CachedLayoutTokens());
}

void AppTitleBar::Layout(const Layout::Rect& bounds,
                         const Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    SetBounds(bounds);
    bounds_ = bounds;
    RequestLayout();
}

bool AppTitleBar::HandleEvent(const Event& event) {
    const auto b = GetBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return false;

    if (event.type == Event::Type::MouseMove) {
        const HitZone z = HitTest(event.position);
        if (z != hovered_zone_) {
            hovered_zone_ = z;
            // Track which menu index is hovered for per-item highlight.
            hovered_menu_index_ = -1;
            if (z == HitZone::Menu) {
                for (std::size_t i = 0; i < menus_.size(); ++i) {
                    if (menus_[i].rect.Contains(event.position)) {
                        hovered_menu_index_ = static_cast<int>(i);
                        break;
                    }
                }
            }
            Invalidate();
        } else if (z == HitZone::Menu) {
            int idx = -1;
            for (std::size_t i = 0; i < menus_.size(); ++i) {
                if (menus_[i].rect.Contains(event.position)) {
                    idx = static_cast<int>(i);
                    break;
                }
            }
            if (idx != hovered_menu_index_) {
                hovered_menu_index_ = idx;
                Invalidate();
            }
        }
        return z != HitZone::None;
    }

    if (event.type == Event::Type::MouseDown &&
        event.mouse_button == MouseButton::Left) {
        const HitZone z = HitTest(event.position);
        if (z != HitZone::None) {
            pressed_zone_ = z;
            if (z == HitZone::Menu) {
                for (std::size_t i = 0; i < menus_.size(); ++i) {
                    if (menus_[i].rect.Contains(event.position)) {
                        hovered_menu_index_ = static_cast<int>(i);
                        break;
                    }
                }
            }
            Invalidate();
            return true;
        }
        return false;
    }

    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        const HitZone z = HitTest(event.position);
        pressed_zone_ = HitZone::None;

        switch (z) {
            case HitZone::Workspace:
                if (!workspace_items_.empty()) {
                    std::vector<ContextMenu::Item> items;
                    items.reserve(workspace_items_.size());
                    for (const auto& wi : workspace_items_) {
                        ContextMenu::Item ci;
                        ci.label = wi.label;
                        ci.shortcut = wi.shortcut;
                        ci.enabled = wi.enabled;
                        ci.checked = wi.checked;
                        ci.danger = wi.danger;
                        ci.separator_before = wi.separator_before;
                        ci.action = wi.action;
                        items.push_back(std::move(ci));
                    }
                    hovered_zone_ = HitZone::None;
                    hovered_menu_index_ = -1;
                    auto menu = ContextMenu::Create(std::move(items));
                    menu->SetAnchor({workspace_rect_.x, b.Bottom() + 2.0f});
                    ShowOverlay(menu);
                }
                Invalidate();
                return true;

            case HitZone::NavBack:
                if (can_go_back_ && on_back_) {
                    on_back_();
                    return true;
                }
                break;

            case HitZone::NavForward:
                if (can_go_forward_ && on_forward_) {
                    on_forward_();
                    return true;
                }
                break;

            case HitZone::Menu: {
                for (std::size_t i = 0; i < menus_.size(); ++i) {
                    if (menus_[i].rect.Contains(event.position)) {
                        const Layout::Point anchor{menus_[i].rect.x,
                                                   b.Bottom() + 2.0f};
                        hovered_zone_ = HitZone::None;
                        hovered_menu_index_ = -1;
                        OpenMenuOverlay(static_cast<int>(i), anchor);
                        Invalidate();
                        return true;
                    }
                }
                break;
            }

            case HitZone::CenterCommand:
                if (on_center_activate_) {
                    on_center_activate_();
                    return true;
                }
                break;

            case HitZone::LeftPanel:
                if (on_left_panel_toggle_) {
                    on_left_panel_toggle_();
                    return true;
                }
                break;

            case HitZone::RightPanel:
                if (on_right_panel_toggle_) {
                    on_right_panel_toggle_();
                    return true;
                }
                break;

            case HitZone::ThemeToggle:
                if (on_theme_changed_) {
                    // Widget owns its icon state; the callback only notifies
                    // the owner so it can switch the window theme.
                    is_dark_theme_ = !is_dark_theme_;
                    on_theme_changed_(is_dark_theme_);
                    Invalidate();
                    return true;
                }
                break;

            case HitZone::Avatar:
                if (!avatar_items_.empty()) {
                    std::vector<ContextMenu::Item> items;
                    items.reserve(avatar_items_.size());
                    for (const auto& ai : avatar_items_) {
                        ContextMenu::Item ci;
                        ci.label = ai.label;
                        ci.shortcut = ai.shortcut;
                        ci.enabled = ai.enabled;
                        ci.checked = ai.checked;
                        ci.danger = ai.danger;
                        ci.separator_before = ai.separator_before;
                        ci.action = ai.action;
                        items.push_back(std::move(ci));
                    }
                    hovered_zone_ = HitZone::None;
                    hovered_menu_index_ = -1;
                    auto menu = ContextMenu::Create(std::move(items));
                    menu->SetAnchor({std::max(4.0f, avatar_rect_.Right() - 216.0f),
                                     b.Bottom() + 2.0f});
                    ShowOverlay(menu);
                    Invalidate();
                    return true;
                }
                break;

            case HitZone::ActionPill:
                if (on_action_pill_) {
                    on_action_pill_();
                    return true;
                }
                break;

            case HitZone::Minimize:
                ExecuteWindowCommand(WindowCommand::Minimize);
                return true;

            case HitZone::Maximize:
                ExecuteWindowCommand(WindowCommand::ToggleMaximize);
                return true;

            case HitZone::Close:
                ExecuteWindowCommand(WindowCommand::Close);
                return true;

            case HitZone::None:
                break;
        }
        Invalidate();
        return z != HitZone::None;
    }

    if (event.type == Event::Type::MouseUp &&
        event.mouse_button == MouseButton::Left) {
        pressed_zone_ = HitZone::None;
        Invalidate();
        return false;
    }

    return false;
}

Layout::Size AppTitleBar::GetMinSize() const {
    return {360.0f, 34.0f};
}

Layout::Size AppTitleBar::GetPreferredSize() const {
    return {960.0f, 38.0f};
}

Layout::Size AppTitleBar::GetMaxSize() const {
    return {16384.0f, 38.0f};
}
} // namespace XER::UI::Widgets
