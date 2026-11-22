#include "tags_input.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include "content/clipboard/clipboard.h"
#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widget_tree.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kFieldPadX = 10.0f;
constexpr float kChipGap = 6.0f;
constexpr float kChipPadX = 8.0f;
constexpr float kChipCloseW = 14.0f; // clickable × zone at the chip's right
constexpr float kChipInsetY = 5.0f;
constexpr float kRowHeight = 28.0f;
constexpr float kPanelPad = 4.0f;
constexpr float kMaxPanelHeight = 8.0f * kRowHeight + 2.0f * kPanelPad;
constexpr std::size_t kMaxSuggestions = 8;
constexpr float kDragThreshold = 6.0f;

std::size_t Utf8Length(std::string_view text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

bool IsContinuation(unsigned char byte) {
    return (byte & 0xC0u) == 0x80u;
}

std::size_t PreviousCodePoint(std::string_view text, std::size_t position) {
    if (position == 0) {
        return 0;
    }
    --position;
    while (position > 0 &&
           IsContinuation(static_cast<unsigned char>(text[position]))) {
        --position;
    }
    return position;
}

std::size_t NextCodePoint(std::string_view text, std::size_t position) {
    if (position >= text.size()) {
        return text.size();
    }
    ++position;
    while (position < text.size() &&
           IsContinuation(static_cast<unsigned char>(text[position]))) {
        ++position;
    }
    return position;
}

std::string ToLower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Suggestions dropdown: a deliberately NON-focusable overlay so the field
// keeps focus and typing continues (drift's filter-as-you-type). Keys and
// text the list doesn't consume are forwarded to the owning TagsInput.
// ---------------------------------------------------------------------------
class TagsSuggestionsOverlay final : public Widget {
public:
    TagsSuggestionsOverlay(Layout::Rect anchor,
                           std::vector<std::string> items)
        : anchor_(anchor), items_(std::move(items)) {
        SetFocusable(false); // focus stays in the field — see class doc
        open_t_.Set(1.0f, 0.12f);
    }

    void SetOwner(std::weak_ptr<Widget> owner) {
        owner_ = std::move(owner);
    }

    void Update(const Layout::Rect& anchor,
                const std::vector<std::string>& items) {
        anchor_ = anchor;
        items_ = items;
        if (active_ >= static_cast<int>(items_.size())) {
            active_ = static_cast<int>(items_.size()) - 1;
        }
        Invalidate();
    }

    void Tick(double dt_seconds) override {
        if (!IsEffectivelyVisible()) {
            return;
        }
        if (!ticked_ && rendered_) {
            open_t_.Set(1.0f, 0.0f);
        }
        ticked_ = true;
        open_t_.Update(static_cast<float>(dt_seconds));
        if (open_t_.Active()) {
            Invalidate();
        }
    }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override {
        const float p = ticked_ ? open_t_.Value() : 1.0f;
        rendered_ = true;
        const auto FA = [p](Theme::Color col) {
            col.a *= p;
            return col;
        };
        Layout::Rect panel = PanelRect();
        panel.y += (1.0f - p) * -4.0f;
        renderer.DrawBackdropBlur(
            Layout::Rect{panel.x - 6.0f, panel.y - 6.0f,
                         panel.width + 12.0f, panel.height + 12.0f},
            0.7f);
        Theme::Color bg = tokens.surface;
        bg.a = 1.0f;
        renderer.FillRect(panel, FA(bg), tokens.radius_medium);
        renderer.StrokeRect(panel, FA(tokens.border), 1.0f,
                            tokens.radius_medium);
        renderer.PushClip(panel);
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const Layout::Rect row{panel.x + kPanelPad,
                                   panel.y + kPanelPad +
                                       kRowHeight * static_cast<float>(i),
                                   panel.width - 2.0f * kPanelPad,
                                   kRowHeight};
            if (static_cast<int>(i) == active_) {
                renderer.FillRect(row, FA(tokens.hover),
                                  tokens.radius_small);
            }
            const Rendering::TextStyle style{
                FA(tokens.foreground), tokens.font_body, 400};
            const auto sample = items_[i].empty()
                ? std::string_view("Ag")
                : std::string_view(items_[i]);
            const float text_h = renderer.MeasureText(sample, style).height;
            renderer.DrawText(
                items_[i],
                Layout::Rect{row.x + 8.0f,
                             row.y + std::max(
                                 0.0f, (kRowHeight - text_h) * 0.5f),
                             std::max(0.0f, row.width - 16.0f),
                             text_h + 1.0f},
                style);
        }
        renderer.PopClip();
    }

    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
    }

    bool HandleEvent(const Event& event) override {
        switch (event.type) {
            case Event::Type::MouseDown:
                return true; // modal: presses don't reach the field
            case Event::Type::MouseMove: {
                const int row = HitRow(event.position);
                if (row != active_) {
                    active_ = row;
                    Invalidate();
                }
                return true;
            }
            case Event::Type::Click: {
                const int row = HitRow(event.position);
                if (row >= 0) {
                    if (const auto owner = LockOwner()) {
                        owner->PickSuggestion(
                            items_[static_cast<std::size_t>(row)]);
                    }
                    return true;
                }
                if (const auto owner = LockOwner()) {
                    owner->CloseSuggestions();
                }
                return true;
            }
            case Event::Type::KeyDown:
                switch (event.key) {
                    case KeyCode::Escape:
                        if (const auto owner = LockOwner()) {
                            owner->CloseSuggestions();
                        }
                        return true;
                    case KeyCode::ArrowUp:
                        MoveActive(-1);
                        return true;
                    case KeyCode::ArrowDown:
                        MoveActive(1);
                        return true;
                    case KeyCode::Home:
                        SetActive(0);
                        return true;
                    case KeyCode::End:
                        SetActive(static_cast<int>(items_.size()) - 1);
                        return true;
                    case KeyCode::Enter:
                    case KeyCode::Space:
                        if (active_ >= 0 &&
                            active_ < static_cast<int>(items_.size())) {
                            if (const auto owner = LockOwner()) {
                                owner->PickSuggestion(
                                    items_[static_cast<std::size_t>(
                                        active_)]);
                            }
                            return true;
                        }
                        // No active row: Enter should behave as if the
                        // list were closed (commit the draft).
                        return Forward(event);
                    default:
                        return Forward(event);
                }
            case Event::Type::TextInput:
            case Event::Type::Focus:
            case Event::Type::Blur:
            case Event::Type::KeyUp:
                return Forward(event);
            default:
                return false;
        }
    }

    Layout::Size GetMinSize() const override { return {0.0f, 0.0f}; }
    Layout::Size GetPreferredSize() const override { return {0.0f, 0.0f}; }
    Layout::Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }

private:
    std::shared_ptr<TagsInput> LockOwner() const {
        return std::static_pointer_cast<TagsInput>(owner_.lock());
    }

    bool Forward(const Event& event) {
        if (const auto owner = LockOwner()) {
            return owner->HandleEvent(event);
        }
        return false;
    }

    Layout::Rect PanelRect() const {
        const auto bounds = GetBounds();
        const float height = std::min(
            kRowHeight * static_cast<float>(items_.size()) +
                2.0f * kPanelPad,
            kMaxPanelHeight);
        const float width = std::clamp(std::max(anchor_.width, 160.0f),
                                       120.0f, bounds.width - 8.0f);
        float x = anchor_.x;
        float y = anchor_.y + anchor_.height + 4.0f;
        const float space_below = bounds.y + bounds.height - 4.0f - y;
        if (space_below < height) {
            const float space_above = anchor_.y - 4.0f - bounds.y;
            if (space_above > space_below) {
                y = anchor_.y - height - 4.0f;
            }
        }
        x = std::clamp(x, bounds.x + 4.0f,
                       bounds.x + bounds.width - width - 4.0f);
        y = std::clamp(y, bounds.y + 4.0f,
                       std::max(bounds.y + 4.0f,
                                bounds.y + bounds.height - height - 4.0f));
        return Layout::Rect{x, y, width, height};
    }

    int HitRow(const Layout::Point& position) const {
        const auto panel = PanelRect();
        if (!panel.Contains(position)) {
            return -1;
        }
        const int row = static_cast<int>(
            (position.y - panel.y - kPanelPad) / kRowHeight);
        if (row < 0 || row >= static_cast<int>(items_.size())) {
            return -1;
        }
        return row;
    }

    void MoveActive(int delta) {
        const int n = static_cast<int>(items_.size());
        if (n == 0) {
            return;
        }
        SetActive(active_ < 0 ? (delta > 0 ? 0 : n - 1)
                              : (active_ + delta + n) % n);
    }

    void SetActive(int index) {
        index = std::clamp(index, -1, static_cast<int>(items_.size()) - 1);
        if (index == active_) {
            return;
        }
        active_ = index;
        Invalidate();
    }

    Layout::Rect anchor_;
    std::vector<std::string> items_;
    int active_ = -1;
    bool ticked_ = false;
    bool rendered_ = false;
    Motion::Tween open_t_;
    std::weak_ptr<Widget> owner_;
};

// ---------------------------------------------------------------------------

TagsInput::TagsInput(std::vector<std::string> tags)
    : tags_(std::move(tags)) {
    SetFocusable(true);
}

TagsInput::~TagsInput() {
    CloseSuggestions();
}

void TagsInput::SetTags(std::vector<std::string> tags) {
    tags_ = std::move(tags);
    selected_chip_ = -1;
    Invalidate();
    RequestLayout();
    NotifyTagsChanged();
}

void TagsInput::SetOnTagsChanged(
    std::function<void(const std::vector<std::string>&)> callback) {
    on_tags_changed_ = std::move(callback);
}

std::string TagsInput::Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool TagsInput::AtMax() const {
    return max_ > 0 && static_cast<int>(tags_.size()) >= max_;
}

bool TagsInput::CanAdd(const std::string& tag) const {
    if (tag.empty() || AtMax()) {
        return false;
    }
    if (!allow_duplicates_ &&
        std::find(tags_.begin(), tags_.end(), tag) != tags_.end()) {
        return false;
    }
    if (validate_ && !validate_(tag)) {
        return false;
    }
    return true;
}

bool TagsInput::AddTag(std::string tag) {
    tag = Trim(tag);
    if (!CanAdd(tag)) {
        return false;
    }
    tags_.push_back(std::move(tag));
    Invalidate();
    RequestLayout();
    NotifyTagsChanged();
    RefreshSuggestions();
    return true;
}

bool TagsInput::RemoveTag(std::size_t index) {
    if (index >= tags_.size()) {
        return false;
    }
    tags_.erase(tags_.begin() + static_cast<std::ptrdiff_t>(index));
    if (selected_chip_ == static_cast<int>(index)) {
        selected_chip_ = -1;
    } else if (selected_chip_ > static_cast<int>(index)) {
        --selected_chip_;
    }
    Invalidate();
    NotifyTagsChanged();
    RefreshSuggestions();
    return true;
}

bool TagsInput::MoveTag(std::size_t from, std::size_t to) {
    if (from >= tags_.size() || to >= tags_.size() || from == to) {
        return false;
    }
    std::string tag = std::move(tags_[from]);
    tags_.erase(tags_.begin() + static_cast<std::ptrdiff_t>(from));
    tags_.insert(tags_.begin() + static_cast<std::ptrdiff_t>(to),
                 std::move(tag));
    if (selected_chip_ == static_cast<int>(from)) {
        selected_chip_ = static_cast<int>(to);
    }
    Invalidate();
    NotifyTagsChanged();
    return true;
}

void TagsInput::SetMax(int max) {
    max_ = max;
    Invalidate();
}

void TagsInput::SetAllowDuplicates(bool allow) {
    allow_duplicates_ = allow;
}

void TagsInput::SetValidate(
    std::function<bool(std::string_view)> validate) {
    validate_ = std::move(validate);
}

void TagsInput::SetAddOnBlur(bool enabled) {
    add_on_blur_ = enabled;
}

void TagsInput::SetPlaceholder(std::string placeholder) {
    placeholder_ = std::move(placeholder);
    Invalidate();
}

void TagsInput::SetSuggestions(std::vector<std::string> suggestions) {
    suggestions_ = std::move(suggestions);
    RefreshSuggestions();
}

void TagsInput::ClearSuggestions() {
    suggestions_.clear();
    CloseSuggestions();
}

bool TagsInput::IsSuggestionsOpen() const {
    const auto overlay = suggestions_overlay_.lock();
    return overlay != nullptr && GetTree() != nullptr &&
           GetTree()->GetOverlay() == overlay;
}

bool TagsInput::CommitDraft() {
    if (!AddTag(draft_)) {
        return false;
    }
    draft_.clear();
    caret_ = 0;
    scroll_x_ = 0.0f;
    Invalidate();
    return true;
}

void TagsInput::NotifyTagsChanged() {
    if (on_tags_changed_) {
        on_tags_changed_(tags_);
    }
}

void TagsInput::InvalidateDraft() {
    Invalidate();
    RefreshSuggestions();
}

void TagsInput::MoveCaret(std::size_t position) {
    caret_ = std::min(position, draft_.size());
    Invalidate();
}

// -- suggestions overlay -------------------------------------------------

void TagsInput::RefreshSuggestions() {
    if (!IsFocused() || !IsEnabled() || suggestions_.empty() ||
        GetTree() == nullptr) {
        CloseSuggestions();
        return;
    }
    // Filter: case-insensitive substring on the draft; already-added
    // entries are hidden when duplicates are disallowed.
    const std::string needle = ToLower(draft_);
    std::vector<std::string> filtered;
    for (const auto& s : suggestions_) {
        if (filtered.size() >= kMaxSuggestions) {
            break;
        }
        if (!needle.empty() && ToLower(s).find(needle) == std::string::npos) {
            continue;
        }
        if (!allow_duplicates_ &&
            std::find(tags_.begin(), tags_.end(), s) != tags_.end()) {
            continue;
        }
        filtered.push_back(s);
    }
    if (filtered.empty() || (draft_.empty() && !arrow_open_)) {
        CloseSuggestions();
        return;
    }
    const auto existing = std::dynamic_pointer_cast<TagsSuggestionsOverlay>(
        suggestions_overlay_.lock());
    if (existing != nullptr && IsSuggestionsOpen()) {
        existing->Update(GetBounds(), filtered);
        return;
    }
    OpenSuggestions();
    if (const auto overlay =
            std::dynamic_pointer_cast<TagsSuggestionsOverlay>(
                suggestions_overlay_.lock())) {
        overlay->Update(GetBounds(), filtered);
    }
}

void TagsInput::OpenSuggestions() {
    if (GetTree() == nullptr || GetBounds().width <= 0.0f) {
        return;
    }
    // The tree has one overlay slot: never stomp somebody else's
    // (a dialog/menu shown over the field wins).
    if (GetTree()->GetOverlay() != nullptr && !IsSuggestionsOpen()) {
        return;
    }
    auto overlay = std::make_shared<TagsSuggestionsOverlay>(
        GetBounds(), std::vector<std::string>{});
    overlay->SetOwner(weak_from_this());
    suggestions_overlay_ = overlay;
    ShowOverlay(overlay);
}

void TagsInput::CloseSuggestions() {
    arrow_open_ = false;
    const auto overlay = suggestions_overlay_.lock();
    const bool ours = overlay != nullptr && GetTree() != nullptr &&
                      GetTree()->GetOverlay() == overlay;
    suggestions_overlay_.reset();
    if (ours) {
        HideOverlay();
    }
}

void TagsInput::PickSuggestion(const std::string& tag) {
    AddTag(tag);
    draft_.clear();
    caret_ = 0;
    scroll_x_ = 0.0f;
    CloseSuggestions();
    Invalidate();
}

// -- chip hit-testing -----------------------------------------------------

int TagsInput::ChipAt(const Layout::Point& position) const {
    for (std::size_t i = 0; i < chip_rects_.size(); ++i) {
        if (chip_rects_[i].Contains(position)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool TagsInput::ChipCloseContains(int chip,
                                const Layout::Point& position) const {
    if (chip < 0 || chip >= static_cast<int>(chip_rects_.size())) {
        return false;
    }
    const auto& r = chip_rects_[static_cast<std::size_t>(chip)];
    return position.x >= r.x + r.width - kChipCloseW - 2.0f &&
           position.x <= r.x + r.width;
}

// -- widget contract --------------------------------------------------------

void TagsInput::Render(Rendering::Renderer& renderer,
                       const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const bool enabled = IsEnabled();
    const bool focused = IsFocused();

    renderer.FillRect(bounds, enabled ? tokens.input : tokens.secondary,
                      tokens.radius_small);
    const Theme::Color border = !enabled
        ? tokens.border
        : focused ? tokens.ring
                  : (IsHovered() ? tokens.muted : tokens.border);
    renderer.StrokeRect(bounds, border, focused ? 1.5f : 1.0f,
                        tokens.radius_small);

    const Rendering::TextStyle chip_style{
        enabled ? tokens.foreground : tokens.disabled,
        tokens.font_small, 500};
    const Rendering::TextStyle draft_style{
        enabled ? tokens.foreground : tokens.disabled,
        tokens.font_body, 400};
    const float chip_h = std::max(18.0f, bounds.height - 2.0f * kChipInsetY);
    const float chip_radius = std::min(chip_h * 0.4f, tokens.radius_medium);

    // Measure chips first: their widths drive the draft origin, the
    // caret scroll and the hit rects (all in unscrolled content space).
    std::vector<float> chip_widths(tags_.size());
    float chips_w = 0.0f;
    for (std::size_t i = 0; i < tags_.size(); ++i) {
        const float text_w =
            renderer.MeasureText(tags_[i], chip_style).width;
        chip_widths[i] = kChipPadX + text_w + kChipCloseW + kChipPadX;
        chips_w += chip_widths[i] + (i > 0 ? kChipGap : 0.0f);
    }
    content_x_ = bounds.x + kFieldPadX + chips_w +
                 (tags_.empty() ? 0.0f : kChipGap);

    // Deferred caret placement (click-to-position needs MeasureText).
    if (pending_caret_x_.has_value()) {
        const float target = *pending_caret_x_ + scroll_x_ - content_x_;
        std::size_t best = draft_.size();
        for (std::size_t pos = 0; pos <= draft_.size();) {
            const float w = renderer.MeasureText(
                std::string_view(draft_).substr(0, pos),
                draft_style).width;
            if (w > target) {
                best = pos == 0 ? 0 : PreviousCodePoint(draft_, pos);
                break;
            }
            if (pos == draft_.size()) {
                break;
            }
            pos = NextCodePoint(draft_, pos);
        }
        caret_ = best;
        pending_caret_x_.reset();
    }

    // Horizontal scroll keeps the draft caret inside the field.
    const float draft_prefix_w = renderer.MeasureText(
        std::string_view(draft_).substr(0, caret_), draft_style).width;
    const float view_left = bounds.x + kFieldPadX;
    const float view_right = bounds.x + bounds.width - kFieldPadX;
    const float caret_px = content_x_ + draft_prefix_w - scroll_x_;
    if (caret_px > view_right) {
        scroll_x_ += caret_px - view_right;
    } else if (caret_px < view_left) {
        scroll_x_ = std::max(0.0f, scroll_x_ - (view_left - caret_px));
    }

    renderer.PushClip(bounds);
    float x = bounds.x + kFieldPadX - scroll_x_;
    chip_rects_.clear();
    chip_rects_.reserve(tags_.size());
    for (std::size_t i = 0; i < tags_.size(); ++i) {
        const float chip_w = chip_widths[i];
        const float text_w = chip_w - 2.0f * kChipPadX - kChipCloseW;
        const bool dragged = chip_dragging_ &&
                             pressed_chip_ == static_cast<int>(i);
        Layout::Rect rect{x, bounds.y + kChipInsetY, chip_w, chip_h};
        if (dragged) {
            rect.x = std::clamp(drag_x_ - chip_w * 0.5f,
                                bounds.x + 2.0f,
                                bounds.x + bounds.width - chip_w - 2.0f);
        }
        chip_rects_.push_back(rect);
        const bool selected = selected_chip_ == static_cast<int>(i);
        Theme::Color chip_fill = dragged ? tokens.hover : tokens.secondary;
        if (!enabled) {
            chip_fill = tokens.secondary;
        }
        renderer.FillRect(rect, chip_fill, chip_radius);
        if (selected && enabled) {
            // select-then-delete arm: accent ring marks the pending
            // removal target.
            renderer.StrokeRect(rect, tokens.accent, 1.0f, chip_radius);
        }
        const auto sample = tags_[i].empty()
            ? std::string_view("Ag")
            : std::string_view(tags_[i]);
        const float text_h = renderer.MeasureText(sample, chip_style).height;
        const float text_y = rect.y +
            std::max(0.0f, (chip_h - text_h) * 0.5f);
        renderer.DrawText(
            tags_[i],
            Layout::Rect{rect.x + kChipPadX, text_y,
                         text_w + 2.0f, text_h + 1.0f},
            chip_style);
        const Rendering::TextStyle close_style{
            enabled ? tokens.muted : tokens.disabled,
            tokens.font_small, 400};
        renderer.DrawText(
            "\xC3\x97", // ×
            Layout::Rect{rect.x + rect.width - kChipCloseW - 2.0f,
                         text_y, kChipCloseW, text_h + 1.0f},
            close_style);
        x += chip_w + kChipGap;
    }
    // Drop-insertion marker while reordering.
    if (chip_dragging_ && pressed_chip_ >= 0) {
        // Slot between chips nearest to the drag position.
        float insert_x = x - kChipGap * 0.5f;
        for (std::size_t i = 0; i < chip_rects_.size(); ++i) {
            const auto& r = chip_rects_[i];
            if (drag_x_ < r.x + r.width * 0.5f) {
                insert_x = r.x - kChipGap * 0.5f;
                break;
            }
        }
        renderer.DrawLine(
            {insert_x, bounds.y + kChipInsetY},
            {insert_x, bounds.y + bounds.height - kChipInsetY},
            tokens.accent, 2.0f);
    }

    // Draft text / placeholder / caret (x is the scrolled draft origin).
    const auto draft_sample = draft_.empty()
        ? std::string_view("Ag")
        : std::string_view(draft_);
    const float draft_h =
        renderer.MeasureText(draft_sample, draft_style).height;
    const float draft_y = bounds.y +
        std::max(0.0f, (bounds.height - draft_h) * 0.5f);
    if (draft_.empty() && tags_.empty() && !placeholder_.empty()) {
        const Rendering::TextStyle ph{tokens.muted, tokens.font_body, 400};
        renderer.DrawText(placeholder_,
                          Layout::Rect{x, draft_y,
                                       bounds.x + bounds.width - x - 4.0f,
                                       draft_h + 1.0f},
                          ph);
    } else {
        renderer.DrawText(draft_,
                          Layout::Rect{x, draft_y,
                                       bounds.x + bounds.width - x - 4.0f +
                                           draft_prefix_w,
                                       draft_h + 1.0f},
                          draft_style);
        if (focused && !AtMax()) {
            renderer.DrawLine(
                {x + draft_prefix_w, draft_y},
                {x + draft_prefix_w, draft_y + draft_h},
                tokens.foreground, 1.0f);
        }
    }
    renderer.PopClip();

    RenderFocusRing(renderer, tokens, tokens.radius_small);
}

void TagsInput::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (const auto overlay =
            std::dynamic_pointer_cast<TagsSuggestionsOverlay>(
                suggestions_overlay_.lock())) {
        // Keep the dropdown glued to the field across relayouts.
        RefreshSuggestions();
    }
}

void TagsInput::Tick(double dt_seconds) {
    (void)dt_seconds;
    if (!IsEffectivelyVisible()) {
        return;
    }
    // Widget-focus loss has no event in this kit — detect the flip and
    // apply the blur semantics (commit per addOnBlur, close the list).
    if (was_focused_ && !IsFocused()) {
        if (add_on_blur_ && !draft_.empty()) {
            CommitDraft();
        }
        selected_chip_ = -1;
        pressed_chip_ = -1;
        chip_dragging_ = false;
        CloseSuggestions();
    }
    if (!was_focused_ && IsFocused()) {
        RefreshSuggestions();
    }
    was_focused_ = IsFocused();
}

bool TagsInput::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }

    switch (event.type) {
        case Event::Type::MouseDown:
            if (event.mouse_button != MouseButton::Left) {
                return false;
            }
            suppress_click_ = false;
            {
                const int chip = ChipAt(event.position);
                if (chip >= 0) {
                    pressed_chip_ = chip;
                    press_x_ = event.position.x;
                    chip_dragging_ = false;
                    selected_chip_ = chip;
                    Invalidate();
                } else {
                    pressed_chip_ = -1;
                    selected_chip_ = -1;
                    pending_caret_x_ = event.position.x;
                    Invalidate();
                }
            }
            return true;
        case Event::Type::MouseMove:
            if (pressed_chip_ >= 0) {
                if (!chip_dragging_ &&
                    std::fabs(event.position.x - press_x_) >
                        kDragThreshold) {
                    chip_dragging_ = true;
                }
                if (chip_dragging_) {
                    drag_x_ = event.position.x;
                    Invalidate();
                }
                return true;
            }
            return false;
        case Event::Type::MouseUp:
            if (event.mouse_button != MouseButton::Left) {
                return false;
            }
            if (pressed_chip_ >= 0) {
                if (chip_dragging_) {
                    // Drop: slot whose midpoint is past the drag x goes
                    // after the moved chip.
                    std::size_t to = tags_.size();
                    for (std::size_t i = 0; i < chip_rects_.size(); ++i) {
                        const auto& r = chip_rects_[i];
                        if (static_cast<int>(i) == pressed_chip_) {
                            continue;
                        }
                        if (drag_x_ < r.x + r.width * 0.5f) {
                            to = i;
                            break;
                        }
                    }
                    if (to > static_cast<std::size_t>(pressed_chip_)) {
                        // Removing `from` first shifts later slots left.
                        to = to > 0 ? to - 1 : 0;
                    }
                    if (to >= tags_.size()) {
                        to = tags_.size() - 1;
                    }
                    suppress_click_ = true;
                    MoveTag(static_cast<std::size_t>(pressed_chip_), to);
                }
                pressed_chip_ = -1;
                chip_dragging_ = false;
                Invalidate();
                return true;
            }
            return false;
        case Event::Type::Click: {
            if (suppress_click_) {
                suppress_click_ = false;
                return true;
            }
            const int chip = ChipAt(event.position);
            if (chip >= 0 && ChipCloseContains(chip, event.position)) {
                RemoveTag(static_cast<std::size_t>(chip));
                return true;
            }
            return true;
        }
        case Event::Type::TextInput: {
            if (AtMax()) {
                return true; // read-only when full (drift `max`)
            }
            selected_chip_ = -1;
            // Paste/typing may carry separators: ',', '\t', '\n' split
            // into commits; the trailing fragment stays as the draft.
            std::string piece;
            for (const char c : event.text) {
                if (c == ',' || c == '\t' || c == '\n' || c == '\r') {
                    draft_.insert(caret_, piece);
                    CommitDraft();
                    piece.clear();
                } else {
                    piece += c;
                }
            }
            if (!piece.empty()) {
                draft_.insert(caret_, piece);
                caret_ += piece.size();
            }
            InvalidateDraft();
            return true;
        }
        case Event::Type::KeyDown:
            if (event.is_ctrl && event.key == KeyCode::V) {
                // Paste: split on , \t \n like typed separators.
                std::string text;
                if (::XER::Content::Clipboard::GetText(text)) {
                    Event paste = event;
                    paste.type = Event::Type::TextInput;
                    paste.text = std::move(text);
                    HandleEvent(paste);
                }
                return true;
            }
            switch (event.key) {
                case KeyCode::Backspace:
                    if (selected_chip_ >= 0) {
                        RemoveTag(static_cast<std::size_t>(selected_chip_));
                        return true;
                    }
                    if (caret_ > 0) {
                        const std::size_t prev =
                            PreviousCodePoint(draft_, caret_);
                        draft_.erase(prev, caret_ - prev);
                        caret_ = prev;
                        InvalidateDraft();
                        return true;
                    }
                    // Drift's select-then-delete: first Backspace on an
                    // empty draft selects the last chip.
                    if (!tags_.empty()) {
                        selected_chip_ =
                            static_cast<int>(tags_.size()) - 1;
                        Invalidate();
                        return true;
                    }
                    return false;
                case KeyCode::Delete:
                    if (selected_chip_ >= 0) {
                        RemoveTag(static_cast<std::size_t>(selected_chip_));
                        return true;
                    }
                    if (caret_ < draft_.size()) {
                        const std::size_t next =
                            NextCodePoint(draft_, caret_);
                        draft_.erase(caret_, next - caret_);
                        InvalidateDraft();
                        return true;
                    }
                    return false;
                case KeyCode::ArrowLeft:
                    if (selected_chip_ > 0) {
                        --selected_chip_;
                        Invalidate();
                        return true;
                    }
                    if (caret_ > 0) {
                        MoveCaret(PreviousCodePoint(draft_, caret_));
                        return true;
                    }
                    if (!tags_.empty()) {
                        selected_chip_ =
                            static_cast<int>(tags_.size()) - 1;
                        Invalidate();
                        return true;
                    }
                    return false;
                case KeyCode::ArrowRight:
                    if (selected_chip_ >= 0) {
                        if (selected_chip_ + 1 <
                            static_cast<int>(tags_.size())) {
                            ++selected_chip_;
                        } else {
                            selected_chip_ = -1;
                            caret_ = 0;
                        }
                        Invalidate();
                        return true;
                    }
                    if (caret_ < draft_.size()) {
                        MoveCaret(NextCodePoint(draft_, caret_));
                        return true;
                    }
                    return false;
                case KeyCode::Home:
                    MoveCaret(0);
                    return true;
                case KeyCode::End:
                    MoveCaret(draft_.size());
                    return true;
                case KeyCode::Enter:
                    // With suggestions open the overlay consumes Enter
                    // itself; reaching here means no active suggestion.
                    CommitDraft();
                    return true;
                case KeyCode::Escape:
                    if (selected_chip_ >= 0) {
                        selected_chip_ = -1;
                        Invalidate();
                        return true;
                    }
                    if (!draft_.empty()) {
                        draft_.clear();
                        caret_ = 0;
                        InvalidateDraft();
                        return true;
                    }
                    return false;
                case KeyCode::ArrowDown:
                    // Keyboard path into the suggestion list: open even
                    // with an empty draft so all entries show.
                    if (!suggestions_.empty() && IsFocused()) {
                        arrow_open_ = true;
                        RefreshSuggestions();
                        return IsSuggestionsOpen();
                    }
                    return false;
                default:
                    return false;
            }
        case Event::Type::Blur:
            if (add_on_blur_ && !draft_.empty()) {
                CommitDraft();
            }
            selected_chip_ = -1;
            pressed_chip_ = -1;
            chip_dragging_ = false;
            CloseSuggestions();
            return false;
        default:
            return false;
    }
}

std::string TagsInput::GetDebugText() const {
    std::string out;
    for (const auto& tag : tags_) {
        if (!out.empty()) {
            out += ", ";
        }
        out += tag;
    }
    return out;
}

Layout::Size TagsInput::GetMinSize() const {
    return {120.0f, 36.0f};
}

Layout::Size TagsInput::GetPreferredSize() const {
    float w = 160.0f;
    for (const auto& tag : tags_) {
        w += static_cast<float>(Utf8Length(tag)) * 6.6f +
             kChipPadX * 2.0f + kChipCloseW + kChipGap;
    }
    return {std::min(std::max(w, 120.0f), 480.0f), 36.0f};
}

Layout::Size TagsInput::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
