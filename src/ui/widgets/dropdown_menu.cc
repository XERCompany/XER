#include "dropdown_menu.h"

#include <utility>

#include "widget_tree.h"

namespace XER::UI::Widgets {

DropdownMenu::DropdownMenu() {
    SetFocusable(false);
}

DropdownMenu::~DropdownMenu() {
    Close();
}

void DropdownMenu::SetTrigger(std::shared_ptr<Widget> trigger) {
    trigger_ = std::move(trigger);
}

void DropdownMenu::SetItems(std::vector<Item> items) {
    items_ = std::move(items);
    if (IsOpen()) {
        // Refresh the live menu so new items appear without reopen.
        Close();
        Open();
    }
}

void DropdownMenu::Open() {
    SyncOpen(true);
}

void DropdownMenu::Close() {
    SyncOpen(false);
}

bool DropdownMenu::IsOpen() const {
    const auto menu = menu_.lock();
    return menu != nullptr && GetTree() != nullptr &&
           GetTree()->GetOverlay() == menu;
}

void DropdownMenu::SetOnOpenChange(std::function<void(bool)> callback) {
    on_open_change_ = std::move(callback);
}

void DropdownMenu::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (trigger_) trigger_->Layout(bounds, CachedLayoutTokens());
}

Layout::Size DropdownMenu::GetMinSize() const {
    return trigger_ ? trigger_->GetMinSize() : Layout::Size{0.0f, 0.0f};
}

Layout::Size DropdownMenu::GetPreferredSize() const {
    return trigger_ ? trigger_->GetPreferredSize()
                    : Layout::Size{0.0f, 0.0f};
}

Layout::Size DropdownMenu::GetMaxSize() const {
    return trigger_ ? trigger_->GetMaxSize()
                    : Layout::Size{100000.0f, 100000.0f};
}

void DropdownMenu::SyncOpen(bool open) {
    // Derive truth from the tree: ContextMenu closes itself after an item
    // action without notifying us.
    const bool actually = IsOpen();
    if (open == actually) {
        open_ = actually;
        return;
    }
    if (!open) {
        const auto menu = menu_.lock();
        const bool ours = menu != nullptr && GetTree() != nullptr &&
                          GetTree()->GetOverlay() == menu;
        menu_.reset();
        if (ours) HideOverlay();
        open_ = false;
        if (on_open_change_) on_open_change_(false);
        return;
    }
    if (GetTree() == nullptr || GetBounds().width <= 0.0f) return;
    auto menu = ContextMenu::Create(items_);
    // Anchor just below the trigger's bottom-left corner.
    const auto b = GetBounds();
    menu->SetAnchor({b.x, b.y + b.height + 4.0f});
    menu_ = menu;
    ShowOverlay(menu);
    open_ = true;
    if (on_open_change_) on_open_change_(true);
}

} // namespace XER::UI::Widgets
