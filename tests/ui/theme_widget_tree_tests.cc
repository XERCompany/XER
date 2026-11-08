#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rendering/renderer.h"
#include "theme/theme.h"
#include "widgets/widget_tree.h"
#include "test_harness.h"

namespace {

using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Layout::Size;
using XER::UI::Rendering::Renderer;
using XER::UI::Rendering::TextStyle;
using XER::UI::Theme::Color;
using XER::UI::Theme::Tokens;
using XER::UI::Widgets::Event;
using XER::UI::Widgets::KeyCode;
using XER::UI::Widgets::Widget;
using XER::UI::Widgets::WidgetTree;

class TestRenderer final : public Renderer {
public:
    void BeginFrame(Rect) override { ++begin_frames; }
    void EndFrame() override { ++end_frames; }
    void FillRect(Rect, Color, float) override {}
    void StrokeRect(Rect, Color, float, float) override {}
    void DrawLine(Point, Point, Color, float) override {}
    void DrawText(std::string_view, Rect, TextStyle) override {}
    Size MeasureText(std::string_view, TextStyle) const override { return {0, 0}; }

    int begin_frames = 0;
    int end_frames = 0;
};

class ProbeWidget : public Widget {
public:
    explicit ProbeWidget(std::string name, std::vector<std::string>* render_order = nullptr)
        : name(std::move(name)), render_order(render_order) {}

    void Render(Renderer&, const Tokens&) override {
        if (render_order != nullptr) {
            render_order->push_back(name);
        }
    }

    void Layout(const Rect& bounds) override {
        SetBounds(bounds);
        for (const auto& child : GetChildren()) {
            child->Layout(bounds);
        }
    }

    bool HandleEvent(const Event& event) override {
        ++handled_events;
        last_event = event.type;
        last_text = event.text;
        return consumes_events;
    }

    Size GetMinSize() const override { return {0, 0}; }
    Size GetPreferredSize() const override { return {0, 0}; }
    Size GetMaxSize() const override { return {10000, 10000}; }
    Rect bounds_for_test() const { return GetBounds(); }

    std::string name;
    std::vector<std::string>* render_order;
    int handled_events = 0;
    Event::Type last_event = Event::Type::MouseMove;
    std::string last_text;
    bool consumes_events = true;
};

} // namespace

XER_UI_TEST(ThemeProviderUsesLightByDefault) {
    XER::UI::Theme::Provider provider;

    XER_UI_EXPECT_EQ(XER::UI::Theme::Variant::Light, provider.GetVariant());
    XER_UI_EXPECT_TRUE(provider.GetTokens().background.a >= 0.0f);
    XER_UI_EXPECT_TRUE(provider.GetTokens().background.a <= 1.0f);
}

XER_UI_TEST(ThemeProviderSwitchesToDark) {
    XER::UI::Theme::Provider provider;
    const auto light = provider.GetTokens();

    provider.SetVariant(XER::UI::Theme::Variant::Dark);
    const auto dark = provider.GetTokens();

    XER_UI_EXPECT_EQ(XER::UI::Theme::Variant::Dark, provider.GetVariant());
    XER_UI_EXPECT_FALSE(light.background == dark.background);
    XER_UI_EXPECT_FALSE(light.foreground == dark.foreground);
    XER_UI_EXPECT_FALSE(light.surface == dark.surface);
    XER_UI_EXPECT_FALSE(light.muted == dark.muted);
    XER_UI_EXPECT_FALSE(light.border == dark.border);
    XER_UI_EXPECT_FALSE(light.accent == dark.accent);
    XER_UI_EXPECT_FALSE(light.focus == dark.focus);
}

XER_UI_TEST(ThemeTokensUseNormalizedColorsAndPositiveMetrics) {
    XER::UI::Theme::Provider provider;
    const auto check_color = [](Color color) {
        return color.r >= 0.0f && color.r <= 1.0f &&
            color.g >= 0.0f && color.g <= 1.0f &&
            color.b >= 0.0f && color.b <= 1.0f &&
            color.a >= 0.0f && color.a <= 1.0f;
    };

    for (const auto variant : {XER::UI::Theme::Variant::Light, XER::UI::Theme::Variant::Dark}) {
        provider.SetVariant(variant);
        const auto tokens = provider.GetTokens();
        XER_UI_EXPECT_TRUE(check_color(tokens.background));
        XER_UI_EXPECT_TRUE(check_color(tokens.foreground));
        XER_UI_EXPECT_TRUE(check_color(tokens.surface));
        XER_UI_EXPECT_TRUE(check_color(tokens.muted));
        XER_UI_EXPECT_TRUE(check_color(tokens.border));
        XER_UI_EXPECT_TRUE(check_color(tokens.accent));
        XER_UI_EXPECT_TRUE(check_color(tokens.focus));
        XER_UI_EXPECT_TRUE(tokens.radius_small > 0.0f);
        XER_UI_EXPECT_TRUE(tokens.radius_medium >= tokens.radius_small);
        XER_UI_EXPECT_TRUE(tokens.spacing_small > 0.0f);
        XER_UI_EXPECT_TRUE(tokens.spacing_medium > tokens.spacing_small);
        XER_UI_EXPECT_TRUE(tokens.spacing_large > tokens.spacing_medium);
        XER_UI_EXPECT_TRUE(tokens.font_small > 0.0f);
        XER_UI_EXPECT_TRUE(tokens.font_body > tokens.font_small);
        XER_UI_EXPECT_TRUE(tokens.font_title > tokens.font_body);
    }
}

XER_UI_TEST(WidgetTreeSkipsHiddenAndDisabledControlsDuringFocusTraversal) {
    auto root = std::make_shared<ProbeWidget>("root");
    auto first = std::make_shared<ProbeWidget>("first");
    auto hidden = std::make_shared<ProbeWidget>("hidden");
    auto disabled = std::make_shared<ProbeWidget>("disabled");
    auto last = std::make_shared<ProbeWidget>("last");

    first->SetFocusable(true);
    hidden->SetFocusable(true);
    hidden->SetVisible(false);
    disabled->SetFocusable(true);
    disabled->SetEnabled(false);
    last->SetFocusable(true);
    root->AddChild(first);
    root->AddChild(hidden);
    root->AddChild(disabled);
    root->AddChild(last);

    WidgetTree tree;
    tree.SetRoot(root);

    Event tab;
    tab.type = Event::Type::KeyDown;
    tab.key = KeyCode::Tab;
    XER_UI_EXPECT_TRUE(tree.HandleEvent(tab));
    XER_UI_EXPECT_TRUE(first->IsFocused());
    XER_UI_EXPECT_FALSE(hidden->IsFocused());
    XER_UI_EXPECT_FALSE(disabled->IsFocused());
    XER_UI_EXPECT_FALSE(last->IsFocused());

    XER_UI_EXPECT_TRUE(tree.HandleEvent(tab));
    XER_UI_EXPECT_TRUE(last->IsFocused());

    XER_UI_EXPECT_TRUE(tree.HandleEvent(tab));
    XER_UI_EXPECT_TRUE(first->IsFocused());

    tab.is_shift = true;
    XER_UI_EXPECT_TRUE(tree.HandleEvent(tab));
    XER_UI_EXPECT_TRUE(last->IsFocused());
}

XER_UI_TEST(WidgetClearsDescendantFocusWhenHiddenOrDisabled) {
    auto container = std::make_shared<ProbeWidget>("container");
    auto child = std::make_shared<ProbeWidget>("child");
    child->SetFocusable(true);
    container->AddChild(child);

    child->SetFocused(true);
    container->SetVisible(false);
    XER_UI_EXPECT_FALSE(child->IsFocused());

    container->SetVisible(true);
    child->SetFocused(true);
    container->SetEnabled(false);
    XER_UI_EXPECT_FALSE(child->IsFocused());
}

XER_UI_TEST(WidgetTreeLaysOutAndRendersVisibleWidgetsDepthFirst) {
    std::vector<std::string> render_order;
    auto root = std::make_shared<ProbeWidget>("root", &render_order);
    auto child = std::make_shared<ProbeWidget>("child", &render_order);
    auto hidden = std::make_shared<ProbeWidget>("hidden", &render_order);
    auto grandchild = std::make_shared<ProbeWidget>("grandchild", &render_order);
    root->AddChild(child);
    root->AddChild(hidden);
    child->AddChild(grandchild);
    hidden->SetVisible(false);

    WidgetTree tree;
    tree.SetRoot(root);
    const Rect bounds{1.0f, 2.0f, 30.0f, 40.0f};
    tree.Layout(bounds);
    TestRenderer renderer;
    tree.Render(renderer, XER::UI::Theme::Provider{}.GetTokens());

    XER_UI_EXPECT_EQ(1, renderer.begin_frames);
    XER_UI_EXPECT_EQ(1, renderer.end_frames);
    XER_UI_EXPECT_EQ(bounds.x, root->bounds_for_test().x);
    XER_UI_EXPECT_EQ(bounds.width, root->bounds_for_test().width);
    XER_UI_EXPECT_EQ(3u, render_order.size());
    XER_UI_EXPECT_EQ(std::string{"root"}, render_order[0]);
    XER_UI_EXPECT_EQ(std::string{"child"}, render_order[1]);
    XER_UI_EXPECT_EQ(std::string{"grandchild"}, render_order[2]);
}

XER_UI_TEST(WidgetTreeDispatchesPointerEventsToTopmostHitWidget) {
    auto root = std::make_shared<ProbeWidget>("root");
    auto lower = std::make_shared<ProbeWidget>("lower");
    auto upper = std::make_shared<ProbeWidget>("upper");
    upper->SetFocusable(true);
    root->AddChild(lower);
    root->AddChild(upper);

    WidgetTree tree;
    tree.SetRoot(root);
    tree.Layout({0, 0, 100, 100});

    Event down;
    down.type = Event::Type::MouseDown;
    down.position = {5, 5};
    XER_UI_EXPECT_TRUE(tree.HandleEvent(down));
    XER_UI_EXPECT_EQ(0, lower->handled_events);
    XER_UI_EXPECT_EQ(1, upper->handled_events);
    XER_UI_EXPECT_TRUE(upper->IsFocused());
}

XER_UI_TEST(WidgetTreeRoutesKeyboardEventsToFocusedWidget) {
    auto root = std::make_shared<ProbeWidget>("root");
    auto child = std::make_shared<ProbeWidget>("child");
    child->SetFocusable(true);
    root->AddChild(child);

    WidgetTree tree;
    tree.SetRoot(root);

    Event tab;
    tab.type = Event::Type::KeyDown;
    tab.key = KeyCode::Tab;
    tree.HandleEvent(tab);

    Event character;
    character.type = Event::Type::TextInput;
    character.text = "\xC3\xA9";
    XER_UI_EXPECT_TRUE(tree.HandleEvent(character));
    XER_UI_EXPECT_EQ(1, child->handled_events);
    XER_UI_EXPECT_EQ(Event::Type::TextInput, child->last_event);
    XER_UI_EXPECT_EQ(std::string{"\xC3\xA9"}, child->last_text);
}
