#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "content/css/computed_style.h"
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

// Lays out the first two children side by side (left/right halves) so
// pointer tests can distinguish hit targets.
class SplitProbe final : public ProbeWidget {
public:
    using ProbeWidget::ProbeWidget;

    void Layout(const Rect& bounds) override {
        SetBounds(bounds);
        const auto& kids = GetChildren();
        if (kids.size() >= 2) {
            kids[0]->Layout({bounds.x, bounds.y,
                             bounds.width * 0.5f, bounds.height});
            kids[1]->Layout({bounds.x + bounds.width * 0.5f, bounds.y,
                             bounds.width * 0.5f, bounds.height});
        }
    }
};

// Records transform/layer/clip traffic so CSS-transform composition
// and exception unwinding can be asserted call-for-call.
class TransformRenderer final : public Renderer {
public:
    struct Matrix {
        float a, b, c, d, tx, ty;
    };

    void BeginFrame(Rect) override {}
    void EndFrame() override {}
    void FillRect(Rect, Color, float) override {}
    void StrokeRect(Rect, Color, float, float) override {}
    void DrawLine(Point, Point, Color, float) override {}
    void DrawText(std::string_view, Rect, TextStyle) override {}
    Size MeasureText(std::string_view, TextStyle) const override {
        return {0, 0};
    }
    void SetTransform(float a, float b, float c, float d, float tx,
                      float ty) override {
        sets.push_back({a, b, c, d, tx, ty});
    }
    void ResetTransform() override { ++resets; }
    void PushLayer(float, std::uint32_t, float) override { ++pushes; }
    void PopLayer() override { ++pops; }
    void PushClip(Rect) override { ++clip_pushes; }
    void PopClip() override { ++clip_pops; }

    std::vector<Matrix> sets;
    int resets = 0;
    int pushes = 0;
    int pops = 0;
    int clip_pushes = 0;
    int clip_pops = 0;
};

// Writes a computed-style property directly (StyleValue() reads only
// computed styles — no CSS cascade needed for these tests). Merges
// into any style already on the widget: SetComputedStyle replaces the
// whole set, so repeated calls would clobber earlier properties.
void SetStyleProp(Widget& w, std::string prop, std::string value) {
    auto style =
        std::make_shared<XER::Content::CSS::ComputedStyle>();
    if (const auto prev = w.GetComputedStyle()) {
        for (const auto& [k, v] : prev->values()) {
            style->Set(k, v);
        }
    }
    style->Set(std::move(prop), std::move(value));
    w.SetComputedStyle(std::move(style));
}

class ThrowOnRender final : public ProbeWidget {
public:
    using ProbeWidget::ProbeWidget;
    void Render(Renderer&, const Tokens&) override {
        throw std::runtime_error("render boom");
    }
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

    // Probes must not consume the Tab key — a focused widget that eats
    // Tab keeps it from reaching the traversal path (real widgets like
    // TextInput return false and let the tree move focus).
    for (const auto& w : {first, hidden, disabled, last}) {
        w->consumes_events = false;
    }
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
    // Two events: the synthetic Focus notification the tree delivers on
    // every focus change (views contract), then the MouseDown itself.
    XER_UI_EXPECT_EQ(2, upper->handled_events);
    XER_UI_EXPECT_EQ(Event::Type::MouseDown, upper->last_event);
    XER_UI_EXPECT_TRUE(upper->IsFocused());
}

XER_UI_TEST(WidgetTreeDisabledOverlayLetsClicksFallThrough) {
    auto root = std::make_shared<ProbeWidget>("root");
    auto lower = std::make_shared<ProbeWidget>("lower");
    auto overlay = std::make_shared<ProbeWidget>("overlay");
    overlay->SetEnabled(false);
    root->AddChild(lower);
    root->AddChild(overlay);

    WidgetTree tree;
    tree.SetRoot(root);
    tree.Layout({0, 0, 100, 100});

    Event down;
    down.type = Event::Type::MouseDown;
    down.position = {5, 5};
    XER_UI_EXPECT_TRUE(tree.HandleEvent(down));
    XER_UI_EXPECT_EQ(1, lower->handled_events);
    XER_UI_EXPECT_EQ(0, overlay->handled_events);
}

XER_UI_TEST(WidgetTreeSuppressesGlobalKeyHandlerWhileOverlayShown) {
    auto root = std::make_shared<ProbeWidget>("root");
    auto overlay = std::make_shared<ProbeWidget>("overlay");
    overlay->consumes_events = false;

    WidgetTree tree;
    tree.SetRoot(root);
    tree.Layout({0, 0, 100, 100});

    int handled = 0;
    tree.SetKeyHandler([&handled](const Event&) {
        ++handled;
        return true;
    });

    Event key;
    key.type = Event::Type::KeyDown;
    key.key = KeyCode::A;
    XER_UI_EXPECT_TRUE(tree.HandleEvent(key));
    XER_UI_EXPECT_EQ(1, handled);

    tree.ShowOverlay(overlay);
    // Modal overlay owns input: an unconsumed key must not reach the
    // global shortcut handler behind it.
    XER_UI_EXPECT_FALSE(tree.HandleEvent(key));
    XER_UI_EXPECT_EQ(1, handled);

    tree.HideOverlay();
    XER_UI_EXPECT_TRUE(tree.HandleEvent(key));
    XER_UI_EXPECT_EQ(2, handled);
}

XER_UI_TEST(WidgetTreeRefreshesHoverAfterCaptureRelease) {
    auto root = std::make_shared<SplitProbe>("root");
    auto left = std::make_shared<ProbeWidget>("left");
    auto right = std::make_shared<ProbeWidget>("right");
    root->AddChild(left);
    root->AddChild(right);

    WidgetTree tree;
    tree.SetRoot(root);
    tree.Layout({0, 0, 100, 50});

    Event move;
    move.type = Event::Type::MouseMove;
    move.position = {75, 25};
    tree.HandleEvent(move);
    XER_UI_EXPECT_TRUE(right->IsHovered());
    XER_UI_EXPECT_FALSE(left->IsHovered());

    Event down;
    down.type = Event::Type::MouseDown;
    down.mouse_button = XER::UI::Widgets::MouseButton::Left;
    down.position = {25, 25};
    tree.HandleEvent(down);
    XER_UI_EXPECT_TRUE(left->IsHovered());
    XER_UI_EXPECT_FALSE(right->IsHovered());

    // While captured, hover stays pinned to the pressed widget.
    tree.HandleEvent(move);
    XER_UI_EXPECT_TRUE(left->IsHovered());
    XER_UI_EXPECT_FALSE(right->IsHovered());

    // Releasing over the other half must restore hover to what's actually
    // under the cursor instead of leaving a stale highlight behind.
    Event up = down;
    up.type = Event::Type::MouseUp;
    up.position = {75, 25};
    tree.HandleEvent(up);
    XER_UI_EXPECT_FALSE(left->IsHovered());
    XER_UI_EXPECT_TRUE(right->IsHovered());
}

XER_UI_TEST(WidgetTreeComposesNestedTransforms) {
    // root: translate(10,20); child: scale(2); sibling: none.
    // The child must see parent ∘ child — not an absolute replace —
    // and the sibling must see the parent's transform restored.
    auto root = std::make_shared<ProbeWidget>("root");
    auto child = std::make_shared<ProbeWidget>("child");
    auto sibling = std::make_shared<ProbeWidget>("sibling");
    root->AddChild(child);
    root->AddChild(sibling);
    SetStyleProp(*root, "transform", "translate(10,20)");
    SetStyleProp(*child, "transform", "scale(2)");

    WidgetTree tree;
    tree.SetRoot(root);
    tree.Layout({0, 0, 100, 100});
    TransformRenderer renderer;
    tree.Render(renderer, Tokens{});

    // Expected SetTransform stream:
    //   root      -> (1,0,0,1,10,20)
    //   child     -> translate ∘ scale = (2,0,0,2,10,20)
    //   unwind    -> parent restored for sibling = (1,0,0,1,10,20)
    XER_UI_EXPECT_EQ(3u, renderer.sets.size());
    if (renderer.sets.size() == 3) {
        const auto& s0 = renderer.sets[0];
        XER_UI_EXPECT_TRUE(s0.a == 1.f && s0.d == 1.f &&
                           s0.tx == 10.f && s0.ty == 20.f);
        const auto& s1 = renderer.sets[1];
        XER_UI_EXPECT_TRUE(s1.a == 2.f && s1.d == 2.f &&
                           s1.tx == 10.f && s1.ty == 20.f);
        const auto& s2 = renderer.sets[2];
        XER_UI_EXPECT_TRUE(s2.a == 1.f && s2.d == 1.f &&
                           s2.tx == 10.f && s2.ty == 20.f);
    }
    // Stack emptied at the root unwind → exactly one ResetTransform.
    XER_UI_EXPECT_EQ(1, renderer.resets);
    XER_UI_EXPECT_EQ(renderer.clip_pushes, renderer.clip_pops);
    XER_UI_EXPECT_EQ(renderer.pushes, renderer.pops);
}

XER_UI_TEST(WidgetTreeUnwindsLayersAndTransformsOnThrow) {
    // A widget->Render exception must unwind the clip, transform and
    // layer state of every enclosing element — the remote worker's
    // stacks cannot stay unbalanced.
    auto root = std::make_shared<ProbeWidget>("root");
    auto mid = std::make_shared<ProbeWidget>("mid");
    auto bad = std::make_shared<ThrowOnRender>("bad");
    root->AddChild(mid);
    mid->AddChild(bad);
    SetStyleProp(*root, "transform", "translate(10,20)");
    SetStyleProp(*root, "opacity", "0.5");
    SetStyleProp(*mid, "transform", "scale(2)");
    SetStyleProp(*mid, "opacity", "0.5");

    WidgetTree tree;
    tree.SetRoot(root);
    tree.Layout({0, 0, 100, 100});
    TransformRenderer renderer;
    bool threw = false;
    try {
        tree.Render(renderer, Tokens{});
    } catch (const std::exception&) {
        threw = true;
    }
    XER_UI_EXPECT_TRUE(threw);
    // Both layers popped, both transform scopes unwound (mid restores
    // the parent matrix via SetTransform; root ends on ResetTransform).
    XER_UI_EXPECT_EQ(2, renderer.pushes);
    XER_UI_EXPECT_EQ(2, renderer.pops);
    XER_UI_EXPECT_EQ(1, renderer.resets);
    XER_UI_EXPECT_EQ(renderer.clip_pushes, renderer.clip_pops);
    if (renderer.sets.size() == 3) {
        const auto& restored = renderer.sets[2];
        XER_UI_EXPECT_TRUE(restored.a == 1.f && restored.tx == 10.f);
    }
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
    // The Tab focus move delivered a Focus event first; the text event
    // is the second one the widget saw.
    XER_UI_EXPECT_EQ(2, child->handled_events);
    XER_UI_EXPECT_EQ(Event::Type::TextInput, child->last_event);
    XER_UI_EXPECT_EQ(std::string{"\xC3\xA9"}, child->last_text);
}
