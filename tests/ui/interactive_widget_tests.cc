#include "test_harness.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rendering/renderer.h"
#include "theme/theme.h"
#include "widgets/app_title_bar.h"
#include "widgets/button.h"
#include "widgets/checkbox.h"
#include "widgets/dialog.h"
#include "widgets/label.h"
#include "widgets/resizable.h"
#include "widgets/switch.h"
#include "widgets/table.h"
#include "widgets/text_input.h"
#include "widgets/textarea.h"
#include "widgets/widget_tree.h"

namespace {

using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Layout::Size;
using XER::UI::Rendering::Renderer;
using XER::UI::Rendering::TextStyle;
using XER::UI::Theme::Color;
using XER::UI::Theme::Tokens;
using XER::UI::Widgets::Button;
using XER::UI::Widgets::Checkbox;
using XER::UI::Widgets::Event;
using XER::UI::Widgets::KeyCode;
using XER::UI::Widgets::Label;
using XER::UI::Widgets::MouseButton;
using XER::UI::Widgets::Switch;
using XER::UI::Widgets::TextInput;
using XER::UI::Widgets::WidgetTree;
using XER::UI::Widgets::AppTitleBar;
using XER::UI::Widgets::Dialog;
using XER::UI::Widgets::Resizable;
using XER::UI::Widgets::Table;
using XER::UI::Widgets::Textarea;

class RecordingRenderer final : public Renderer {
public:
    void BeginFrame(Rect) override {}
    void EndFrame() override {}
    void FillRect(Rect, Color color, float) override { fills.push_back(color); }
    void StrokeRect(Rect, Color color, float, float) override { strokes.push_back(color); }
    void DrawLine(Point start, Point end, Color, float) override {
        lines.emplace_back(start, end);
    }
    void DrawText(std::string_view text, Rect, TextStyle) override {
        drawn_text.emplace_back(text);
    }
    Size MeasureText(std::string_view text, TextStyle style) const override {
        return {static_cast<float>(text.size()) * style.size * 0.5f, style.size};
    }

    std::vector<Color> fills;
    std::vector<Color> strokes;
    std::vector<std::string> drawn_text;
    std::vector<std::pair<Point, Point>> lines;
};

void SendKey(WidgetTree& tree, KeyCode key) {
    Event event;
    event.type = Event::Type::KeyDown;
    event.key = key;
    tree.HandleEvent(event);
}

void SendText(WidgetTree& tree, std::string text) {
    Event event;
    event.type = Event::Type::TextInput;
    event.text = std::move(text);
    tree.HandleEvent(event);
}

void SendPointerClick(WidgetTree& tree, Point point) {
    Event down;
    down.type = Event::Type::MouseDown;
    down.mouse_button = MouseButton::Left;
    down.position = point;
    tree.HandleEvent(down);

    Event up = down;
    up.type = Event::Type::MouseUp;
    tree.HandleEvent(up);
}

Event PointerEvent(Event::Type type, MouseButton button, Point point = {}) {
    Event event;
    event.type = type;
    event.mouse_button = button;
    event.position = point;
    return event;
}

} // namespace

XER_UI_TEST(ButtonActivatesOnceOnClickEnterOrSpace) {
    auto button = std::make_shared<Button>();
    XER_UI_EXPECT_EQ(std::string{}, button->GetText());
    XER_UI_EXPECT_TRUE(button->IsFocusable());
    button->SetText("Save");
    XER_UI_EXPECT_EQ(std::string{"Save"}, button->GetText());
    button->Layout({0, 0, 100, 32});

    RecordingRenderer renderer;
    const auto tokens = XER::UI::Theme::Provider{}.GetTokens();
    button->Render(renderer, tokens);
    XER_UI_EXPECT_TRUE(!renderer.fills.empty());
    XER_UI_EXPECT_EQ(std::string{"Save"}, renderer.drawn_text.back());

    // Secondary is borderless in the shadcn theme — the hairline stroke
    // path is covered by the Outline style instead.
    Button outlined("Outline", XER::UI::Widgets::ButtonStyle::Outline);
    outlined.Layout({0, 0, 100, 32});
    outlined.Render(renderer, tokens);
    XER_UI_EXPECT_TRUE(!renderer.strokes.empty());

    Label label;
    XER_UI_EXPECT_EQ(std::string{}, label.GetText());
    XER_UI_EXPECT_FALSE(label.IsFocusable());
    label.SetText("Status");
    label.Layout({0, 0, 80, 20});
    label.Render(renderer, tokens);
    XER_UI_EXPECT_EQ(std::string{"Status"}, label.GetText());
    XER_UI_EXPECT_TRUE(label.GetPreferredSize().width > 0.0f);
    XER_UI_EXPECT_EQ(std::string{"Status"}, renderer.drawn_text.back());

    WidgetTree tree;
    tree.SetRoot(button);
    tree.Layout({0, 0, 100, 32});
    int activations = 0;
    button->SetOnClick([&activations] { ++activations; });

    SendPointerClick(tree, {10, 10});
    XER_UI_EXPECT_EQ(1, activations);
    SendKey(tree, KeyCode::Enter);
    XER_UI_EXPECT_EQ(2, activations);
    SendKey(tree, KeyCode::Space);
    XER_UI_EXPECT_EQ(3, activations);
}

XER_UI_TEST(ButtonActivatesOnceForPointerDownUpClickAndRequiresLeftButton) {
    Button button("Action");
    button.Layout({0, 0, 80, 30});
    int activations = 0;
    button.SetOnClick([&activations] { ++activations; });

    button.HandleEvent(PointerEvent(Event::Type::MouseDown, MouseButton::Left, {5, 5}));
    button.HandleEvent(PointerEvent(Event::Type::MouseUp, MouseButton::Left, {5, 5}));
    button.HandleEvent(PointerEvent(Event::Type::Click, MouseButton::Left, {5, 5}));
    XER_UI_EXPECT_EQ(1, activations);

    button.HandleEvent(PointerEvent(Event::Type::Click, MouseButton::Middle, {5, 5}));
    button.HandleEvent(PointerEvent(Event::Type::Click, MouseButton::Right, {5, 5}));
    button.HandleEvent(PointerEvent(Event::Type::Click, MouseButton::None, {5, 5}));
    XER_UI_EXPECT_EQ(1, activations);

    auto shared_button = std::make_shared<Button>("Tree");
    WidgetTree tree;
    tree.SetRoot(shared_button);
    tree.Layout({0, 0, 80, 30});
    shared_button->SetOnClick([&activations] { ++activations; });
    tree.HandleEvent(PointerEvent(Event::Type::MouseDown, MouseButton::Left, {5, 5}));
    tree.HandleEvent(PointerEvent(Event::Type::MouseUp, MouseButton::Left, {5, 5}));
    tree.HandleEvent(PointerEvent(Event::Type::Click, MouseButton::Left, {5, 5}));
    XER_UI_EXPECT_EQ(2, activations);
}

XER_UI_TEST(CheckboxAndSwitchToggleOnClickOrSpace) {
    auto checkbox = std::make_shared<Checkbox>();
    auto toggle = std::make_shared<Switch>();
    XER_UI_EXPECT_FALSE(checkbox->IsChecked());
    XER_UI_EXPECT_FALSE(toggle->IsChecked());
    checkbox->Layout({0, 0, 24, 24});
    toggle->Layout({32, 0, 40, 24});

    RecordingRenderer renderer;
    const auto tokens = XER::UI::Theme::Provider{}.GetTokens();
    checkbox->Render(renderer, tokens);
    toggle->Render(renderer, tokens);
    XER_UI_EXPECT_TRUE(renderer.fills.size() >= 2);
    XER_UI_EXPECT_TRUE(renderer.strokes.size() >= 2);

    auto root = std::make_shared<Label>();
    root->Layout({0, 0, 100, 30});
    root->AddChild(checkbox);
    root->AddChild(toggle);
    WidgetTree tree;
    tree.SetRoot(root);
    tree.Layout({0, 0, 100, 30});

    int checkbox_changes = 0;
    int switch_changes = 0;
    checkbox->SetOnChanged([&checkbox_changes](bool) { ++checkbox_changes; });
    toggle->SetOnChanged([&switch_changes](bool) { ++switch_changes; });

    SendPointerClick(tree, {8, 8});
    SendPointerClick(tree, {40, 8});
    XER_UI_EXPECT_TRUE(checkbox->IsChecked());
    XER_UI_EXPECT_TRUE(toggle->IsChecked());
    XER_UI_EXPECT_EQ(1, checkbox_changes);
    XER_UI_EXPECT_EQ(1, switch_changes);

    checkbox->HandleEvent(PointerEvent(Event::Type::Click, MouseButton::Middle, {8, 8}));
    checkbox->HandleEvent(PointerEvent(Event::Type::Click, MouseButton::None, {8, 8}));
    toggle->HandleEvent(PointerEvent(Event::Type::Click, MouseButton::Right, {40, 8}));
    toggle->HandleEvent(PointerEvent(Event::Type::Click, MouseButton::None, {40, 8}));
    XER_UI_EXPECT_TRUE(checkbox->IsChecked());
    XER_UI_EXPECT_TRUE(toggle->IsChecked());
    XER_UI_EXPECT_EQ(1, checkbox_changes);
    XER_UI_EXPECT_EQ(1, switch_changes);

    SendKey(tree, KeyCode::Space);
    XER_UI_EXPECT_FALSE(toggle->IsChecked());
    XER_UI_EXPECT_EQ(2, switch_changes);
}

XER_UI_TEST(SettersNotifyOnlyWhenValuesChange) {
    TextInput input("before");
    int value_changes = 0;
    input.SetOnValueChanged([&value_changes](const std::string&) { ++value_changes; });
    input.SetValue("before");
    XER_UI_EXPECT_EQ(0, value_changes);
    input.SetValue("after");
    XER_UI_EXPECT_EQ(1, value_changes);
    input.SetValue("after");
    XER_UI_EXPECT_EQ(1, value_changes);

    Checkbox checkbox;
    int checkbox_changes = 0;
    checkbox.SetOnChanged([&checkbox_changes](bool) { ++checkbox_changes; });
    checkbox.SetChecked(false);
    XER_UI_EXPECT_EQ(0, checkbox_changes);
    checkbox.SetChecked(true);
    XER_UI_EXPECT_EQ(1, checkbox_changes);
    checkbox.SetChecked(true);
    XER_UI_EXPECT_EQ(1, checkbox_changes);

    Switch toggle;
    int switch_changes = 0;
    toggle.SetOnChanged([&switch_changes](bool) { ++switch_changes; });
    toggle.SetChecked(false);
    XER_UI_EXPECT_EQ(0, switch_changes);
    toggle.SetChecked(true);
    XER_UI_EXPECT_EQ(1, switch_changes);
    toggle.SetChecked(true);
    XER_UI_EXPECT_EQ(1, switch_changes);
}

XER_UI_TEST(TextInputReceivesUtf8AndEditsAtCaret) {
    TextInput default_input;
    XER_UI_EXPECT_EQ(std::string{}, default_input.GetValue());
    XER_UI_EXPECT_TRUE(default_input.IsFocusable());

    auto input = std::make_shared<TextInput>("A");
    WidgetTree tree;
    tree.SetRoot(input);
    tree.Layout({0, 0, 120, 32});

    Event focus;
    focus.type = Event::Type::Click;
    focus.position = {4, 4};
    tree.HandleEvent(focus);

    std::vector<std::string> changes;
    input->SetOnValueChanged([&changes](const std::string& value) {
        changes.push_back(value);
    });

    SendText(tree, "\xC3\xA9");
    XER_UI_EXPECT_EQ(std::string("A\xC3\xA9"), input->GetValue());
    SendKey(tree, KeyCode::ArrowLeft);
    SendText(tree, "\xF0\x9F\x99\x82");
    XER_UI_EXPECT_EQ(std::string("A\xF0\x9F\x99\x82\xC3\xA9"), input->GetValue());
    SendKey(tree, KeyCode::Backspace);
    XER_UI_EXPECT_EQ(std::string("A\xC3\xA9"), input->GetValue());

    SendKey(tree, KeyCode::Home);
    SendKey(tree, KeyCode::Delete);
    XER_UI_EXPECT_EQ(std::string("\xC3\xA9"), input->GetValue());
    XER_UI_EXPECT_EQ(std::size_t{4}, changes.size());
}

XER_UI_TEST(TextInputCaretMovementChangesRenderedCaretPosition) {
    auto input = std::make_shared<TextInput>("abcd");
    WidgetTree tree;
    tree.SetRoot(input);
    tree.Layout({0, 0, 120, 32});
    Event focus;
    focus.type = Event::Type::Click;
    focus.mouse_button = MouseButton::Left;
    focus.position = {4, 4};
    tree.HandleEvent(focus);

    RecordingRenderer renderer;
    const auto tokens = XER::UI::Theme::Provider{}.GetTokens();
    input->Render(renderer, tokens);
    XER_UI_EXPECT_EQ(std::size_t{1}, renderer.lines.size());
    const float initial_caret_x = renderer.lines.back().first.x;

    SendKey(tree, KeyCode::ArrowLeft);
    renderer.lines.clear();
    input->Render(renderer, tokens);
    XER_UI_EXPECT_EQ(std::size_t{1}, renderer.lines.size());
    XER_UI_EXPECT_TRUE(renderer.lines.back().first.x < initial_caret_x);
}

XER_UI_TEST(DisabledControlsDoNotActivate) {
    auto button = std::make_shared<Button>("Disabled");
    auto checkbox = std::make_shared<Checkbox>();
    auto toggle = std::make_shared<Switch>();
    button->SetEnabled(false);
    checkbox->SetEnabled(false);
    toggle->SetEnabled(false);

    int button_activations = 0;
    int checkbox_changes = 0;
    int switch_changes = 0;
    button->SetOnClick([&button_activations] { ++button_activations; });
    checkbox->SetOnChanged([&checkbox_changes](bool) { ++checkbox_changes; });
    toggle->SetOnChanged([&switch_changes](bool) { ++switch_changes; });

    Event pointer;
    pointer.type = Event::Type::MouseDown;
    pointer.mouse_button = MouseButton::Left;
    pointer.position = {2, 2};
    button->HandleEvent(pointer);
    checkbox->HandleEvent(pointer);
    toggle->HandleEvent(pointer);

    Event key;
    key.type = Event::Type::KeyDown;
    key.key = KeyCode::Enter;
    button->HandleEvent(key);
    key.key = KeyCode::Space;
    checkbox->HandleEvent(key);
    toggle->HandleEvent(key);

    XER_UI_EXPECT_EQ(0, button_activations);
    XER_UI_EXPECT_EQ(0, checkbox_changes);
    XER_UI_EXPECT_EQ(0, switch_changes);
    XER_UI_EXPECT_FALSE(checkbox->IsChecked());
    XER_UI_EXPECT_FALSE(toggle->IsChecked());
}

XER_UI_TEST(DialogSwallowsUnhandledKeys) {
    auto dialog = Dialog::Create("Title", nullptr, "OK", "Cancel");
    dialog->Layout({0, 0, 800, 600});

    Event key;
    key.type = Event::Type::KeyDown;
    // A modal dialog must consume every key, not just Escape/Enter —
    // otherwise shortcuts fire behind it.
    for (const KeyCode k : {KeyCode::A, KeyCode::ArrowLeft, KeyCode::F5,
                            KeyCode::Backspace}) {
        key.key = k;
        XER_UI_EXPECT_TRUE(dialog->HandleEvent(key));
    }

    int closed = 0;
    dialog->SetOnClosed([&closed] { ++closed; });
    key.key = KeyCode::Enter;
    XER_UI_EXPECT_TRUE(dialog->HandleEvent(key));
    XER_UI_EXPECT_TRUE(dialog->WasConfirmed());
    XER_UI_EXPECT_EQ(1, closed);
}

XER_UI_TEST(TableScrollbarStripDoesNotStealClicksOrRearmOnClick) {
    auto table = Table::Create();
    table->SetColumns({{Table::Column{"Name", 0.0f, false}}});
    std::vector<std::vector<std::string>> rows(10, {"row"});
    table->SetRows(rows);
    table->Layout({0, 0, 200, 50});
    // Header 30px + 20px view, 300px content → scrollable; strip is the
    // rightmost 6px starting at x=194, y in [30,50).

    int row_clicks = 0;
    table->SetOnRowClick([&row_clicks](int) { ++row_clicks; });

    // A click inside the scrollbar strip still resolves as a row click —
    // it must select the row, not arm the scrollbar drag.
    XER_UI_EXPECT_TRUE(table->HandleEvent(
        PointerEvent(Event::Type::Click, MouseButton::Left, {197, 40})));
    XER_UI_EXPECT_EQ(1, row_clicks);
    XER_UI_EXPECT_EQ(0, table->GetSelectedRow());
    // ...and it must not have armed the drag: the next move reports
    // unhandled (hover only) instead of scrolling.
    XER_UI_EXPECT_FALSE(table->HandleEvent(
        PointerEvent(Event::Type::MouseMove, MouseButton::None, {197, 35})));

    // A real left press on the strip arms the drag; only a left release
    // ends it.
    XER_UI_EXPECT_TRUE(table->HandleEvent(
        PointerEvent(Event::Type::MouseDown, MouseButton::Left, {197, 40})));
    XER_UI_EXPECT_TRUE(table->HandleEvent(
        PointerEvent(Event::Type::MouseMove, MouseButton::Left, {197, 35})));
    XER_UI_EXPECT_FALSE(table->HandleEvent(
        PointerEvent(Event::Type::MouseUp, MouseButton::Right, {197, 35})));
    XER_UI_EXPECT_TRUE(table->HandleEvent(
        PointerEvent(Event::Type::MouseMove, MouseButton::Left, {197, 38})));
    XER_UI_EXPECT_TRUE(table->HandleEvent(
        PointerEvent(Event::Type::MouseUp, MouseButton::Left, {197, 38})));
    XER_UI_EXPECT_FALSE(table->HandleEvent(
        PointerEvent(Event::Type::MouseMove, MouseButton::Left, {197, 42})));
}

XER_UI_TEST(TableSetRowsClearsHoverHighlight) {
    auto table = Table::Create();
    table->SetColumns({{Table::Column{"Name", 0.0f, false}}});
    table->SetRows({{"a"}, {"b"}});
    table->Layout({0, 0, 200, 200});
    const auto tokens = XER::UI::Theme::Provider{}.GetTokens();

    // Hover row 0, then replace the rows: the hover fill must disappear.
    table->HandleEvent(
        PointerEvent(Event::Type::MouseMove, MouseButton::None, {50, 45}));
    RecordingRenderer renderer;
    table->Render(renderer, tokens);
    const bool had_hover_fill = std::any_of(
        renderer.fills.begin(), renderer.fills.end(),
        [&tokens](const Color& c) { return c == tokens.hover; });
    XER_UI_EXPECT_TRUE(had_hover_fill);

    table->SetRows({{"c"}, {"d"}});
    renderer.fills.clear();
    table->Render(renderer, tokens);
    const bool still_hover = std::any_of(
        renderer.fills.begin(), renderer.fills.end(),
        [&tokens](const Color& c) { return c == tokens.hover; });
    XER_UI_EXPECT_FALSE(still_hover);
}

XER_UI_TEST(ResizableCollapseExpandAndLeftOnlyDragEnd) {
    auto resizable =
        Resizable::Create(Resizable::Direction::Horizontal);
    auto left_label = std::make_shared<Label>();
    auto right_label = std::make_shared<Label>();
    Resizable::Panel left_panel;
    left_panel.widget = left_label;
    left_panel.size = 0.5f;
    Resizable::Panel right_panel;
    right_panel.widget = right_label;
    right_panel.size = 0.5f;
    resizable->AddPanel(left_panel);
    resizable->AddPanel(right_panel);
    resizable->Layout({0, 0, 400, 100});
    // usable span = 400 - 6 = 394 → 197/197 split, handle at x in [194,206].

    XER_UI_EXPECT_FALSE(resizable->IsCollapsed(0));
    resizable->SetCollapsed(0, true);
    XER_UI_EXPECT_TRUE(resizable->IsCollapsed(0));
    resizable->Layout({0, 0, 400, 100});
    XER_UI_EXPECT_EQ(0.0f, left_label->GetBounds().width);
    XER_UI_EXPECT_TRUE(right_label->GetBounds().width > 390.0f);

    resizable->SetCollapsed(0, false);
    resizable->Layout({0, 0, 400, 100});
    XER_UI_EXPECT_FALSE(resizable->IsCollapsed(0));
    XER_UI_EXPECT_TRUE(std::abs(left_label->GetBounds().width - 197.0f) < 1.0f);

    // Splitter drag: delta is measured against the usable span
    // (394 = 400 - one 6px handle), not the full 400.
    XER_UI_EXPECT_TRUE(resizable->HandleEvent(
        PointerEvent(Event::Type::MouseDown, MouseButton::Left, {200, 50})));
    XER_UI_EXPECT_TRUE(resizable->HandleEvent(
        PointerEvent(Event::Type::MouseMove, MouseButton::Left, {297, 50})));
    resizable->Layout({0, 0, 400, 100});
    // 0.5 + 97/394 ≈ 0.7462 → ~294px; the old full-span math gives ~292.5.
    XER_UI_EXPECT_TRUE(std::abs(left_label->GetBounds().width - 294.0f) < 0.5f);

    // A non-left release must not end the drag.
    XER_UI_EXPECT_FALSE(resizable->HandleEvent(
        PointerEvent(Event::Type::MouseUp, MouseButton::Right, {297, 50})));
    XER_UI_EXPECT_TRUE(resizable->HandleEvent(
        PointerEvent(Event::Type::MouseMove, MouseButton::Left, {320, 50})));
    XER_UI_EXPECT_TRUE(resizable->HandleEvent(
        PointerEvent(Event::Type::MouseUp, MouseButton::Left, {320, 50})));
}

XER_UI_TEST(TextareaIgnoresKeysAndTextWhenUnfocused) {
    Textarea area("hello");
    area.Layout({0, 0, 200, 100});

    Event key;
    key.type = Event::Type::KeyDown;
    key.key = KeyCode::Backspace;
    XER_UI_EXPECT_FALSE(area.HandleEvent(key));
    XER_UI_EXPECT_EQ(std::string{"hello"}, area.GetText());

    Event text;
    text.type = Event::Type::TextInput;
    text.text = "x";
    XER_UI_EXPECT_FALSE(area.HandleEvent(text));
    XER_UI_EXPECT_EQ(std::string{"hello"}, area.GetText());

    area.SetFocused(true);
    XER_UI_EXPECT_TRUE(area.HandleEvent(text));
    XER_UI_EXPECT_EQ(std::string{"xhello"}, area.GetText());
}

XER_UI_TEST(AppTitleBarHitZonesValidAfterLayoutBeforeFirstRender) {
    AppTitleBar bar("Studio");
    std::vector<AppTitleBar::Menu> menus;
    AppTitleBar::Menu file;
    file.label = "File";
    file.items.push_back(AppTitleBar::MenuItem{"Open", "", {}, true, false,
                                               false, false});
    menus.push_back(std::move(file));
    bar.SetMenus(std::move(menus));
    bar.Layout({0, 0, 1000, 38});

    // No Render() has run — hit zones must already be live.
    XER_UI_EXPECT_TRUE(bar.IsInteractiveAt({977, 19}));   // close button
    XER_UI_EXPECT_TRUE(bar.IsInteractiveAt({931, 19}));   // maximize
    XER_UI_EXPECT_TRUE(bar.IsInteractiveAt({10, 19}));    // workspace pill
    XER_UI_EXPECT_TRUE(bar.IsInteractiveAt({500, 19}));   // center pill
    XER_UI_EXPECT_FALSE(bar.IsInteractiveAt({2, 19}));    // padding gap
}
