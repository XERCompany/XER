// Headless smoke entry for the color-picking widget set —
// registered from main.cc as the "color_widgets" check (the suite
// lives in the UI-free core, so it can't see these widget classes).
// Exercises ColorPicker / GradientEditor / Eyedropper plus the
// colorspace/oklch conversion helpers without a renderer: constructs
// the widgets, lays them out, scripts drags and picks through
// synthetic Events, asserts on state and callback sequencing.
// Empty string = OK, otherwise a one-line failure reason.
#pragma once

#include <string>

namespace XER::VideoEditor::Ui2 {

std::string ColorWidgetsSmoke();

}  // namespace XER::VideoEditor::Ui2
