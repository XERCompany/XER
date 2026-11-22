// Implementation of widget_kit_smoke.h — see the header comment.
// Logic-only checks: widget HandleEvent/Tick state machines need no
// renderer, so the suite stays headless like the rest of --smoke.
#include "widget_kit_smoke.h"

#include <memory>
#include <string>
#include <vector>

#include "theme/tokens.h"
#include "widgets/confirm.h"
#include "widgets/pie_menu.h"
#include "widgets/tags_input.h"
#include "widgets/widget.h"
#include "widgets/widget_tree.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace W = XER::UI::Widgets;

W::Event Mouse(W::Event::Type type, float x, float y,
               W::MouseButton button = W::MouseButton::Left) {
    W::Event ev{};
    ev.type = type;
    ev.position = {x, y};
    ev.mouse_button = button;
    return ev;
}

W::Event Key(W::KeyCode key) {
    W::Event ev{};
    ev.type = W::Event::Type::KeyDown;
    ev.key = key;
    return ev;
}

W::Event Text(const char* text) {
    W::Event ev{};
    ev.type = W::Event::Type::TextInput;
    ev.text = text;
    return ev;
}

// -- PieMenu ---------------------------------------------------------------

std::string CheckPieMenu() {
    using PieItem = W::PieMenu::Item;

    // Press-drag-release: dead-zone press highlights nothing, aiming
    // out to the top wedge and releasing selects it.
    int selected = -1;
    int highlighted = -1;
    bool fired_action = false;
    auto pie = W::PieMenu::Create({
        PieItem{"Cut", "", [&] { fired_action = true; }},
        PieItem{"Copy", "", nullptr},
        PieItem{"Paste", "", nullptr},
    });
    pie->SetCenter({400.0f, 300.0f});
    pie->SetOnSelect([&](int i) { selected = i; });
    pie->SetOnHighlight([&](int i) { highlighted = i; });
    pie->Layout({0.0f, 0.0f, 800.0f, 600.0f});
    pie->HandleEvent(Mouse(W::Event::Type::MouseDown, 400, 300));
    if (highlighted != -1 || pie->HighlightedIndex() != -1) {
        return "dead-zone press highlighted an item";
    }
    pie->HandleEvent(Mouse(W::Event::Type::MouseMove, 400, 190));
    if (highlighted != 0 || pie->HighlightedIndex() != 0) {
        return "aiming at the top wedge did not highlight item 0";
    }
    pie->HandleEvent(Mouse(W::Event::Type::MouseUp, 400, 190));
    if (selected != 0 || !fired_action) {
        return "release over the top wedge did not select item 0";
    }

    // Dead-zone release cancels (fires on_cancel, selects nothing).
    bool cancelled = false;
    selected = -1;
    auto pie2 = W::PieMenu::Create({
        PieItem{"A", "", nullptr},
        PieItem{"B", "", nullptr},
    });
    pie2->SetCenter({400.0f, 300.0f});
    pie2->SetOnCancel([&] { cancelled = true; });
    pie2->SetOnSelect([&](int i) { selected = i; });
    pie2->Layout({0.0f, 0.0f, 800.0f, 600.0f});
    pie2->HandleEvent(Mouse(W::Event::Type::MouseDown, 400, 300));
    // 10 px from center is still inside the 20 px dead zone.
    pie2->HandleEvent(Mouse(W::Event::Type::MouseMove, 410, 300));
    pie2->HandleEvent(Mouse(W::Event::Type::MouseUp, 410, 300));
    if (!cancelled || selected != -1) {
        return "dead-zone release did not cancel";
    }

    // Keyboard: arrows aim by direction, Enter fires. With 4 items the
    // wedge centers are top/right/bottom/left = indices 0..3.
    auto pie3 = W::PieMenu::Create({
        PieItem{"N", "", nullptr},
        PieItem{"E", "", nullptr},
        PieItem{"S", "", nullptr},
        PieItem{"W", "", nullptr},
    });
    pie3->SetCenter({400.0f, 300.0f});
    pie3->Layout({0.0f, 0.0f, 800.0f, 600.0f});
    int sel3 = -1;
    pie3->SetOnSelect([&](int i) { sel3 = i; });
    pie3->HandleEvent(Key(W::KeyCode::ArrowRight));
    if (pie3->HighlightedIndex() != 1) {
        return "ArrowRight did not aim at the right wedge";
    }
    pie3->HandleEvent(Key(W::KeyCode::Enter));
    if (sel3 != 1) {
        return "Enter did not fire the aimed wedge";
    }

    // Escape cancels.
    cancelled = false;
    auto pie4 = W::PieMenu::Create({PieItem{"A", "", nullptr}});
    pie4->SetCenter({400.0f, 300.0f});
    pie4->SetOnCancel([&] { cancelled = true; });
    pie4->Layout({0.0f, 0.0f, 800.0f, 600.0f});
    pie4->HandleEvent(Key(W::KeyCode::Escape));
    if (!cancelled) {
        return "Escape did not cancel";
    }

    // Click outside the ring cancels; click on a wedge selects.
    cancelled = false;
    selected = -1;
    auto pie5 = W::PieMenu::Create({
        PieItem{"N", "", nullptr},
        PieItem{"E", "", nullptr},
    });
    pie5->SetCenter({400.0f, 300.0f});
    pie5->SetOnCancel([&] { cancelled = true; });
    pie5->SetOnSelect([&](int i) { selected = i; });
    pie5->Layout({0.0f, 0.0f, 800.0f, 600.0f});
    pie5->HandleEvent(Mouse(W::Event::Type::Click, 10, 10));
    if (!cancelled) {
        return "click outside the ring did not cancel";
    }

    // Live re-flow: replacing items while open keeps the widget usable.
    auto pie6 = W::PieMenu::Create({
        PieItem{"one", "", nullptr},
        PieItem{"two", "", nullptr},
        PieItem{"three", "", nullptr},
        PieItem{"four", "", nullptr},
        PieItem{"five", "", nullptr},
        PieItem{"six", "", nullptr},
    });
    pie6->SetCenter({400.0f, 300.0f});
    pie6->Layout({0.0f, 0.0f, 800.0f, 600.0f});
    pie6->HandleEvent(Key(W::KeyCode::ArrowDown));
    if (pie6->HighlightedIndex() < 0) {
        return "arrow aim produced no highlight";
    }
    pie6->SetItems({PieItem{"only", "", nullptr}});
    if (pie6->HighlightedIndex() >= 1) {
        return "SetItems did not re-clamp the highlight";
    }

    return {};
}

// -- ConfirmButton -----------------------------------------------------------

std::string CheckConfirmButton() {
    int fired = 0;

    // ClickAgain: arm → fire once → re-arm → timeout lapse.
    auto b = std::make_shared<W::ConfirmButton>(
        "Delete", W::ButtonStyle::Destructive);
    b->Layout({0.0f, 0.0f, 120.0f, 36.0f});
    b->SetOnConfirm([&] { ++fired; });
    b->HandleEvent(Mouse(W::Event::Type::Click, 10, 10));
    if (!b->IsArmed()) {
        return "ClickAgain first click did not arm";
    }
    b->HandleEvent(Mouse(W::Event::Type::Click, 10, 10));
    if (fired != 1 || b->IsArmed()) {
        return "ClickAgain second click did not fire exactly once";
    }
    b->HandleEvent(Mouse(W::Event::Type::Click, 10, 10));
    if (!b->IsArmed()) {
        return "ClickAgain did not re-arm";
    }
    b->Tick(3.5);
    if (b->IsArmed()) {
        return "armed state did not lapse after the timeout";
    }

    // HoldToConfirm: full hold fires; early release does not.
    auto h = std::make_shared<W::ConfirmButton>(
        "Purge", W::ButtonStyle::Secondary);
    h->Layout({0.0f, 0.0f, 120.0f, 36.0f});
    h->SetConfirmMode(W::ConfirmMode::HoldToConfirm);
    h->SetHoldDuration(0.5);
    h->SetOnConfirm([&] { ++fired; });
    h->HandleEvent(Mouse(W::Event::Type::MouseDown, 10, 10));
    h->Tick(0.2);
    if (h->HoldProgress() <= 0.0f || h->HoldProgress() >= 1.0f) {
        return "hold progress did not advance";
    }
    h->Tick(0.4);
    if (fired != 2) {
        return "hold did not fire at the threshold";
    }
    h->HandleEvent(Mouse(W::Event::Type::MouseUp, 10, 10));

    auto h2 = std::make_shared<W::ConfirmButton>(
        "Purge", W::ButtonStyle::Secondary);
    h2->Layout({0.0f, 0.0f, 120.0f, 36.0f});
    h2->SetConfirmMode(W::ConfirmMode::HoldToConfirm);
    h2->SetHoldDuration(0.5);
    h2->SetOnConfirm([&] { ++fired; });
    h2->HandleEvent(Mouse(W::Event::Type::MouseDown, 10, 10));
    h2->Tick(0.1);
    h2->HandleEvent(Mouse(W::Event::Type::MouseUp, 10, 10));
    h2->Tick(1.0);
    if (fired != 2) {
        return "early release still confirmed";
    }

    // TypePhrase: partial phrase never fires, exact match fires.
    auto t = std::make_shared<W::ConfirmButton>(
        "Nuke", W::ButtonStyle::Destructive);
    t->Layout({0.0f, 0.0f, 160.0f, 36.0f});
    t->SetConfirmMode(W::ConfirmMode::TypePhrase);
    t->SetConfirmPhrase("DELETE");
    t->SetOnConfirm([&] { ++fired; });
    t->HandleEvent(Mouse(W::Event::Type::Click, 10, 10));
    if (!t->IsArmed()) {
        return "TypePhrase click did not arm";
    }
    t->HandleEvent(Text("DEL"));
    if (fired != 2 || t->GetTyped() != "DEL") {
        return "partial phrase fired or draft lost";
    }
    t->HandleEvent(Text("ETE"));
    if (fired != 3 || t->IsArmed()) {
        return "exact phrase did not fire once";
    }

    // Escape disarms without firing.
    auto e = std::make_shared<W::ConfirmButton>("Del");
    e->Layout({0.0f, 0.0f, 120.0f, 36.0f});
    e->SetOnConfirm([&] { ++fired; });
    e->HandleEvent(Mouse(W::Event::Type::Click, 10, 10));
    e->HandleEvent(Key(W::KeyCode::Escape));
    if (e->IsArmed() || fired != 3) {
        return "Escape did not disarm";
    }

    return {};
}

// -- TagsInput ----------------------------------------------------------------

std::string CheckTagsInput() {
    auto t = std::make_shared<W::TagsInput>();
    t->Layout({0.0f, 0.0f, 300.0f, 36.0f});
    t->SetFocused(true);
    std::vector<std::string> last;
    t->SetOnTagsChanged(
        [&](const std::vector<std::string>& v) { last = v; });

    // Enter commits the draft.
    t->HandleEvent(Text("red"));
    t->HandleEvent(Key(W::KeyCode::Enter));
    if (t->GetTags().size() != 1 || t->GetTags()[0] != "red" ||
        !t->GetDraft().empty()) {
        return "Enter did not commit the draft as a tag";
    }

    // Comma commits mid-typing; "a,b" splits into tag "a" + draft "b".
    t->HandleEvent(Text("a,b"));
    if (t->GetTags().size() != 2 || t->GetTags()[1] != "a" ||
        t->GetDraft() != "b") {
        return "comma did not split-commit typed text";
    }
    t->HandleEvent(Key(W::KeyCode::Enter));

    // Duplicates rejected by default.
    if (t->AddTag("red")) {
        return "duplicate tag was accepted";
    }
    t->SetAllowDuplicates(true);
    if (!t->AddTag("red")) {
        return "duplicate rejected while allowed";
    }

    // Select-then-delete: first Backspace on empty draft selects the
    // last chip, second removes it.
    const std::size_t before = t->GetTags().size();
    t->HandleEvent(Key(W::KeyCode::Backspace));
    if (t->SelectedChip() != static_cast<int>(before) - 1) {
        return "Backspace on empty draft did not select the last chip";
    }
    t->HandleEvent(Key(W::KeyCode::Backspace));
    if (t->GetTags().size() != before - 1) {
        return "second Backspace did not remove the selected chip";
    }

    // Reorder: MoveTag moves the element.
    auto r = std::make_shared<W::TagsInput>(
        std::vector<std::string>{"x", "y", "z"});
    if (!r->MoveTag(0, 2) || r->GetTags()[2] != "x" ||
        r->GetTags()[0] != "y") {
        return "MoveTag did not reorder";
    }

    // Max: field goes read-only at the cap but chips stay removable.
    auto m = std::make_shared<W::TagsInput>();
    m->SetMax(1);
    if (!m->AddTag("only") || m->AddTag("second")) {
        return "max cap not enforced";
    }
    if (!m->RemoveTag(0) || !m->GetTags().empty()) {
        return "removal blocked at max";
    }

    // Validate: commits the predicate rejects are refused.
    auto v = std::make_shared<W::TagsInput>();
    v->SetValidate([](std::string_view s) { return s.size() <= 3; });
    if (v->AddTag("toolong") || !v->AddTag("ok")) {
        return "validate predicate not honored";
    }

    // Suggestions: filter-as-you-type through a real (headless)
    // WidgetTree — the non-focusable overlay keeps field focus and
    // forwards typing.
    W::WidgetTree tree;
    auto s = std::make_shared<W::TagsInput>();
    tree.SetRoot(s);
    const XER::UI::Theme::Tokens tokens{};
    tree.Layout({0.0f, 0.0f, 400.0f, 400.0f}, tokens);
    s->SetFocused(true);
    s->SetSuggestions({"alpha", "beta", "alpine"});
    tree.HandleEvent(Text("al"));
    if (!s->IsSuggestionsOpen()) {
        return "suggestions did not open while typing";
    }
    // ArrowDown activates the first row, Enter picks it.
    tree.HandleEvent(Key(W::KeyCode::ArrowDown));
    tree.HandleEvent(Key(W::KeyCode::Enter));
    if (s->GetTags().size() != 1 || s->GetTags()[0] != "alpha") {
        return "suggestion pick did not commit 'alpha'";
    }
    // Typing after close stays a normal draft.
    tree.HandleEvent(Text("zz"));
    if (s->GetDraft() != "zz") {
        return "draft broken after suggestions closed";
    }
    if (s->IsSuggestionsOpen()) {
        return "suggestions reopened on a no-match draft";
    }
    tree.SetRoot(nullptr);

    if (last.empty()) {
        return "on_tags_changed never fired";
    }
    return {};
}

} // namespace

std::string WidgetKitSmoke() {
    if (const std::string err = CheckPieMenu(); !err.empty()) {
        return "pie_menu: " + err;
    }
    if (const std::string err = CheckConfirmButton(); !err.empty()) {
        return "confirm: " + err;
    }
    if (const std::string err = CheckTagsInput(); !err.empty()) {
        return "tags_input: " + err;
    }
    return {};
}

}  // namespace XER::VideoEditor::Ui
