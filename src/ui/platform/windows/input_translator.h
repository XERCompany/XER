#ifndef XER_UI_PLATFORM_WINDOWS_INPUT_TRANSLATOR_H
#define XER_UI_PLATFORM_WINDOWS_INPUT_TRANSLATOR_H

#include <optional>

#include <windows.h>

#include "widgets/widget.h"

namespace XER::UI::Platform::Windows {

class InputTranslator {
public:
    std::optional<Widgets::Event> Translate(UINT message, WPARAM wParam, LPARAM lParam);

private:
    std::optional<Widgets::Event> TranslateCharacter(WPARAM character);
    Widgets::Event CreateEvent(Widgets::Event::Type type) const;

    char16_t pending_high_surrogate_ = 0;
    bool shift_down_ = false;
    bool control_down_ = false;
    bool alt_down_ = false;
};

} // namespace XER::UI::Platform::Windows

#endif // XER_UI_PLATFORM_WINDOWS_INPUT_TRANSLATOR_H
