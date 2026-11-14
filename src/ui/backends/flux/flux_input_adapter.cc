#include "flux_input_adapter.h"

#include <cstdint>

#include "flux/terminal/terminal_input_handler.h"
#include "ui/widgets/widget.h"
#include "ui/widgets/widget_tree.h"

namespace XER::UI::Backends {
namespace {

Widgets::MouseButton ToMouseButton(std::uint8_t button) {
    switch (button) {
    case 0: return Widgets::MouseButton::Left;
    case 1: return Widgets::MouseButton::Middle;
    case 2: return Widgets::MouseButton::Right;
    default: return Widgets::MouseButton::None;
    }
}

bool ToWidgetEvent(const XER::Flux::Terminal::InputEvent& input, Widgets::Event& event) {
    using XER::Flux::Terminal::InputEventKind;
    using Widgets::Event;
    using Widgets::KeyCode;

    switch (input.kind) {
    case InputEventKind::kText:
        event.type = Event::Type::TextInput;
        event.text = input.text;
        return true;
    case InputEventKind::kTab:
        event.type = Event::Type::KeyDown;
        event.key = KeyCode::Tab;
        return true;
    case InputEventKind::kSubmit:
        event.type = Event::Type::KeyDown;
        event.key = KeyCode::Enter;
        return true;
    case InputEventKind::kBackspace:
        event.type = Event::Type::KeyDown;
        event.key = KeyCode::Backspace;
        return true;
    case InputEventKind::kArrowUp:
        event.type = Event::Type::KeyDown;
        event.key = KeyCode::ArrowUp;
        return true;
    case InputEventKind::kArrowDown:
        event.type = Event::Type::KeyDown;
        event.key = KeyCode::ArrowDown;
        return true;
    case InputEventKind::kArrowLeft:
        event.type = Event::Type::KeyDown;
        event.key = KeyCode::ArrowLeft;
        return true;
    case InputEventKind::kArrowRight:
        event.type = Event::Type::KeyDown;
        event.key = KeyCode::ArrowRight;
        return true;
    case InputEventKind::kEscape:
        event.type = Event::Type::KeyDown;
        event.key = KeyCode::Escape;
        return true;
    case InputEventKind::kMouseDown:
        event.type = Event::Type::MouseDown;
        break;
    case InputEventKind::kMouseUp:
        event.type = Event::Type::MouseUp;
        break;
    case InputEventKind::kMouseMove:
        event.type = Event::Type::MouseMove;
        break;
    case InputEventKind::kUnknown:
    case InputEventKind::kInterrupt:
        return false;
    }
    event.mouse_button = ToMouseButton(input.button);
    event.position = Layout::Point(
        static_cast<float>(input.x - 1u),
        static_cast<float>(input.y - 1u)
    );
    return true;
}

}  // namespace

bool FluxInputAdapter::HandleInput(std::string_view input, Widgets::WidgetTree& tree) const {
    bool handled = false;
    for (const XER::Flux::Terminal::InputEvent& terminal_event :
         XER::Flux::Terminal::ParseInputBuffer(input)) {
        Widgets::Event event;
        if (ToWidgetEvent(terminal_event, event) && tree.HandleEvent(event)) {
            handled = true;
        }
    }
    return handled;
}

}  // namespace XER::UI::Backends
