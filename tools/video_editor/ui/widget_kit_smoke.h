// Headless smoke check ("widget_kit") for the src/ui/widgets kit —
// registered from main.cc alongside "photo_workspace". Exercises the
// state machines of PieMenu (dead-zone cancel, wedge aim, keyboard
// aim), ConfirmButton (ClickAgain arm/fire/timeout, HoldToConfirm,
// TypePhrase) and TagsInput (commit rules, select-then-delete,
// duplicates, max, reorder) through synthetic Events. No renderer is
// needed: all assertions run on HandleEvent/Tick state transitions.
// Empty string = OK, otherwise a one-line failure reason.
#pragma once

#include <string>

namespace XER::VideoEditor::Ui {

std::string WidgetKitSmoke();

}  // namespace XER::VideoEditor::Ui
