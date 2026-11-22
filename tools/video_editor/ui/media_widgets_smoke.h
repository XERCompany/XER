// Headless smoke hook for the media widgets registered as
// "media_widgets" in main.cc — exercises Knob (vertical/circular drag,
// Shift-fine, detents, wheel/keys, live-vs-committed, double-click
// reset) without opening a window. Same contract as
// PhotoWorkspaceSmoke/DataWidgetsSmoke: "" on success, one-line
// failure reason otherwise.
#pragma once

#include <string>

namespace XER::VideoEditor::Ui {

std::string MediaWidgetsSmoke();

}  // namespace XER::VideoEditor::Ui
