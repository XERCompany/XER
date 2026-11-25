// Tests for the chromium-views port batch:
//  - T1: Label honest measurement via the layout-time text-measure
//    target (SetTextMeasureTarget) and the Render-populated cache.
//  - T2: FocusReason on the focus path — the ring paints only for
//    traversal/programmatic focus, never for mouse clicks.
//  - T4: WidgetTree::AdvanceFocus / GetFocusedWidget / RequestFocus.

#include "ui/layout/flex.h"
#include "ui/rendering/renderer.h"
#include "ui/theme/theme.h"
#include "ui/widgets/label.h"
#include "ui/widgets/widget.h"
#include "ui/widgets/widget_tree.h"

#include <memory>
#include <string_view>

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
using XER::UI::Widgets::FocusReason;
using XER::UI::Widgets::KeyCode;
using XER::UI::Widgets::Label;
using XER::UI::Widgets::MouseButton;
using XER::UI::Widgets::Widget;
using XER::UI::Widgets::WidgetTree;

bool NearlyEqual(float a, float b, float eps = 0.001f) {
    return (a - b) < eps && (b - a) < eps;
}

// Deterministic text metrics: 5px per UTF-8 codepoint, 20px line
// height. The U+2026 ellipsis is one codepoint → 5px wide.
class FixedTextRenderer final : public Renderer {
public:
    void BeginFrame(Rect) override {}
    void EndFrame() override {}
    void FillRect(Rect, Color, float) override {}
    void StrokeRect(Rect, Color, float, float) override {}
    void DrawLine(Point, Point, Color, float) override {}
    void DrawText(std::string_view, Rect, TextStyle) override {}
    Size MeasureText(std::string_view text, TextStyle) const override {
        float width = 0.0f;
        for (const unsigned char byte : text) {
            if ((byte & 0xC0u) != 0x80u) {
                width += 5.0f;
            }
        }
        return {width, 20.0f};
    }
};

// Minimal widget. Layout tiles children into equal horizontal bands so
// tests can point-hit each one; Focus/Blur events are counted to verify
// the tree's synthetic delivery. `focusable` lets a container stay out
// of the traversal order while its children walk it.
class FocusBox final : public Widget {
public:
    int focus_events = 0;
    int blur_events = 0;

    explicit FocusBox(bool focusable = true) { SetFocusable(focusable); }

    void Render(Renderer&, const Tokens&) override {}
    void Layout(const Rect& bounds) override {
        SetBounds(bounds);
        const Rect box = GetBounds();
        const auto& kids = GetChildren();
        if (kids.empty()) {
            return;
        }
        const float h = box.height / static_cast<float>(kids.size());
        float y = box.y;
        for (const auto& kid : kids) {
            kid->Layout(Rect{box.x, y, box.width, h});
            y += h;
        }
    }
    using Widget::Layout;
    bool HandleEvent(const Event& event) override {
        if (event.type == Event::Type::Focus) {
            ++focus_events;
        }
        if (event.type == Event::Type::Blur) {
            ++blur_events;
        }
        return false;
    }
    Size GetMinSize() const override { return {0.0f, 0.0f}; }
    Size GetPreferredSize() const override { return {10.0f, 10.0f}; }
    Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }
};

Event MouseDownAt(float x, float y) {
    Event ev{};
    ev.type = Event::Type::MouseDown;
    ev.mouse_button = MouseButton::Left;
    ev.position = {x, y};
    return ev;
}

Event TabKey(bool shift) {
    Event ev{};
    ev.type = Event::Type::KeyDown;
    ev.key = KeyCode::Tab;
    ev.is_shift = shift;
    return ev;
}

struct MeasureTargetGuard {
    ~MeasureTargetGuard() {
        XER::UI::Widgets::SetTextMeasureTarget(nullptr);
    }
};

}  // namespace

XER_UI_TEST(LabelPreferredSizeUsesMeasureTarget) {
    FixedTextRenderer renderer;
    XER::UI::Widgets::SetTextMeasureTarget(&renderer);
    const MeasureTargetGuard guard;

    Label label("hello");  // 5 codepoints → 25x20
    const Size pref = label.GetPreferredSize();
    XER_UI_EXPECT_TRUE(NearlyEqual(pref.width, 25.0f));
    XER_UI_EXPECT_TRUE(NearlyEqual(pref.height, 20.0f));
}

XER_UI_TEST(LabelMinSizeIsEllipsisFloor) {
    FixedTextRenderer renderer;
    XER::UI::Widgets::SetTextMeasureTarget(&renderer);
    const MeasureTargetGuard guard;

    Label label("a long label that gets squeezed");
    const Size min = label.GetMinSize();
    // One ellipsis glyph wide (5px), one measured line tall (20px) —
    // never {0,0}, which let flex layouts assign zero extent.
    XER_UI_EXPECT_TRUE(NearlyEqual(min.width, 5.0f));
    XER_UI_EXPECT_TRUE(NearlyEqual(min.height, 20.0f));

    // The fallback without a measure target is still honest: a small
    // non-zero floor instead of {0,0}.
    XER::UI::Widgets::SetTextMeasureTarget(nullptr);
    const Size fallback = label.GetMinSize();
    XER_UI_EXPECT_TRUE(fallback.width > 0.0f && fallback.height > 0.0f);
    XER_UI_EXPECT_TRUE(fallback.width < label.GetPreferredSize().width);
}

XER_UI_TEST(LabelRenderCachesMeasuredSize) {
    // No target installed: preferred size starts on the estimate, then
    // the first Render() captures the real measurement.
    Label label("hello");
    XER_UI_EXPECT_TRUE(
        NearlyEqual(label.GetPreferredSize().width, 5.0f * 7.7f));

    FixedTextRenderer renderer;
    XER::UI::Theme::Provider provider;
    label.Layout(Rect{0.0f, 0.0f, 200.0f, 30.0f}, provider.GetTokens());
    label.Render(renderer, provider.GetTokens());
    const Size pref = label.GetPreferredSize();
    XER_UI_EXPECT_TRUE(NearlyEqual(pref.width, 25.0f));
    XER_UI_EXPECT_TRUE(NearlyEqual(pref.height, 20.0f));
}

XER_UI_TEST(LabelIsDisplayTextClipped) {
    FixedTextRenderer renderer;
    XER::UI::Widgets::SetTextMeasureTarget(&renderer);
    const MeasureTargetGuard guard;

    Label label("1234567890");  // measured 50px wide
    label.Layout(Rect{0.0f, 0.0f, 30.0f, 20.0f});
    XER_UI_EXPECT_TRUE(label.IsDisplayTextClipped());
    label.Layout(Rect{0.0f, 0.0f, 80.0f, 20.0f});
    XER_UI_EXPECT_FALSE(label.IsDisplayTextClipped());
}

XER_UI_TEST(MouseFocusShowsNoRingTraversalDoes) {
    auto root = std::make_shared<FocusBox>(/*focusable=*/false);
    auto first = std::make_shared<FocusBox>();
    auto second = std::make_shared<FocusBox>();
    root->AddChild(first);
    root->AddChild(second);

    WidgetTree tree;
    tree.SetRoot(root);
    tree.Layout(Rect{0.0f, 0.0f, 100.0f, 40.0f});

    // Mouse click on the second band: widget takes focus (keys reach
    // it) but the ring must not paint — views' pointer-focus rule.
    tree.HandleEvent(MouseDownAt(50.0f, 30.0f));
    XER_UI_EXPECT_TRUE(second->IsFocused());
    XER_UI_EXPECT_EQ(FocusReason::kPointer, second->GetFocusReason());
    XER_UI_EXPECT_FALSE(second->IsFocusRingVisible());
    XER_UI_EXPECT_EQ(1, second->focus_events);

    // Tab traversal moves focus AND turns the ring on.
    XER_UI_EXPECT_TRUE(tree.AdvanceFocus(false));
    XER_UI_EXPECT_TRUE(first->IsFocused());
    XER_UI_EXPECT_EQ(FocusReason::kTraversal, first->GetFocusReason());
    XER_UI_EXPECT_TRUE(first->IsFocusRingVisible());
    // Blur/Focus were delivered on the tree-level move.
    XER_UI_EXPECT_EQ(1, second->blur_events);
    XER_UI_EXPECT_EQ(1, first->focus_events);
}

XER_UI_TEST(TabKeyDownRoutesThroughAdvanceFocus) {
    auto root = std::make_shared<FocusBox>(/*focusable=*/false);
    auto first = std::make_shared<FocusBox>();
    auto second = std::make_shared<FocusBox>();
    root->AddChild(first);
    root->AddChild(second);

    WidgetTree tree;
    tree.SetRoot(root);
    tree.Layout(Rect{0.0f, 0.0f, 100.0f, 40.0f});

    XER_UI_EXPECT_TRUE(tree.HandleEvent(TabKey(false)));
    XER_UI_EXPECT_TRUE(first->IsFocused());
    XER_UI_EXPECT_TRUE(tree.HandleEvent(TabKey(false)));
    XER_UI_EXPECT_TRUE(second->IsFocused());
    // Wrap-around: the third Tab cycles back to the first widget.
    XER_UI_EXPECT_TRUE(tree.HandleEvent(TabKey(false)));
    XER_UI_EXPECT_TRUE(first->IsFocused());
    // Shift+Tab walks backwards, also wrapping.
    XER_UI_EXPECT_TRUE(tree.HandleEvent(TabKey(true)));
    XER_UI_EXPECT_TRUE(second->IsFocused());
}

XER_UI_TEST(RequestFocusIsProgrammaticRing) {
    auto root = std::make_shared<FocusBox>(/*focusable=*/false);
    auto first = std::make_shared<FocusBox>();
    root->AddChild(first);

    WidgetTree tree;
    tree.SetRoot(root);
    tree.Layout(Rect{0.0f, 0.0f, 100.0f, 40.0f});

    tree.RequestFocus(first);
    XER_UI_EXPECT_TRUE(first->IsFocused());
    XER_UI_EXPECT_EQ(FocusReason::kProgrammatic,
                     first->GetFocusReason());
    XER_UI_EXPECT_TRUE(first->IsFocusRingVisible());
    XER_UI_EXPECT_TRUE(tree.GetFocusedWidget() == first);

    tree.RequestFocus(nullptr);
    XER_UI_EXPECT_TRUE(tree.GetFocusedWidget() == nullptr);
    XER_UI_EXPECT_FALSE(first->IsFocused());
    XER_UI_EXPECT_EQ(1, first->blur_events);
}

XER_UI_TEST(TraversalUpgradesPointerFocusToRing) {
    // Tabbing to the ALREADY-focused widget still flips the reason so
    // the ring appears once the user starts navigating by keyboard.
    auto root = std::make_shared<FocusBox>(/*focusable=*/false);
    auto only = std::make_shared<FocusBox>();
    root->AddChild(only);

    WidgetTree tree;
    tree.SetRoot(root);
    tree.Layout(Rect{0.0f, 0.0f, 100.0f, 40.0f});

    tree.HandleEvent(MouseDownAt(50.0f, 20.0f));
    XER_UI_EXPECT_TRUE(only->IsFocused());
    XER_UI_EXPECT_FALSE(only->IsFocusRingVisible());
    // Single focusable widget: AdvanceFocus wraps back onto it.
    XER_UI_EXPECT_TRUE(tree.AdvanceFocus(false));
    XER_UI_EXPECT_TRUE(only->IsFocused());
    XER_UI_EXPECT_TRUE(only->IsFocusRingVisible());
    XER_UI_EXPECT_EQ(FocusReason::kTraversal, only->GetFocusReason());
}
