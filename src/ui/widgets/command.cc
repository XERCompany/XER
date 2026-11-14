#include "command.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widget_tree.h"

namespace XER::UI::Widgets {

std::shared_ptr<Command> Command::Create(std::string placeholder) {
    return std::shared_ptr<Command>(new Command(std::move(placeholder)));
}

Command::Command(std::string placeholder)
    : placeholder_(std::move(placeholder)) {
    SetFocusable(true);
    open_t_.Set(1.0f, 0.14f);
}

void Command::SetItems(std::vector<Item> items) {
    items_ = std::move(items);
    highlight_ = 0;
    Invalidate();
}

void Command::SetPlaceholder(std::string placeholder) {
    placeholder_ = std::move(placeholder);
    Invalidate();
}

void Command::Open() {
    if (open_) return;
    open_ = true;
    query_.clear();
    highlight_ = 0;
    scroll_ = 0.0f;
    open_t_.Set(0.0f, 0.0f);
    open_t_.Set(1.0f, 0.14f);
    if (on_open_change_) on_open_change_(true);
    Invalidate();
}

void Command::Close() {
    if (!open_) return;
    open_ = false;
    if (on_open_change_) on_open_change_(false);
    if (GetTree() != nullptr) HideOverlay();
}

void Command::SetOnOpenChange(std::function<void(bool)> cb) {
    on_open_change_ = std::move(cb);
}

std::vector<int> Command::Filtered() const {
    std::vector<int> out;
    auto lower = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(c));
        return s;
    };
    const std::string q = lower(query_);
    for (size_t i = 0; i < items_.size(); ++i) {
        if (!items_[i].enabled) continue;
        if (q.empty() || lower(items_[i].label).find(q) != std::string::npos)
            out.push_back(static_cast<int>(i));
    }
    return out;
}

void Command::Execute(int filtered_idx) {
    const auto f = Filtered();
    if (filtered_idx < 0 || filtered_idx >= static_cast<int>(f.size()))
        return;
    auto action = items_[f[filtered_idx]].action;
    Close();
    if (action) action();
}

Layout::Rect Command::PanelRect(const Layout::Rect& b) const {
    const float w = std::min(560.0f, b.width - 80.0f);
    const float h = std::min(420.0f, b.height - 120.0f);
    return {b.x + (b.width - w) * 0.5f, b.y + (b.height - h) * 0.35f, w, h};
}

Layout::Rect Command::InputRect(const Layout::Rect& panel) const {
    return {panel.x + 12.0f, panel.y + 12.0f, panel.width - 24.0f, 34.0f};
}

Layout::Rect Command::ListRect(const Layout::Rect& panel) const {
    return {panel.x + 8.0f, panel.y + 54.0f, panel.width - 16.0f,
            panel.height - 62.0f};
}

void Command::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

void Command::Render(Rendering::Renderer& renderer,
                     const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return;
    const float t = open_t_.Value();

    // Dimmed backdrop.
    Theme::Color dim = tokens.background;
    dim.a = dim.a * 0.5f * t;
    renderer.FillRect(b, dim, 0.0f);

    const auto panel = PanelRect(b);
    const float scale = 0.96f + 0.04f * t;
    const float pw = panel.width * scale;
    const float ph = panel.height * scale;
    Layout::Rect p{panel.x + (panel.width - pw) * 0.5f,
                   panel.y + (panel.height - ph) * 0.5f, pw, ph};

    renderer.FillRect(p, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(p, tokens.border, 1.0f, tokens.radius_large);

    // Input row.
    const auto ir = InputRect(p);
    renderer.FillRect(ir, tokens.muted, tokens.radius_medium);
    const Rendering::TextStyle qs{tokens.foreground, tokens.font_body, 400};
    const std::string shown =
        query_.empty() ? placeholder_ : query_;
    const Theme::Color qc = query_.empty() ? tokens.muted : tokens.foreground;
    const Rendering::TextStyle qstyle{qc, tokens.font_body, 400};
    Layout::Rect qr{ir.x + 10.0f, ir.y, ir.width - 20.0f, ir.height};
    renderer.DrawText(shown, qr, qstyle);
    (void)qs;

    // Results.
    const auto lr = ListRect(p);
    const auto filtered = Filtered();
    const float item_h = 32.0f;
    renderer.PushClip(lr);
    for (size_t i = 0; i < filtered.size(); ++i) {
        const float iy = lr.y + item_h * i - scroll_;
        if (iy + item_h < lr.y || iy > lr.y + lr.height) continue;
        const bool hi = static_cast<int>(i) == highlight_;
        if (hi) {
            renderer.FillRect({lr.x + 4.0f, iy, lr.width - 8.0f, item_h},
                              tokens.accent, tokens.radius_small);
        }
        const auto& item = items_[filtered[i]];
        const Theme::Color tc =
            hi ? tokens.accent_foreground : tokens.foreground;
        const Rendering::TextStyle ts{tc, tokens.font_small, 500};
        Layout::Rect tr{lr.x + 14.0f, iy, lr.width - 120.0f, item_h};
        renderer.DrawText(item.label, tr, ts);
        if (!item.hint.empty()) {
            const Rendering::TextStyle hs{tokens.muted, tokens.font_small,
                                          400};
            Layout::Rect hr{lr.x + lr.width - 110.0f, iy,
                            100.0f, item_h};
            renderer.DrawText(item.hint, hr, hs);
        }
    }
    renderer.PopClip();
}

void Command::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) return;
    open_t_.Update(static_cast<float>(dt_seconds));
    if (open_t_.Active()) Invalidate();
}

bool Command::HandleEvent(const Event& event) {
    if (!open_) return false;
    const auto& p = event.position;
    const auto panel = PanelRect(GetBounds());
    const auto ir = InputRect(panel);
    const auto lr = ListRect(panel);
    const auto filtered = Filtered();
    const float item_h = 32.0f;

    switch (event.type) {
        case Event::Type::Click: {
            const bool in_panel =
                p.x >= panel.x && p.x < panel.x + panel.width &&
                p.y >= panel.y && p.y < panel.y + panel.height;
            if (!in_panel) {
                Close();
                return true;
            }
            if (p.y >= lr.y && p.y < lr.y + lr.height &&
                p.x >= lr.x && p.x < lr.x + lr.width) {
                const int idx = static_cast<int>(
                    std::floor((p.y - lr.y + scroll_) / item_h));
                if (idx >= 0 && idx < static_cast<int>(filtered.size())) {
                    Execute(idx);
                    return true;
                }
            }
            return true;
        }
        case Event::Type::KeyDown:
            switch (event.key) {
                case KeyCode::ArrowDown:
                    if (!filtered.empty()) {
                        highlight_ = (highlight_ + 1) %
                                     static_cast<int>(filtered.size());
                        Invalidate();
                    }
                    return true;
                case KeyCode::ArrowUp:
                    if (!filtered.empty()) {
                        highlight_ =
                            (highlight_ - 1 +
                             static_cast<int>(filtered.size())) %
                            static_cast<int>(filtered.size());
                        Invalidate();
                    }
                    return true;
                case KeyCode::Enter:
                    Execute(highlight_);
                    return true;
                case KeyCode::Escape:
                    Close();
                    return true;
                case KeyCode::Backspace:
                    if (!query_.empty()) {
                        query_.pop_back();
                        highlight_ = 0;
                        scroll_ = 0.0f;
                        Invalidate();
                    }
                    return true;
                default:
                    return true;  // swallow other keys while palette open
            }
        case Event::Type::TextInput:
            if (!event.text.empty()) {
                query_ += event.text;
                highlight_ = 0;
                scroll_ = 0.0f;
                Invalidate();
            }
            return true;
        default:
            return true;  // modal: consume everything
    }
}

Layout::Size Command::GetMinSize() const {
    return {200.0f, 100.0f};
}

Layout::Size Command::GetPreferredSize() const {
    return {560.0f, 420.0f};
}

Layout::Size Command::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
