#include "test_harness.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "flux/input/input_manager.h"
#include "flux/rendering/render_context.h"
#include "flux/rendering/renderer.h"
#include "flux/terminal/terminal_colors.h"
#include "flux/terminal/terminal_input_handler.h"
#include "ui/backends/flux/flux_input_adapter.h"
#include "ui/backends/flux/flux_renderer.h"
#include "ui/widgets/widget.h"
#include "ui/widgets/widget_tree.h"

namespace {

using XER::UI::Layout::Rect;
using XER::UI::Layout::Size;
using XER::UI::Rendering::TextStyle;
using XER::UI::Theme::Color;
using XER::UI::Theme::Tokens;
using XER::UI::Widgets::Event;
using XER::UI::Widgets::KeyCode;
using XER::UI::Widgets::Widget;
using XER::UI::Widgets::WidgetTree;

class RecordingWidget final : public Widget {
public:
    void Render(XER::UI::Rendering::Renderer&, const Tokens&) override {}

    void Layout(const Rect& bounds) override {
        SetBounds(bounds);
    }

    bool HandleEvent(const Event& event) override {
        events.push_back(event);
        return true;
    }

    Size GetMinSize() const override { return {0, 0}; }
    Size GetPreferredSize() const override { return {0, 0}; }
    Size GetMaxSize() const override { return {1000, 1000}; }

    std::vector<Event> events;
};

std::shared_ptr<RecordingWidget> FocusedWidget(WidgetTree& tree) {
    auto widget = std::make_shared<RecordingWidget>();
    widget->SetFocusable(true);
    tree.SetRoot(widget);
    tree.Layout({0, 0, 20, 10});
    return widget;
}

}  // namespace

XER_UI_TEST(FluxRendererDrawsThemeAwarePrimitiveToFrameBuffer) {
    XER::Flux::Rendering::RenderContext context({8, 5, true});
    XER::UI::Backends::FluxRenderer renderer(context);

    renderer.FillRect({1.2f, 0.2f, 2.0f, 1.3f}, {1, 0, 0, 1}, 0);

    XER_UI_EXPECT_EQ(
        XER::Flux::Terminal::TerminalColor::kBrightRed,
        context.frame().GetCell(1, 0).style.background
    );
    XER_UI_EXPECT_EQ(
        XER::Flux::Terminal::TerminalColor::kBrightRed,
        context.frame().GetCell(2, 1).style.background
    );
    XER_UI_EXPECT_EQ(' ', context.frame().GetCell(1, 0).glyph);
    XER_UI_EXPECT_EQ(
        XER::Flux::Terminal::TerminalColor::kDefault,
        context.frame().GetCell(0, 0).style.background
    );

    renderer.StrokeRect({0, 2, 3, 3}, {0, 1, 0, 1}, 1, 0);
    XER_UI_EXPECT_EQ('+', context.frame().GetCell(0, 2).glyph);
    XER_UI_EXPECT_EQ('|', context.frame().GetCell(0, 3).glyph);
    renderer.DrawLine({4, 3}, {7, 3}, {0, 1, 0, 1}, 1);
    XER_UI_EXPECT_EQ('-', context.frame().GetCell(5, 3).glyph);
    XER_UI_EXPECT_EQ(
        XER::Flux::Terminal::TerminalColor::kBrightGreen,
        context.frame().GetCell(5, 3).style.foreground
    );
    XER_UI_EXPECT_EQ(Size(2, 2), renderer.MeasureText("A\xC3\xA9\nbc", {}));
}

XER_UI_TEST(FluxRendererProducesPlainAndAnsiCompatibleCells) {
    XER::Flux::Rendering::RenderContext context({8, 2, true});
    XER::UI::Backends::FluxRenderer renderer(context);

    renderer.DrawText(
        "A\xC3\xA9" "B",
        {0, 0, 8, 1},
        TextStyle{Color{1, 0, 0, 1}, 14, 700}
    );
    renderer.FillRect({4, 0, 2, 1}, {0, 0, 1, 1}, 0);

    const XER::Flux::Rendering::Renderer serializer;
    XER_UI_EXPECT_EQ(std::string("A?B\n\n"), serializer.RenderPlainText(context));
    XER_UI_EXPECT_EQ(
        std::string("\x1b[1m\x1b[91mA?B\x1b[0m \x1b[104m  \x1b[0m\n\n"),
        serializer.RenderAnsi(context)
    );
}

XER_UI_TEST(FluxInputDispatchesTabTextBackspaceAndArrows) {
    WidgetTree tree;
    auto widget = FocusedWidget(tree);
    XER::UI::Backends::FluxInputAdapter adapter;

    XER_UI_EXPECT_TRUE(adapter.HandleInput("\t", tree));
    XER_UI_EXPECT_TRUE(widget->IsFocused());
    XER_UI_EXPECT_TRUE(adapter.HandleInput("h\xC3\xA9", tree));
    XER_UI_EXPECT_TRUE(adapter.HandleInput("\x7f", tree));
    XER_UI_EXPECT_TRUE(adapter.HandleInput("\x1b[A\x1b[D", tree));
    XER_UI_EXPECT_TRUE(adapter.HandleInput("\r\x1b[B\x1b[C", tree));

    XER_UI_EXPECT_EQ(std::size_t{7}, widget->events.size());
    XER_UI_EXPECT_EQ(Event::Type::TextInput, widget->events[0].type);
    XER_UI_EXPECT_EQ(std::string("h\xC3\xA9"), widget->events[0].text);
    XER_UI_EXPECT_EQ(Event::Type::KeyDown, widget->events[1].type);
    XER_UI_EXPECT_EQ(KeyCode::Backspace, widget->events[1].key);
    XER_UI_EXPECT_EQ(KeyCode::ArrowUp, widget->events[2].key);
    XER_UI_EXPECT_EQ(KeyCode::ArrowLeft, widget->events[3].key);
    XER_UI_EXPECT_EQ(KeyCode::Enter, widget->events[4].key);
    XER_UI_EXPECT_EQ(KeyCode::ArrowDown, widget->events[5].key);
    XER_UI_EXPECT_EQ(KeyCode::ArrowRight, widget->events[6].key);
}

XER_UI_TEST(FluxInputMapsSgrMouseCoordinates) {
    WidgetTree tree;
    auto widget = FocusedWidget(tree);
    XER::UI::Backends::FluxInputAdapter adapter;

    const auto parsed = XER::Flux::Terminal::ParseInputBuffer("\x1b[<0;5;4M");
    XER_UI_EXPECT_EQ(std::size_t{1}, parsed.size());
    XER_UI_EXPECT_EQ(XER::Flux::Terminal::InputEventKind::kMouseDown, parsed[0].kind);
    XER_UI_EXPECT_EQ(std::uint8_t{0}, parsed[0].button);
    XER_UI_EXPECT_EQ(std::uint16_t{5}, parsed[0].x);
    XER_UI_EXPECT_EQ(std::uint16_t{4}, parsed[0].y);

    XER_UI_EXPECT_TRUE(
        adapter.HandleInput("\x1b[<0;5;4M\x1b[<32;6;5M\x1b[<0;5;4m", tree)
    );
    XER_UI_EXPECT_EQ(std::size_t{4}, widget->events.size());
    XER_UI_EXPECT_EQ(Event::Type::MouseDown, widget->events[0].type);
    XER_UI_EXPECT_EQ(XER::UI::Widgets::MouseButton::Left, widget->events[0].mouse_button);
    XER_UI_EXPECT_EQ(4.0f, widget->events[0].position.x);
    XER_UI_EXPECT_EQ(3.0f, widget->events[0].position.y);
    XER_UI_EXPECT_EQ(Event::Type::MouseMove, widget->events[1].type);
    XER_UI_EXPECT_EQ(XER::UI::Widgets::MouseButton::Left, widget->events[1].mouse_button);
    XER_UI_EXPECT_EQ(5.0f, widget->events[1].position.x);
    XER_UI_EXPECT_EQ(4.0f, widget->events[1].position.y);
    XER_UI_EXPECT_EQ(Event::Type::MouseUp, widget->events[2].type);
    XER_UI_EXPECT_EQ(XER::UI::Widgets::MouseButton::Left, widget->events[2].mouse_button);
    // MouseUp on the pressed target synthesizes a Click, like native UI.
    XER_UI_EXPECT_EQ(Event::Type::Click, widget->events[3].type);
    XER_UI_EXPECT_EQ(XER::UI::Widgets::MouseButton::Left, widget->events[3].mouse_button);

    XER_UI_EXPECT_TRUE(adapter.HandleInput("\x1b[<1;5;4M\x1b[<2;5;4m", tree));
    XER_UI_EXPECT_EQ(std::size_t{6}, widget->events.size());
    XER_UI_EXPECT_EQ(Event::Type::MouseDown, widget->events[4].type);
    XER_UI_EXPECT_EQ(XER::UI::Widgets::MouseButton::Middle, widget->events[4].mouse_button);
    XER_UI_EXPECT_EQ(Event::Type::MouseUp, widget->events[5].type);
    XER_UI_EXPECT_EQ(XER::UI::Widgets::MouseButton::Right, widget->events[5].mouse_button);
}

XER_UI_TEST(FluxRendererClipsLinesToFrame) {
    XER::Flux::Rendering::RenderContext context({4, 3, true});
    XER::UI::Backends::FluxRenderer renderer(context);

    renderer.DrawLine({-8, -8}, {-3, -3}, {1, 0, 0, 1}, 1);
    XER_UI_EXPECT_EQ(' ', context.frame().GetCell(0, 0).glyph);
    XER_UI_EXPECT_EQ(
        XER::Flux::Terminal::TerminalColor::kDefault,
        context.frame().GetCell(0, 0).style.foreground
    );

    renderer.DrawLine({-2, 1}, {2, 1}, {1, 0, 0, 1}, 1);
    XER_UI_EXPECT_EQ('-', context.frame().GetCell(0, 1).glyph);
    XER_UI_EXPECT_EQ('-', context.frame().GetCell(2, 1).glyph);
    XER_UI_EXPECT_EQ(
        XER::Flux::Terminal::TerminalColor::kBrightRed,
        context.frame().GetCell(0, 1).style.foreground
    );
}

XER_UI_TEST(MalformedEscapeDoesNotBecomeText) {
    WidgetTree tree;
    auto widget = FocusedWidget(tree);
    XER::UI::Backends::FluxInputAdapter adapter;

    XER_UI_EXPECT_FALSE(adapter.HandleInput("\x1b[<0;0;4M", tree));
    XER_UI_EXPECT_FALSE(adapter.HandleInput("\x1b[?25l", tree));
    XER_UI_EXPECT_FALSE(adapter.HandleInput("\x1b[<0;2", tree));
    XER_UI_EXPECT_FALSE(adapter.HandleInput("\x1bPignored\x1b\\", tree));
    XER_UI_EXPECT_TRUE(widget->events.empty());
}

XER_UI_TEST(InputManagerCommandHistoryAndBindingsRemainUnchanged) {
    XER::Flux::Input::InputManager manager;

    const auto command = manager.Feed("open file.txt\n");
    XER_UI_EXPECT_EQ(std::size_t{1}, command.size());
    XER_UI_EXPECT_EQ(XER::Flux::Input::InputDispatchKind::kCommand, command[0].kind);
    XER_UI_EXPECT_EQ(std::string("open file.txt"), command[0].buffer);
    XER_UI_EXPECT_EQ(std::string("open"), command[0].parsed_command.name);
    XER_UI_EXPECT_EQ(std::size_t{1}, manager.history().size());

    const auto history = manager.Feed("\x1b[A");
    XER_UI_EXPECT_EQ(std::size_t{1}, history.size());
    XER_UI_EXPECT_EQ(XER::Flux::Input::InputDispatchKind::kHistoryRecall, history[0].kind);
    XER_UI_EXPECT_EQ(std::string("open file.txt"), manager.current_buffer());

    XER_UI_EXPECT_TRUE(manager.bindings().Register("\x1b", "help"));
    const auto binding = manager.Feed("\x1b");
    XER_UI_EXPECT_EQ(std::size_t{1}, binding.size());
    XER_UI_EXPECT_EQ(XER::Flux::Input::InputDispatchKind::kBinding, binding[0].kind);
    XER_UI_EXPECT_EQ(std::string("help"), binding[0].binding_command);

    manager.ClearCurrentBuffer();
    XER_UI_EXPECT_TRUE(manager.Feed("\t").empty());
    XER_UI_EXPECT_EQ(std::string("\t"), manager.current_buffer());
}
