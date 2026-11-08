#include "test_harness.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rendering/renderer.h"
#include "theme/theme.h"
#include "widgets/button.h"
#include "widgets/checkbox.h"
#include "widgets/label.h"
#include "widgets/switch.h"
#include "widgets/text_input.h"
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
    XER_UI_EXPECT_TRUE(!renderer.strokes.empty());
    XER_UI_EXPECT_EQ(std::string{"Save"}, renderer.drawn_text.back());

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
