#ifndef XER_UI_WIDGETS_TAGS_INPUT_H
#define XER_UI_WIDGETS_TAGS_INPUT_H

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// drift-ui TagsInput: a single-line field mixing committed tag chips
// with an inline text draft. Enter or comma commits the draft as a tag;
// pasted text splits on ',' / '\t' / '\n'; Backspace on an empty draft
// selects the last chip (a second Backspace or Delete removes it —
// drift's select-then-delete), a chip's × removes it directly, and
// chips can be drag-reordered horizontally.
//
// SetSuggestions() enables a filter-as-you-type dropdown rendered in
// the overlay layer. The overlay is deliberately non-focusable so the
// field keeps focus and typing continues (it forwards key/text events
// the dropdown doesn't consume); picking a suggestion commits it.
class TagsInput final : public Widget {
public:
    explicit TagsInput(std::vector<std::string> tags = {});
    ~TagsInput() override;

    void SetTags(std::vector<std::string> tags);
    const std::vector<std::string>& GetTags() const { return tags_; }
    // Fires after every mutation (add/remove/reorder/programmatic set)
    // with the full tag list — drift's onValueChange.
    void SetOnTagsChanged(
        std::function<void(const std::vector<std::string>&)> callback);

    // Commits `tag` under the same rules as a typed commit (trim, max,
    // duplicates, validate). Returns false when rejected.
    bool AddTag(std::string tag);
    bool RemoveTag(std::size_t index);
    bool MoveTag(std::size_t from, std::size_t to);

    // draft `max`: at most this many tags; once reached the field is
    // read-only for input but chips stay removable. <= 0 = unlimited.
    void SetMax(int max);
    int GetMax() const { return max_; }
    // drift `allowDuplicates` — default false.
    void SetAllowDuplicates(bool allow);
    bool AllowsDuplicates() const { return allow_duplicates_; }
    // drift `validate(tag)` — commits the predicate rejects return false.
    void SetValidate(std::function<bool(std::string_view)> validate);
    // drift `addOnBlur`: losing focus commits a non-empty draft.
    void SetAddOnBlur(bool enabled);
    bool AddOnBlur() const { return add_on_blur_; }
    // Hint text shown while the field holds neither chips nor a draft.
    void SetPlaceholder(std::string placeholder);

    // Filter-as-you-type suggestion source. While focused and the
    // filtered set is non-empty, a dropdown lists matches
    // (case-insensitive substring, already-added entries hidden when
    // duplicates are disallowed, capped at 8 rows).
    void SetSuggestions(std::vector<std::string> suggestions);
    void ClearSuggestions();
    bool IsSuggestionsOpen() const;

    // Current uncommitted draft text and its byte-caret.
    const std::string& GetDraft() const { return draft_; }
    // Index of the select-then-delete chip, or -1.
    int SelectedChip() const { return selected_chip_; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;
    std::string GetDebugText() const override;

private:
    friend class TagsSuggestionsOverlay;

    // Commits the draft when it survives TrimDraft/validation. Called
    // from Enter, comma input, paste splits and suggestions picks.
    bool CommitDraft();
    static std::string Trim(const std::string& text);
    bool CanAdd(const std::string& tag) const;
    bool AtMax() const;
    void NotifyTagsChanged();
    void InvalidateDraft();
    // Suggestions overlay lifecycle (Select's open/close discipline).
    void RefreshSuggestions();
    void OpenSuggestions();
    void CloseSuggestions();
    void PickSuggestion(const std::string& tag);
    // Chip geometry — chip_rects_ is computed inside Render (widths
    // need a renderer to measure); hit-testing uses the last pass.
    int ChipAt(const Layout::Point& position) const;
    bool ChipCloseContains(int chip, const Layout::Point& position) const;
    void MoveCaret(std::size_t position);

    std::vector<std::string> tags_;
    std::string draft_;
    std::size_t caret_ = 0;
    std::string placeholder_;
    int max_ = 0;
    bool allow_duplicates_ = false;
    bool add_on_blur_ = false;
    std::function<bool(std::string_view)> validate_;
    std::function<void(const std::vector<std::string>&)> on_tags_changed_;

    // Chip layout + interaction state. chip_rects_ is refreshed during
    // Render (mutable for the same reason ContextMenu caches metrics).
    mutable std::vector<Layout::Rect> chip_rects_;
    float content_x_ = 0.0f;      // input-area x origin after chips (unscrolled)
    float scroll_x_ = 0.0f;       // horizontal overflow scroll
    int selected_chip_ = -1;
    int pressed_chip_ = -1;       // chip the active press started on
    float press_x_ = 0.0f;
    bool chip_dragging_ = false;
    float drag_x_ = 0.0f;
    bool suppress_click_ = false; // swallow the click that ends a drag
    bool arrow_open_ = false;     // suggestions opened via ArrowDown
    std::optional<float> pending_caret_x_; // click-to-caret, applied in Render
    bool was_focused_ = false;

    std::vector<std::string> suggestions_;
    // The open suggestions overlay (TagsSuggestionsOverlay, defined in
    // tags_input.cc). Weak — the tree owns it while shown.
    std::weak_ptr<Widget> suggestions_overlay_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_TAGS_INPUT_H
