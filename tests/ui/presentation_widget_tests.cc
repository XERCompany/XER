#include "test_harness.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rendering/renderer.h"
#include "theme/theme.h"
#include "widgets/badge.h"
#include "widgets/card.h"
#include "widgets/progress.h"
#include "widgets/separator.h"
#include "widgets/slider.h"
#include "widgets/spinner.h"
#include "widgets/widget.h"
#include "widgets/widget_tree.h"

namespace {

using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Layout::Size;
using XER::UI::Rendering::Renderer;
using XER::UI::Rendering::TextStyle;
using XER::UI::Theme::Color;
using XER::UI::Theme::Tokens;
using XER::UI::Widgets::Badge;
using XER::UI::Widgets::Card;
using XER::UI::Widgets::Event;
using XER::UI::Widgets::KeyCode;
using XER::UI::Widgets::MouseButton;
using XER::UI::Widgets::Orientation;
using XER::UI::Widgets::Progress;
using XER::UI::Widgets::Separator;
using XER::UI::Widgets::Slider;
using XER::UI::Widgets::Spinner;
using XER::UI::Widgets::Widget;

class RecordingRenderer final : public Renderer {
public:
    void BeginFrame(Rect) override {}
    void EndFrame() override {}
    void FillRect(Rect rect, Color color, float) override {
        fills.push_back({rect, color});
    }
    void StrokeRect(Rect rect, Color color, float, float) override {
        strokes.push_back({rect, color});
    }
    void DrawLine(Point start, Point end, Color color, float) override {
        lines.push_back({start, end, color});
    }
    void DrawText(std::string_view text, Rect, TextStyle) override {
        drawn_text.emplace_back(text);
    }
    Size MeasureText(std::string_view text, TextStyle style) const override {
        return {static_cast<float>(text.size()) * style.size * 0.5f, style.size};
    }

    struct Fill {
        Rect rect;
        Color color;
    };
    struct Line {
        Point start;
        Point end;
        Color color;
    };

    std::vector<Fill> fills;
    std::vector<Fill> strokes;
    std::vector<Line> lines;
    std::vector<std::string> drawn_text;
};

class LayoutProbe final : public Widget {
public:
    void Render(Renderer&, const Tokens&) override {}
    void Layout(const Rect& bounds) override { SetBounds(bounds); }
    bool HandleEvent(const Event&) override { return false; }
    Size GetMinSize() const override { return {0.0f, 0.0f}; }
    Size GetPreferredSize() const override { return {20.0f, 10.0f}; }
    Size GetMaxSize() const override { return {1000.0f, 1000.0f}; }
    Rect BoundsForTest() const { return GetBounds(); }
};

Event PointerEvent(Event::Type type, MouseButton button, Point position) {
    Event event;
    event.type = type;
    event.mouse_button = button;
    event.position = position;
    return event;
}

} // namespace

XER_UI_TEST(LegacyCardLayoutUsesDefaultThemeSpacing) {
    auto card = std::make_shared<Card>();
    auto child = std::make_shared<LayoutProbe>();
    card->AddChild(child);

    card->Layout({10.0f, 20.0f, 120.0f, 80.0f});
    const Rect child_bounds = child->BoundsForTest();

    XER_UI_EXPECT_EQ(34.0f, child_bounds.x);
    XER_UI_EXPECT_EQ(44.0f, child_bounds.y);
    XER_UI_EXPECT_EQ(72.0f, child_bounds.width);
    XER_UI_EXPECT_EQ(32.0f, child_bounds.height);

    RecordingRenderer renderer;
    card->Render(renderer, XER::UI::Theme::Provider{}.GetTokens());
    XER_UI_EXPECT_TRUE(!renderer.fills.empty());
    XER_UI_EXPECT_TRUE(!renderer.strokes.empty());
}

XER_UI_TEST(WidgetTreeLayoutPassesThemeSpacingToCard) {
    auto card = std::make_shared<Card>();
    auto child = std::make_shared<LayoutProbe>();
    card->AddChild(child);

    auto tokens = XER::UI::Theme::Provider{}.GetTokens();
    tokens.spacing_large = 24.0f;
    XER::UI::Widgets::WidgetTree tree;
    tree.SetRoot(card);
    tree.Layout({10.0f, 20.0f, 120.0f, 80.0f}, tokens);

    const Rect child_bounds = child->BoundsForTest();
    XER_UI_EXPECT_EQ(34.0f, child_bounds.x);
    XER_UI_EXPECT_EQ(44.0f, child_bounds.y);
    XER_UI_EXPECT_EQ(72.0f, child_bounds.width);
    XER_UI_EXPECT_EQ(32.0f, child_bounds.height);
}

XER_UI_TEST(SeparatorDrawsRequestedOrientation) {
    const Tokens& tokens = XER::UI::Theme::Provider{}.GetTokens();
    RecordingRenderer renderer;

    Separator horizontal(Orientation::Horizontal);
    horizontal.Layout({2.0f, 3.0f, 80.0f, 12.0f});
    horizontal.Render(renderer, tokens);
    XER_UI_EXPECT_EQ(1u, renderer.lines.size());
    XER_UI_EXPECT_EQ(2.0f, renderer.lines[0].start.x);
    XER_UI_EXPECT_EQ(82.0f, renderer.lines[0].end.x);
    XER_UI_EXPECT_EQ(9.0f, renderer.lines[0].start.y);
    XER_UI_EXPECT_EQ(9.0f, renderer.lines[0].end.y);

    renderer.lines.clear();
    Separator vertical(Orientation::Vertical);
    vertical.Layout({2.0f, 3.0f, 12.0f, 80.0f});
    vertical.Render(renderer, tokens);
    XER_UI_EXPECT_EQ(1u, renderer.lines.size());
    XER_UI_EXPECT_EQ(8.0f, renderer.lines[0].start.x);
    XER_UI_EXPECT_EQ(8.0f, renderer.lines[0].end.x);
    XER_UI_EXPECT_EQ(3.0f, renderer.lines[0].start.y);
    XER_UI_EXPECT_EQ(83.0f, renderer.lines[0].end.y);
}

XER_UI_TEST(ProgressClampsToUnitRange) {
    Progress progress(-0.5f);
    XER_UI_EXPECT_EQ(0.0f, progress.GetValue());
    progress.SetValue(1.5f);
    XER_UI_EXPECT_EQ(1.0f, progress.GetValue());
    progress.SetValue(0.25f);
    XER_UI_EXPECT_EQ(0.25f, progress.GetValue());

    // The fill eases toward the value on Tick; run the tween to completion
    // before asserting the rendered width.
    progress.Tick(1.0);
    RecordingRenderer renderer;
    progress.Layout({0.0f, 0.0f, 100.0f, 10.0f});
    progress.Render(renderer, XER::UI::Theme::Provider{}.GetTokens());
    XER_UI_EXPECT_EQ(2u, renderer.fills.size());
    XER_UI_EXPECT_EQ(25.0f, renderer.fills[1].rect.width);
}

XER_UI_TEST(SpinnerPhaseChangesRenderedFrame) {
    Spinner spinner(0.0f);
    spinner.Layout({0.0f, 0.0f, 24.0f, 24.0f});
    const Tokens& tokens = XER::UI::Theme::Provider{}.GetTokens();
    RecordingRenderer first_frame;
    spinner.Render(first_frame, tokens);

    spinner.SetPhase(0.5f);
    RecordingRenderer second_frame;
    spinner.Render(second_frame, tokens);

    XER_UI_EXPECT_EQ(8u, first_frame.lines.size());
    XER_UI_EXPECT_EQ(8u, second_frame.lines.size());
    const auto& first = first_frame.lines[0];
    const auto& second = second_frame.lines[0];
    XER_UI_EXPECT_TRUE(
        first.start.x != second.start.x || first.start.y != second.start.y ||
        first.end.x != second.end.x || first.end.y != second.end.y
    );
}

XER_UI_TEST(SliderClampsAndRespondsToArrowsAndPointer) {
    Slider slider(0.0f, 10.0f, 5.0f);
    XER_UI_EXPECT_TRUE(slider.IsFocusable());
    slider.Layout({0.0f, 0.0f, 100.0f, 20.0f});
    int callback_count = 0;
    slider.SetOnValueChanged([&callback_count](float) { ++callback_count; });

    slider.SetValue(-2.0f);
    XER_UI_EXPECT_EQ(0.0f, slider.GetValue());
    XER_UI_EXPECT_EQ(1, callback_count);
    slider.SetValue(12.0f);
    XER_UI_EXPECT_EQ(10.0f, slider.GetValue());
    XER_UI_EXPECT_EQ(2, callback_count);
    slider.SetValue(10.0f);
    XER_UI_EXPECT_EQ(2, callback_count);

    slider.SetValue(5.0f);
    slider.SetFocused(true);
    Event key;
    key.type = Event::Type::KeyDown;
    key.key = KeyCode::ArrowRight;
    XER_UI_EXPECT_TRUE(slider.HandleEvent(key));
    XER_UI_EXPECT_TRUE(slider.GetValue() > 5.0f);
    XER_UI_EXPECT_EQ(4, callback_count);

    slider.HandleEvent(PointerEvent(Event::Type::MouseDown, MouseButton::Left, {100.0f, 10.0f}));
    XER_UI_EXPECT_EQ(10.0f, slider.GetValue());
    XER_UI_EXPECT_EQ(5, callback_count);
    slider.HandleEvent(PointerEvent(Event::Type::MouseDown, MouseButton::Left, {100.0f, 10.0f}));
    XER_UI_EXPECT_EQ(5, callback_count);

    slider.SetRange(2.0f, 4.0f);
    XER_UI_EXPECT_EQ(4.0f, slider.GetValue());
}

XER_UI_TEST(BadgeUpdatesItsText) {
    Badge badge("Draft");
    XER_UI_EXPECT_EQ(std::string{"Draft"}, badge.GetText());
    badge.SetText("Ready");
    XER_UI_EXPECT_EQ(std::string{"Ready"}, badge.GetText());
}
