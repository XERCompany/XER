#include "input_translator.h"

#include <cstdint>
#include <string>

#include <windowsx.h>

namespace XER::UI::Platform::Windows {
namespace {

Widgets::KeyCode TranslateKey(WPARAM key) {
    switch (key) {
    case VK_TAB: return Widgets::KeyCode::Tab;
    case VK_RETURN: return Widgets::KeyCode::Enter;
    case VK_SPACE: return Widgets::KeyCode::Space;
    case VK_BACK: return Widgets::KeyCode::Backspace;
    case VK_DELETE: return Widgets::KeyCode::Delete;
    case VK_LEFT: return Widgets::KeyCode::ArrowLeft;
    case VK_RIGHT: return Widgets::KeyCode::ArrowRight;
    case VK_UP: return Widgets::KeyCode::ArrowUp;
    case VK_DOWN: return Widgets::KeyCode::ArrowDown;
    case VK_HOME: return Widgets::KeyCode::Home;
    case VK_END: return Widgets::KeyCode::End;
    case VK_PRIOR: return Widgets::KeyCode::PageUp;
    case VK_NEXT: return Widgets::KeyCode::PageDown;
    case VK_ESCAPE: return Widgets::KeyCode::Escape;
    case VK_F5: return Widgets::KeyCode::F5;
    default: return Widgets::KeyCode::None;
    }
}

Widgets::KeyCode TranslateShortcutKey(WPARAM key) {
    switch (key) {
    case 'A': return Widgets::KeyCode::A;
    case 'C': return Widgets::KeyCode::C;
    case 'F': return Widgets::KeyCode::F;
    case 'K': return Widgets::KeyCode::K;
    case 'N': return Widgets::KeyCode::N;
    case 'O': return Widgets::KeyCode::O;
    case 'P': return Widgets::KeyCode::P;
    case 'S': return Widgets::KeyCode::S;
    case 'V': return Widgets::KeyCode::V;
    case 'X': return Widgets::KeyCode::X;
    case 'Y': return Widgets::KeyCode::Y;
    case 'Z': return Widgets::KeyCode::Z;
    default: return Widgets::KeyCode::None;
    }
}

void AppendUtf8(std::string& text, std::uint32_t code_point) {
    if (code_point <= 0x7F) {
        text.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        text.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        text.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        text.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

constexpr std::uint32_t ReplacementCharacter = 0xFFFD;

Widgets::MouseButton TranslateMouseButton(UINT message) {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        return Widgets::MouseButton::Left;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        return Widgets::MouseButton::Middle;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        return Widgets::MouseButton::Right;
    default:
        return Widgets::MouseButton::None;
    }
}

} // namespace

Widgets::Event InputTranslator::CreateEvent(Widgets::Event::Type type) const {
    Widgets::Event event;
    event.type = type;
    event.is_shift = shift_down_;
    event.is_ctrl = control_down_;
    event.is_alt = alt_down_;
    return event;
}

std::optional<Widgets::Event> InputTranslator::Translate(
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
            shift_down_ = true;
            return std::nullopt;
        }
        if (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL) {
            control_down_ = true;
            return std::nullopt;
        }
        if (wParam == VK_MENU || wParam == VK_LMENU || wParam == VK_RMENU) {
            alt_down_ = true;
            return std::nullopt;
        }
        // Ctrl+Alt is AltGr on many layouts and types characters.
        if (control_down_ && !alt_down_) {
            if (const auto key = TranslateShortcutKey(wParam);
                key != Widgets::KeyCode::None) {
                auto event = CreateEvent(Widgets::Event::Type::KeyDown);
                event.key = key;
                return event;
            }
        }
        if (const auto key = TranslateKey(wParam); key != Widgets::KeyCode::None) {
            auto event = CreateEvent(Widgets::Event::Type::KeyDown);
            event.key = key;
            return event;
        }
        return std::nullopt;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
            shift_down_ = false;
            return std::nullopt;
        }
        if (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL) {
            control_down_ = false;
            return std::nullopt;
        }
        if (wParam == VK_MENU || wParam == VK_LMENU || wParam == VK_RMENU) {
            alt_down_ = false;
            return std::nullopt;
        }
        if (const auto key = TranslateKey(wParam); key != Widgets::KeyCode::None) {
            auto event = CreateEvent(Widgets::Event::Type::KeyUp);
            event.key = key;
            return event;
        }
        return std::nullopt;

    case WM_CHAR:
        return TranslateCharacter(wParam);

    case WM_MOUSEMOVE: {
        auto event = CreateEvent(Widgets::Event::Type::MouseMove);
        event.position = Layout::Point(
            static_cast<float>(GET_X_LPARAM(lParam)),
            static_cast<float>(GET_Y_LPARAM(lParam))
        );
        // wParam carries the currently held buttons so widgets can distinguish
        // hover moves from drag moves.
        if (wParam & MK_LBUTTON) {
            event.mouse_button = Widgets::MouseButton::Left;
        } else if (wParam & MK_MBUTTON) {
            event.mouse_button = Widgets::MouseButton::Middle;
        } else if (wParam & MK_RBUTTON) {
            event.mouse_button = Widgets::MouseButton::Right;
        }
        return event;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        auto event = CreateEvent(Widgets::Event::Type::MouseDown);
        event.mouse_button = TranslateMouseButton(message);
        event.position = Layout::Point(
            static_cast<float>(GET_X_LPARAM(lParam)),
            static_cast<float>(GET_Y_LPARAM(lParam))
        );
        return event;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        auto event = CreateEvent(Widgets::Event::Type::MouseUp);
        event.mouse_button = TranslateMouseButton(message);
        event.position = Layout::Point(
            static_cast<float>(GET_X_LPARAM(lParam)),
            static_cast<float>(GET_Y_LPARAM(lParam))
        );
        return event;
    }

    case WM_SETFOCUS:
        return CreateEvent(Widgets::Event::Type::Focus);

    case WM_KILLFOCUS:
        shift_down_ = false;
        control_down_ = false;
        alt_down_ = false;
        return CreateEvent(Widgets::Event::Type::Blur);

    default:
        return std::nullopt;
    }
}

std::optional<Widgets::Event> InputTranslator::TranslateCharacter(WPARAM character) {
    const auto code_unit = static_cast<char16_t>(character & 0xFFFF);
    if (code_unit < 0x20) {
        return std::nullopt;
    }
    if (code_unit >= 0xD800 && code_unit <= 0xDBFF) {
        if (pending_high_surrogate_ != 0) {
            pending_high_surrogate_ = code_unit;
            auto event = CreateEvent(Widgets::Event::Type::TextInput);
            AppendUtf8(event.text, ReplacementCharacter);
            return event;
        }
        pending_high_surrogate_ = code_unit;
        return std::nullopt;
    }

    auto event = CreateEvent(Widgets::Event::Type::TextInput);
    if (code_unit >= 0xDC00 && code_unit <= 0xDFFF) {
        if (pending_high_surrogate_ == 0) {
            AppendUtf8(event.text, ReplacementCharacter);
        } else {
            const auto code_point = 0x10000u +
                ((static_cast<std::uint32_t>(pending_high_surrogate_) - 0xD800u) << 10) +
                (static_cast<std::uint32_t>(code_unit) - 0xDC00u);
            AppendUtf8(event.text, code_point);
            pending_high_surrogate_ = 0;
        }
        return event;
    }

    if (pending_high_surrogate_ != 0) {
        AppendUtf8(event.text, ReplacementCharacter);
        pending_high_surrogate_ = 0;
    }
    AppendUtf8(event.text, static_cast<std::uint32_t>(code_unit));
    return event;
}

} // namespace XER::UI::Platform::Windows
